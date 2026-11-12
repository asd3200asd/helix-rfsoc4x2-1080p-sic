#include "xparameters.h"
#include "xaxidma.h"
#include "xaxidma_hw.h"
#include "xgpiops.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xtime_l.h"

#if defined(XPAR_ETH_DMA_INTERNAL_AXI_ETH_DMA_INTERNAL_DEVICE_ID)
#define PS_DMA_DEVICE_ID XPAR_ETH_DMA_INTERNAL_AXI_ETH_DMA_INTERNAL_DEVICE_ID
#define PS_DMA_BASEADDR  XPAR_ETH_DMA_INTERNAL_AXI_ETH_DMA_INTERNAL_BASEADDR
#elif defined(XPAR_AXI_ETH_DMA_INTERNAL_DEVICE_ID)
#define PS_DMA_DEVICE_ID XPAR_AXI_ETH_DMA_INTERNAL_DEVICE_ID
#define PS_DMA_BASEADDR  XPAR_AXI_ETH_DMA_INTERNAL_BASEADDR
#else
#error "V46 eth_dma_internal AXI DMA was not found in xparameters.h"
#endif

#if defined(XPAR_PS_QSFP_MODE_GPIO_BASEADDR)
#define MODE_GPIO_BASEADDR XPAR_PS_QSFP_MODE_GPIO_BASEADDR
#elif defined(XPAR_PS_QSFP_MODE_GPIO_0_BASEADDR)
#define MODE_GPIO_BASEADDR XPAR_PS_QSFP_MODE_GPIO_0_BASEADDR
#else
#define MODE_GPIO_BASEADDR 0xB0120000U
#endif

#if defined(XPAR_STREAM_MANAGER_0_S00_AXI_BASEADDR)
#define STREAM_MANAGER_BASEADDR XPAR_STREAM_MANAGER_0_S00_AXI_BASEADDR
#else
#define STREAM_MANAGER_BASEADDR 0xB000A000U
#endif

#define TEST_BYTES       4096U
#define RESET_LIMIT      1000000U
#define TIMEOUT_SECONDS  5U
#define MODE_PS_HELIX    1U
#define GPIO_DATA        0x00U
#define GPIO2_DATA       0x08U
#define GPIO_TRI         0x04U
#define GPIO2_TRI        0x0CU
#define CONTROL_CLEAR    (1U << 2U)
#define STATUS_MODE_MASK 0x3U
#define STATUS_PENDING   (1U << 2U)
#define STATUS_KEEP_ERR  (1U << 5U)
#define STREAM_CONTROL   0x00U
#define STREAM_TX_SIZE   0x18U
#define STREAM_TX_START  (1U << 30U)
#define STREAM_BEAT_BYTES 8U
#define EMIO_GPIO_OFFSET  78U
#define HARD_RESET_TX_PIN (EMIO_GPIO_OFFSET + 0U)

#define RESULT_RUNNING   0x56343601U
#define RESULT_PASS      0x563436A5U
#define RESULT_TIMEOUT   0x563436E1U

static u8 TxBuffer[TEST_BYTES] __attribute__((aligned(64)));
static XAxiDma PsDma;
static XGpioPs PsGpio;
volatile u32 V46SmokeResult = RESULT_RUNNING;

static void publish_result(u32 value)
{
    V46SmokeResult = value;
    Xil_DCacheFlushRange((INTPTR)&V46SmokeResult,
                         (u32)sizeof(V46SmokeResult));
}

static u32 dma_status(void)
{
    return Xil_In32((UINTPTR)PS_DMA_BASEADDR + XAXIDMA_TX_OFFSET +
                    XAXIDMA_SR_OFFSET);
}

static void print_dma_status(const char *tag, u32 status)
{
    xil_printf("%s MM2S_DMASR=0x%08lx [", tag, (unsigned long)status);
    if ((status & XAXIDMA_HALTED_MASK) != 0U)       xil_printf(" HALTED");
    if ((status & XAXIDMA_IDLE_MASK) != 0U)         xil_printf(" IDLE");
    if ((status & XAXIDMA_ERR_INTERNAL_MASK) != 0U) xil_printf(" DMA_INT_ERR");
    if ((status & XAXIDMA_ERR_SLAVE_MASK) != 0U)    xil_printf(" DMA_SLV_ERR");
    if ((status & XAXIDMA_ERR_DECODE_MASK) != 0U)   xil_printf(" DMA_DEC_ERR");
    if ((status & XAXIDMA_IRQ_IOC_MASK) != 0U)      xil_printf(" IOC");
    if ((status & XAXIDMA_IRQ_ERROR_MASK) != 0U)    xil_printf(" ERROR_IRQ");
    xil_printf(" ]\r\n");
}

static int set_ps_helix_mode(void)
{
    u32 count;
    u32 status;

    Xil_Out32((UINTPTR)MODE_GPIO_BASEADDR + GPIO_TRI, 0x0U);
    Xil_Out32((UINTPTR)MODE_GPIO_BASEADDR + GPIO2_TRI, 0xFFFFFFFFU);
    Xil_Out32((UINTPTR)MODE_GPIO_BASEADDR + GPIO_DATA,
              MODE_PS_HELIX | CONTROL_CLEAR);
    for (count = 0U; count < 10000U; ++count) { __asm__ volatile("nop"); }
    Xil_Out32((UINTPTR)MODE_GPIO_BASEADDR + GPIO_DATA, MODE_PS_HELIX);

    for (count = 0U; count < RESET_LIMIT; ++count) {
        status = Xil_In32((UINTPTR)MODE_GPIO_BASEADDR + GPIO2_DATA);
        if (((status & STATUS_MODE_MASK) == MODE_PS_HELIX) &&
            ((status & STATUS_PENDING) == 0U)) {
            xil_printf("Router mode/status=0x%08lx\r\n", (unsigned long)status);
            return XST_SUCCESS;
        }
    }
    xil_printf("FAIL: router did not enter PS->HELIX mode; status=0x%08lx\r\n",
               (unsigned long)Xil_In32((UINTPTR)MODE_GPIO_BASEADDR + GPIO2_DATA));
    return XST_FAILURE;
}

static int reset_dma(void)
{
    u32 count;
    XAxiDma_Reset(&PsDma);
    for (count = 0U; count < RESET_LIMIT; ++count) {
        if (XAxiDma_ResetIsDone(&PsDma) != 0) return XST_SUCCESS;
    }
    return XST_FAILURE;
}

static int reset_helix_tx_path(void)
{
    XGpioPs_Config *config;
    u32 count;
    int rc;

    /* The original HELIX application configures all EMIO GPIOs as outputs and
     * pulses EMIO[0].  In this block design EMIO[0] is inverted and combined
     * with the PL reset to create stream_manager_0/tx_arst_in.  Without this
     * pulse packet_fragmentation_TX retains undefined edge-detector state. */
    config = XGpioPs_LookupConfig(XPAR_PSU_GPIO_0_DEVICE_ID);
    if (config == NULL) return XST_FAILURE;
    rc = XGpioPs_CfgInitialize(&PsGpio, config, config->BaseAddr);
    if (rc != XST_SUCCESS) return XST_FAILURE;
    XGpioPs_SetDirectionPin(&PsGpio, HARD_RESET_TX_PIN, 1U);
    XGpioPs_SetOutputEnablePin(&PsGpio, HARD_RESET_TX_PIN, 1U);
    XGpioPs_WritePin(&PsGpio, HARD_RESET_TX_PIN, 1U);
    for (count = 0U; count < 10000U; ++count) { __asm__ volatile("nop"); }
    XGpioPs_WritePin(&PsGpio, HARD_RESET_TX_PIN, 0U);
    for (count = 0U; count < 10000U; ++count) { __asm__ volatile("nop"); }
    return XST_SUCCESS;
}

static void fill_test_pattern(void)
{
    u32 i;
    for (i = 0U; i < TEST_BYTES; ++i) {
        TxBuffer[i] = (u8)((i * 29U + (i >> 3U) + 0x46U) & 0xFFU);
    }
}

static void arm_stream_manager_tx(void)
{
    u32 count;

    /* Match HELIX triggerTX(): deassert trigger, program 64-bit beat count,
     * then generate the control_o[14] rising edge used by
     * packet_fragmentation_TX to assert S00_UDP_AXIS TREADY. */
    Xil_Out32((UINTPTR)STREAM_MANAGER_BASEADDR + STREAM_CONTROL, 0U);
    for (count = 0U; count < 10000U; ++count) { __asm__ volatile("nop"); }
    Xil_Out32((UINTPTR)STREAM_MANAGER_BASEADDR + STREAM_TX_SIZE,
              TEST_BYTES / STREAM_BEAT_BYTES);
    Xil_Out32((UINTPTR)STREAM_MANAGER_BASEADDR + STREAM_CONTROL,
              STREAM_TX_START);
    for (count = 0U; count < 10000U; ++count) { __asm__ volatile("nop"); }
}

int main(void)
{
    XAxiDma_Config *config;
    XTime start;
    XTime now;
    const XTime deadline = (XTime)TIMEOUT_SECONDS * (XTime)COUNTS_PER_SECOND;
    u32 status;
    int rc;

    publish_result(RESULT_RUNNING);
    xil_printf("\r\nV46 RFSoC4x2 PS->HELIX AXI DMA smoke test\r\n");
    xil_printf("DMA device=%u base=0x%08lx mode_gpio=0x%08lx stream_manager=0x%08lx bytes=%u\r\n",
               (unsigned int)PS_DMA_DEVICE_ID, (unsigned long)PS_DMA_BASEADDR,
               (unsigned long)MODE_GPIO_BASEADDR,
               (unsigned long)STREAM_MANAGER_BASEADDR,
               (unsigned int)TEST_BYTES);

    if (set_ps_helix_mode() != XST_SUCCESS) return XST_FAILURE;
    if (reset_helix_tx_path() != XST_SUCCESS) {
        xil_printf("FAIL: HELIX TX EMIO reset initialization failed.\r\n");
        return XST_FAILURE;
    }

    config = XAxiDma_LookupConfig(PS_DMA_DEVICE_ID);
    if (config == NULL) {
        xil_printf("FAIL: XAxiDma_LookupConfig returned NULL.\r\n");
        return XST_FAILURE;
    }
    rc = XAxiDma_CfgInitialize(&PsDma, config);
    if (rc != XST_SUCCESS || XAxiDma_HasSg(&PsDma) != 0) {
        xil_printf("FAIL: DMA initialization rc=%d sg=%d.\r\n", rc,
                   XAxiDma_HasSg(&PsDma));
        return XST_FAILURE;
    }
    if (reset_dma() != XST_SUCCESS) {
        xil_printf("FAIL: DMA reset timeout.\r\n");
        return XST_FAILURE;
    }
    XAxiDma_IntrDisable(&PsDma, XAXIDMA_IRQ_ALL_MASK,
                        XAXIDMA_DMA_TO_DEVICE);

    fill_test_pattern();
    Xil_DCacheFlushRange((UINTPTR)TxBuffer, TEST_BYTES);
    arm_stream_manager_tx();
    print_dma_status("BEFORE", dma_status());
    rc = XAxiDma_SimpleTransfer(&PsDma, (UINTPTR)TxBuffer, TEST_BYTES,
                                XAXIDMA_DMA_TO_DEVICE);
    if (rc != XST_SUCCESS) {
        xil_printf("FAIL: XAxiDma_SimpleTransfer rc=%d.\r\n", rc);
        print_dma_status("START_FAIL", dma_status());
        return XST_FAILURE;
    }

    XTime_GetTime(&start);
    do {
        status = dma_status();
        if ((status & XAXIDMA_ERR_ALL_MASK) != 0U) {
            xil_printf("FAIL: DMA bus/datamover error.\r\n");
            print_dma_status("ERROR", status);
            return XST_FAILURE;
        }
        if (XAxiDma_Busy(&PsDma, XAXIDMA_DMA_TO_DEVICE) == 0U) {
            const u32 router = Xil_In32((UINTPTR)MODE_GPIO_BASEADDR + GPIO2_DATA);
            print_dma_status("AFTER", dma_status());
            xil_printf("Router final status=0x%08lx\r\n", (unsigned long)router);
            if ((router & STATUS_KEEP_ERR) != 0U) {
                xil_printf("FAIL: router detected a partial PS->HELIX beat.\r\n");
                return XST_FAILURE;
            }
            xil_printf("HELIX_QSFP_V46_PS_DMA_SMOKE_PASS\r\n");
            xil_printf("This proves PS DDR read, MM2S, router mode 1, downstream "
                       "TREADY, and TLAST acceptance.\r\n");
            publish_result(RESULT_PASS);
            return XST_SUCCESS;
        }
        XTime_GetTime(&now);
    } while ((now - start) < deadline);

    xil_printf("FAIL: PS->HELIX DMA timed out after %u seconds.\r\n",
               (unsigned int)TIMEOUT_SECONDS);
    print_dma_status("TIMEOUT", dma_status());
    xil_printf("Router status=0x%08lx\r\n",
               (unsigned long)Xil_In32((UINTPTR)MODE_GPIO_BASEADDR + GPIO2_DATA));
    publish_result(RESULT_TIMEOUT);
    return XST_FAILURE;
}
