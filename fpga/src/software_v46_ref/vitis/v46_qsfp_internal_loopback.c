#include "xparameters.h"
#include "xaxidma.h"
#include "xaxidma_hw.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xtime_l.h"
#include "sleep.h"

#include <string.h>

#if defined(XPAR_ETH_DMA_INTERNAL_AXI_ETH_DMA_INTERNAL_DEVICE_ID)
#define PS_DMA_DEVICE_ID XPAR_ETH_DMA_INTERNAL_AXI_ETH_DMA_INTERNAL_DEVICE_ID
#define PS_DMA_BASEADDR  XPAR_ETH_DMA_INTERNAL_AXI_ETH_DMA_INTERNAL_BASEADDR
#elif defined(XPAR_AXI_ETH_DMA_INTERNAL_DEVICE_ID)
#define PS_DMA_DEVICE_ID XPAR_AXI_ETH_DMA_INTERNAL_DEVICE_ID
#define PS_DMA_BASEADDR  XPAR_AXI_ETH_DMA_INTERNAL_BASEADDR
#else
#error "V46 eth_dma_internal AXI DMA was not found in xparameters.h"
#endif

#define CMAC_BASE                0xB0100000U
#define NL_BASE                  0xB0110000U
#define ROUTER_BASE              0xB0120000U
#define STREAM_MANAGER_BASE      0xB000A000U

/* Vivado 2024.1 CMAC v3.1 generated AXI4-Lite register header. */
#define CMAC_CONFIG_TX           0x000CU
#define CMAC_CONFIG_RX           0x0014U
#define CMAC_CORE_MODE           0x0020U
#define CMAC_CORE_VERSION        0x0024U
#define CMAC_GT_LOOPBACK         0x0090U
#define CMAC_STAT_TX_STATUS      0x0200U
#define CMAC_STAT_RX_STATUS      0x0204U
#define CMAC_TICK                0x02B0U
#define CMAC_TX_PACKETS          0x0500U
#define CMAC_TX_GOOD_PACKETS     0x0508U
#define CMAC_TX_BYTES            0x0510U
#define CMAC_RX_PACKETS          0x0608U
#define CMAC_RX_GOOD_PACKETS     0x0610U
#define CMAC_RX_BYTES            0x0618U
#define CMAC_RSFEC_STATUS        0x1004U
#define CMAC_RSFEC_CORRECTED     0x1008U
#define CMAC_RSFEC_UNCORRECTED   0x1010U
#define CMAC_RSFEC_CONFIG        0x1000U
#define CMAC_RSFEC_ENABLE        0x107CU

#define CMAC_ENABLE              0x00000001U
#define CMAC_RX_LINK_MASK        0x00000003U
#define CMAC_RX_ERROR_MASK       0x000000FCU
#define CMAC_RSFEC_LOCK_MASK     0x00004F00U
#define CMAC_RSFEC_HI_SER_MASK   0x0000000CU

#define NL_MAC_LO                0x0010U
#define NL_MAC_HI                0x0014U
#define NL_IP_ADDRESS            0x0018U
#define NL_GATEWAY               0x001CU
#define NL_IP_MASK               0x0020U
#define NL_ETH_IN_PACKETS        0x0410U
#define NL_ETH_OUT_PACKETS       0x04B8U
#define NL_APP_OUT_PACKETS       0x04E8U
#define NL_UDP_OUT_PACKETS       0x0500U
#define NL_APP_IN_PACKETS        0x0518U
#define NL_DEBUG_RESET           0x05F0U
#define NL_SOCKET_THEIR_IP       0x0810U
#define NL_SOCKET_THEIR_PORT     0x0890U
#define NL_SOCKET_MY_PORT        0x0910U
#define NL_SOCKET_VALID          0x0990U
#define NL_SOCKET_COUNT          0x0A10U
#define NL_ARP_VALID             0x1100U
#define NL_ARP_IP                0x1400U
#define NL_ARP_MAC               0x1800U
#define NL_SOCKET_STRIDE         8U

#define FPGA_IP                  0xC0A80A02U
#define FPGA_MAC_LO              0x00001002U
#define FPGA_MAC_HI              0x00000200U
#define NETWORK_MASK             0xFFFFFF00U
#define CONTROL_PORT             10000U
#define DATA_PORT                10001U
#define CONTROL_SOCKET           1U
#define DATA_SOCKET              0U

/* ARP RAM uses byte-reversed IP/MAC words, matching NetworkLayer.py. */
#define SELF_ARP_INDEX           2U
#define SELF_ARP_IP_WORD         0x020AA8C0U
#define SELF_ARP_MAC_LSB         0x00000002U
#define SELF_ARP_MAC_MSB         0x00000210U
#define SELF_ARP_VALID_BIT       0x00010000U

#define GPIO_DATA                0x0000U
#define GPIO2_DATA               0x0008U
#define GPIO_TRI                 0x0004U
#define GPIO2_TRI                0x000CU
#define ROUTER_MODE_PRODUCTION   0U
#define ROUTER_MODE_PS_QSFP      2U
#define ROUTER_CLEAR_STATUS      (1U << 2U)

#define TEST_CAPACITY            16384U
#define GUARD_BYTES              64U
#define LINK_TIMEOUT_SECONDS     15U
#define DMA_TIMEOUT_SECONDS      5U
#define RESULT_ADDRESS           ((UINTPTR)0x0FF02000U)
#define RESULT_RUNNING           0x56345101U
#define RESULT_PASS              0x563451A5U
#define RESULT_CMAC_FAIL         0x563451E1U
#define RESULT_DMA_FAIL          0x563451E2U
#define RESULT_COMPARE_FAIL      0x563451E3U
#define RESULT_COUNTER_FAIL      0x563451E4U
#define RESULT_MODE_FAIL         0x563451E5U
#define RESULT_NL_FAIL           0x563451E6U

static XAxiDma Dma;
static u8 TxBuffer[TEST_CAPACITY] __attribute__((aligned(64)));
static u8 RxBuffer[TEST_CAPACITY] __attribute__((aligned(64)));

static void publish_result(u32 value)
{
    Xil_Out32(RESULT_ADDRESS, value);
    Xil_DCacheFlushRange(RESULT_ADDRESS, sizeof(value));
}

static void fail_and_hold(u32 result, const char *marker)
{
    xil_printf("%s result=0x%08lx\r\n", marker, (unsigned long)result);
    publish_result(result);
    /* Keep the APU/debug AXI awake so XSCT can always collect the failure
     * signature and CMAC registers.  WFE may let platform firmware clock-gate
     * the APU before the post-run DAP read completes. */
    while (1) { __asm__ volatile("nop"); }
}

static u32 cmac_read(u32 offset)
{
    return Xil_In32((UINTPTR)CMAC_BASE + offset);
}

static void cmac_write(u32 offset, u32 value)
{
    Xil_Out32((UINTPTR)CMAC_BASE + offset, value);
}

static u32 nl_read(u32 offset)
{
    return Xil_In32((UINTPTR)NL_BASE + offset);
}

static void nl_write(u32 offset, u32 value)
{
    Xil_Out32((UINTPTR)NL_BASE + offset, value);
}

static u64 read_counter48(UINTPTR base, u32 lsb_offset)
{
    u32 high_before;
    u32 high_after;
    u32 low;
    do {
        high_before = Xil_In32(base + lsb_offset + 4U) & 0xFFFFU;
        low = Xil_In32(base + lsb_offset);
        high_after = Xil_In32(base + lsb_offset + 4U) & 0xFFFFU;
    } while (high_before != high_after);
    return ((u64)high_after << 32U) | (u64)low;
}

static u64 read_counter64(UINTPTR base, u32 lsb_offset)
{
    u32 high_before;
    u32 high_after;
    u32 low;
    do {
        high_before = Xil_In32(base + lsb_offset + 4U);
        low = Xil_In32(base + lsb_offset);
        high_after = Xil_In32(base + lsb_offset + 4U);
    } while (high_before != high_after);
    return ((u64)high_after << 32U) | (u64)low;
}

static void print_counter(const char *name, u64 value)
{
    xil_printf("%s=0x%08lx%08lx\r\n", name,
               (unsigned long)(u32)(value >> 32U),
               (unsigned long)(u32)value);
}

static int wait_cmac_loopback_link(void)
{
    XTime start;
    XTime now;
    const XTime deadline =
        (XTime)LINK_TIMEOUT_SECONDS * (XTime)COUNTS_PER_SECOND;
    u32 rx_status = 0U;
    u32 tx_status = 0U;
    u32 rsfec_status = 0U;

    cmac_write(CMAC_CONFIG_TX, cmac_read(CMAC_CONFIG_TX) & ~CMAC_ENABLE);
    cmac_write(CMAC_CONFIG_RX, cmac_read(CMAC_CONFIG_RX) & ~CMAC_ENABLE);
    cmac_write(CMAC_RSFEC_CONFIG, 3U);
    cmac_write(CMAC_RSFEC_ENABLE, 3U);
    cmac_write(CMAC_GT_LOOPBACK, 1U);
    usleep(10000U);
    cmac_write(CMAC_CONFIG_RX, cmac_read(CMAC_CONFIG_RX) | CMAC_ENABLE);
    cmac_write(CMAC_CONFIG_TX, cmac_read(CMAC_CONFIG_TX) | CMAC_ENABLE);

    (void)cmac_read(CMAC_STAT_TX_STATUS);
    (void)cmac_read(CMAC_STAT_RX_STATUS);
    XTime_GetTime(&start);
    do {
        tx_status = cmac_read(CMAC_STAT_TX_STATUS);
        rx_status = cmac_read(CMAC_STAT_RX_STATUS);
        rsfec_status = cmac_read(CMAC_RSFEC_STATUS);
        if ((rx_status & CMAC_RX_LINK_MASK) == CMAC_RX_LINK_MASK &&
            (rx_status & CMAC_RX_ERROR_MASK) == 0U &&
            (tx_status & 1U) == 0U &&
            (rsfec_status & CMAC_RSFEC_LOCK_MASK) == CMAC_RSFEC_LOCK_MASK &&
            (rsfec_status & CMAC_RSFEC_HI_SER_MASK) == 0U) {
            xil_printf("CMAC_LOOPBACK_LINK_PASS tx=0x%08lx rx=0x%08lx rsfec=0x%08lx\r\n",
                       (unsigned long)tx_status, (unsigned long)rx_status,
                       (unsigned long)rsfec_status);
            return XST_SUCCESS;
        }
        usleep(1000U);
        XTime_GetTime(&now);
    } while ((now - start) < deadline);

    xil_printf("CMAC_LOOPBACK_LINK_TIMEOUT tx=0x%08lx rx=0x%08lx rsfec=0x%08lx\r\n",
               (unsigned long)tx_status, (unsigned long)rx_status,
               (unsigned long)rsfec_status);
    return XST_FAILURE;
}

static void configure_socket(u32 index, u32 port)
{
    const u32 stride = index * NL_SOCKET_STRIDE;
    nl_write(NL_SOCKET_THEIR_IP + stride, FPGA_IP);
    nl_write(NL_SOCKET_THEIR_PORT + stride, port);
    nl_write(NL_SOCKET_MY_PORT + stride, port);
    nl_write(NL_SOCKET_VALID + stride, 1U);
}

static int configure_self_network(void)
{
    u32 socket_count;
    u32 index;
    u32 valid_word;

    nl_write(NL_MAC_LO, FPGA_MAC_LO);
    nl_write(NL_MAC_HI, FPGA_MAC_HI);
    nl_write(NL_IP_ADDRESS, FPGA_IP);
    nl_write(NL_GATEWAY, FPGA_IP);
    nl_write(NL_IP_MASK, NETWORK_MASK);

    socket_count = nl_read(NL_SOCKET_COUNT);
    if (socket_count < 3U || socket_count > 16U) {
        xil_printf("NETWORKLAYER_SOCKET_COUNT_FAIL count=%lu\r\n",
                   (unsigned long)socket_count);
        return XST_FAILURE;
    }
    for (index = 0U; index < socket_count; ++index) {
        const u32 stride = index * NL_SOCKET_STRIDE;
        nl_write(NL_SOCKET_THEIR_IP + stride, 0U);
        nl_write(NL_SOCKET_THEIR_PORT + stride, 0U);
        nl_write(NL_SOCKET_MY_PORT + stride, 0U);
        nl_write(NL_SOCKET_VALID + stride, 0U);
    }
    configure_socket(DATA_SOCKET, DATA_PORT);
    configure_socket(CONTROL_SOCKET, CONTROL_PORT);

    nl_write(NL_ARP_IP + SELF_ARP_INDEX * 4U, SELF_ARP_IP_WORD);
    nl_write(NL_ARP_MAC + SELF_ARP_INDEX * 8U, SELF_ARP_MAC_LSB);
    nl_write(NL_ARP_MAC + SELF_ARP_INDEX * 8U + 4U, SELF_ARP_MAC_MSB);
    valid_word = nl_read(NL_ARP_VALID);
    nl_write(NL_ARP_VALID, valid_word | SELF_ARP_VALID_BIT);

    nl_write(NL_DEBUG_RESET, 1U);
    usleep(1000U);
    nl_write(NL_DEBUG_RESET, 0U);

    if (nl_read(NL_IP_ADDRESS) != FPGA_IP ||
        nl_read(NL_SOCKET_THEIR_IP + CONTROL_SOCKET * NL_SOCKET_STRIDE) != FPGA_IP ||
        nl_read(NL_SOCKET_MY_PORT + CONTROL_SOCKET * NL_SOCKET_STRIDE) != CONTROL_PORT ||
        nl_read(NL_SOCKET_VALID + CONTROL_SOCKET * NL_SOCKET_STRIDE) != 1U ||
        (nl_read(NL_ARP_VALID) & SELF_ARP_VALID_BIT) == 0U) {
        xil_printf("NETWORKLAYER_SELF_CONFIG_READBACK_FAIL\r\n");
        return XST_FAILURE;
    }
    xil_printf("NETWORKLAYER_SELF_CONFIG_PASS sockets=%lu ip=0x%08lx\r\n",
               (unsigned long)socket_count,
               (unsigned long)nl_read(NL_IP_ADDRESS));
    return XST_SUCCESS;
}

static int set_router_mode(u32 mode)
{
    u32 count;
    u32 status;
    Xil_Out32((UINTPTR)ROUTER_BASE + GPIO_TRI, 0U);
    Xil_Out32((UINTPTR)ROUTER_BASE + GPIO2_TRI, 0xFFFFFFFFU);
    Xil_Out32((UINTPTR)ROUTER_BASE + GPIO_DATA,
              mode | ROUTER_CLEAR_STATUS);
    usleep(1000U);
    Xil_Out32((UINTPTR)ROUTER_BASE + GPIO_DATA, mode);
    for (count = 0U; count < 1000000U; ++count) {
        status = Xil_In32((UINTPTR)ROUTER_BASE + GPIO2_DATA);
        if ((status & 0x7U) == mode) {
            xil_printf("ROUTER_MODE_PASS requested=%lu status=0x%08lx\r\n",
                       (unsigned long)mode, (unsigned long)status);
            return XST_SUCCESS;
        }
    }
    xil_printf("ROUTER_MODE_TIMEOUT requested=%lu status=0x%08lx\r\n",
               (unsigned long)mode, (unsigned long)status);
    return XST_FAILURE;
}

static int init_dma(void)
{
    XAxiDma_Config *config = XAxiDma_LookupConfig(PS_DMA_DEVICE_ID);
    u32 count;
    if (config == NULL || XAxiDma_CfgInitialize(&Dma, config) != XST_SUCCESS ||
        XAxiDma_HasSg(&Dma)) {
        return XST_FAILURE;
    }
    XAxiDma_Reset(&Dma);
    for (count = 0U; count < 1000000U; ++count) {
        if (XAxiDma_ResetIsDone(&Dma)) {
            XAxiDma_IntrDisable(&Dma, XAXIDMA_IRQ_ALL_MASK,
                                XAXIDMA_DMA_TO_DEVICE);
            XAxiDma_IntrDisable(&Dma, XAXIDMA_IRQ_ALL_MASK,
                                XAXIDMA_DEVICE_TO_DMA);
            return XST_SUCCESS;
        }
    }
    return XST_FAILURE;
}

static int wait_dma(int wait_rx)
{
    XTime start;
    XTime now;
    const XTime deadline =
        (XTime)DMA_TIMEOUT_SECONDS * (XTime)COUNTS_PER_SECOND;
    XTime_GetTime(&start);
    do {
        const int tx_busy = XAxiDma_Busy(&Dma, XAXIDMA_DMA_TO_DEVICE);
        const int rx_busy = wait_rx ?
            XAxiDma_Busy(&Dma, XAXIDMA_DEVICE_TO_DMA) : 0;
        if (!tx_busy && !rx_busy) {
            const u32 tx_status = Xil_In32((UINTPTR)PS_DMA_BASEADDR +
                                           XAXIDMA_TX_OFFSET + XAXIDMA_SR_OFFSET);
            const u32 rx_status = Xil_In32((UINTPTR)PS_DMA_BASEADDR +
                                           XAXIDMA_RX_OFFSET + XAXIDMA_SR_OFFSET);
            const u32 error_mask = XAXIDMA_ERR_INTERNAL_MASK |
                                   XAXIDMA_ERR_SLAVE_MASK |
                                   XAXIDMA_ERR_DECODE_MASK;
            if ((tx_status & error_mask) != 0U ||
                (wait_rx && (rx_status & error_mask) != 0U)) {
                xil_printf("DMA_STATUS_ERROR tx=0x%08lx rx=0x%08lx\r\n",
                           (unsigned long)tx_status,
                           (unsigned long)rx_status);
                return XST_FAILURE;
            }
            return XST_SUCCESS;
        }
        XTime_GetTime(&now);
    } while ((now - start) < deadline);
    xil_printf("DMA_TIMEOUT tx=0x%08lx rx=0x%08lx\r\n",
               (unsigned long)Xil_In32((UINTPTR)PS_DMA_BASEADDR +
                                       XAXIDMA_TX_OFFSET + XAXIDMA_SR_OFFSET),
               (unsigned long)Xil_In32((UINTPTR)PS_DMA_BASEADDR +
                                       XAXIDMA_RX_OFFSET + XAXIDMA_SR_OFFSET));
    return XST_FAILURE;
}

static int exact_control_loopback(u32 length, u32 seed)
{
    u32 index;
    for (index = 0U; index < length; ++index) {
        TxBuffer[index] = (u8)((seed + index * 13U) & 0xFFU);
    }
    memset(RxBuffer, 0xA5, sizeof(RxBuffer));
    Xil_DCacheFlushRange((UINTPTR)TxBuffer, length);
    Xil_DCacheFlushRange((UINTPTR)RxBuffer, sizeof(RxBuffer));

    if (XAxiDma_SimpleTransfer(&Dma, (UINTPTR)RxBuffer, TEST_CAPACITY,
                               XAXIDMA_DEVICE_TO_DMA) != XST_SUCCESS ||
        XAxiDma_SimpleTransfer(&Dma, (UINTPTR)TxBuffer, length,
                               XAXIDMA_DMA_TO_DEVICE) != XST_SUCCESS ||
        wait_dma(1) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    Xil_DCacheInvalidateRange((UINTPTR)RxBuffer, sizeof(RxBuffer));
    for (index = 0U; index < length; ++index) {
        if (RxBuffer[index] != TxBuffer[index]) {
            xil_printf("LOOPBACK_COMPARE_FAIL length=%lu index=%lu tx=0x%02x rx=0x%02x\r\n",
                       (unsigned long)length, (unsigned long)index,
                       TxBuffer[index], RxBuffer[index]);
            return XST_FAILURE;
        }
    }
    for (index = length; index < length + GUARD_BYTES; ++index) {
        if (RxBuffer[index] != 0xA5U) {
            xil_printf("LOOPBACK_GUARD_FAIL length=%lu index=%lu value=0x%02x\r\n",
                       (unsigned long)length, (unsigned long)index,
                       RxBuffer[index]);
            return XST_FAILURE;
        }
    }
    xil_printf("CONTROL_LOOPBACK_PACKET_PASS length=%lu\r\n",
               (unsigned long)length);
    return XST_SUCCESS;
}

static int send_data_to_helix(u32 length)
{
    u32 index;
    for (index = 0U; index < length; ++index) {
        TxBuffer[index] = (u8)((0x5AU + index * 7U) & 0xFFU);
    }
    Xil_DCacheFlushRange((UINTPTR)TxBuffer, length);
    if (XAxiDma_SimpleTransfer(&Dma, (UINTPTR)TxBuffer, length,
                               XAXIDMA_DMA_TO_DEVICE) != XST_SUCCESS ||
        wait_dma(0) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}

int main(void)
{
    /* Include short PS control payloads to prove that the post-NetworkLayer
     * Ethernet padder produces a legal 60-byte pre-FCS frame without changing
     * the UDP payload length observed after receive parsing. */
    static const u32 lengths[] = {
        1U, 4U, 7U, 8U, 9U, 17U, 18U, 19U,
        63U, 64U, 65U, 1024U, 9000U
    };
    const u32 packet_count = (u32)(sizeof(lengths) / sizeof(lengths[0]));
    u32 index;
    u64 cmac_tx_before;
    u64 cmac_rx_before;
    u64 cmac_tx_after;
    u64 cmac_rx_after;
    u64 cmac_tx_good;
    u64 cmac_rx_good;
    u64 nl_app_out_before;
    u64 nl_app_in_before;
    u64 nl_app_out_after;
    u64 nl_app_in_after;
    u32 router_status;

    publish_result(RESULT_RUNNING);
    xil_printf("\r\nV46 QSFP100G CMAC/GT/UDP internal loopback test\r\n");
    xil_printf("CMAC base=0x%08lx mode=0x%08lx version=0x%08lx DMA=0x%08lx\r\n",
               (unsigned long)CMAC_BASE,
               (unsigned long)cmac_read(CMAC_CORE_MODE),
               (unsigned long)cmac_read(CMAC_CORE_VERSION),
               (unsigned long)PS_DMA_BASEADDR);

    if (wait_cmac_loopback_link() != XST_SUCCESS) {
        fail_and_hold(RESULT_CMAC_FAIL, "V46_QSFP_INTERNAL_LOOPBACK_CMAC_FAIL");
    }
    if (configure_self_network() != XST_SUCCESS) {
        fail_and_hold(RESULT_NL_FAIL, "V46_QSFP_INTERNAL_LOOPBACK_NL_FAIL");
    }
    if (init_dma() != XST_SUCCESS) {
        fail_and_hold(RESULT_DMA_FAIL, "V46_QSFP_INTERNAL_LOOPBACK_DMA_INIT_FAIL");
    }
    if (set_router_mode(ROUTER_MODE_PRODUCTION) != XST_SUCCESS) {
        fail_and_hold(RESULT_MODE_FAIL, "V46_QSFP_INTERNAL_LOOPBACK_MODE0_FAIL");
    }

    cmac_write(CMAC_TICK, 1U);
    usleep(1000U);
    cmac_tx_before = read_counter48(CMAC_BASE, CMAC_TX_PACKETS);
    cmac_rx_before = read_counter48(CMAC_BASE, CMAC_RX_PACKETS);
    nl_app_out_before = read_counter64(NL_BASE, NL_APP_OUT_PACKETS);
    nl_app_in_before = read_counter64(NL_BASE, NL_APP_IN_PACKETS);

    for (index = 0U; index < packet_count; ++index) {
        if (exact_control_loopback(lengths[index], 0x31U + index) != XST_SUCCESS) {
            fail_and_hold(RESULT_COMPARE_FAIL,
                          "V46_QSFP_INTERNAL_LOOPBACK_COMPARE_FAIL");
        }
    }

    if (set_router_mode(ROUTER_MODE_PS_QSFP) != XST_SUCCESS ||
        send_data_to_helix(4096U) != XST_SUCCESS) {
        fail_and_hold(RESULT_DMA_FAIL, "V46_QSFP_INTERNAL_LOOPBACK_MODE2_FAIL");
    }
    usleep(100000U);
    router_status = Xil_In32((UINTPTR)ROUTER_BASE + GPIO2_DATA);
    if ((router_status & 0x1FU) != ROUTER_MODE_PS_QSFP) {
        xil_printf("MODE2_ROUTER_STATUS_FAIL status=0x%08lx\r\n",
                   (unsigned long)router_status);
        fail_and_hold(RESULT_MODE_FAIL, "V46_QSFP_INTERNAL_LOOPBACK_MODE2_STATUS_FAIL");
    }

    cmac_write(CMAC_TICK, 1U);
    usleep(1000U);
    cmac_tx_after = read_counter48(CMAC_BASE, CMAC_TX_PACKETS);
    cmac_rx_after = read_counter48(CMAC_BASE, CMAC_RX_PACKETS);
    cmac_tx_good = read_counter48(CMAC_BASE, CMAC_TX_GOOD_PACKETS);
    cmac_rx_good = read_counter48(CMAC_BASE, CMAC_RX_GOOD_PACKETS);
    nl_app_out_after = read_counter64(NL_BASE, NL_APP_OUT_PACKETS);
    nl_app_in_after = read_counter64(NL_BASE, NL_APP_IN_PACKETS);

    print_counter("CMAC_TX_PACKETS_BEFORE", cmac_tx_before);
    print_counter("CMAC_TX_PACKETS_AFTER", cmac_tx_after);
    print_counter("CMAC_RX_PACKETS_BEFORE", cmac_rx_before);
    print_counter("CMAC_RX_PACKETS_AFTER", cmac_rx_after);
    print_counter("CMAC_TX_GOOD_PACKETS", cmac_tx_good);
    print_counter("CMAC_RX_GOOD_PACKETS", cmac_rx_good);
    print_counter("NL_APP_OUT_BEFORE", nl_app_out_before);
    print_counter("NL_APP_OUT_AFTER", nl_app_out_after);
    print_counter("NL_APP_IN_BEFORE", nl_app_in_before);
    print_counter("NL_APP_IN_AFTER", nl_app_in_after);
    print_counter("NL_ETH_OUT_PACKETS",
                  read_counter64(NL_BASE, NL_ETH_OUT_PACKETS));
    print_counter("NL_ETH_IN_PACKETS",
                  read_counter64(NL_BASE, NL_ETH_IN_PACKETS));
    print_counter("NL_UDP_OUT_PACKETS",
                  read_counter64(NL_BASE, NL_UDP_OUT_PACKETS));
    print_counter("CMAC_TX_BYTES", read_counter48(CMAC_BASE, CMAC_TX_BYTES));
    print_counter("CMAC_RX_BYTES", read_counter48(CMAC_BASE, CMAC_RX_BYTES));
    xil_printf("CMAC_FINAL tx=0x%08lx rx=0x%08lx rsfec=0x%08lx corrected=0x%08lx uncorrected=0x%08lx\r\n",
               (unsigned long)cmac_read(CMAC_STAT_TX_STATUS),
               (unsigned long)cmac_read(CMAC_STAT_RX_STATUS),
               (unsigned long)cmac_read(CMAC_RSFEC_STATUS),
               (unsigned long)cmac_read(CMAC_RSFEC_CORRECTED),
               (unsigned long)cmac_read(CMAC_RSFEC_UNCORRECTED));
    xil_printf("ROUTER_FINAL status=0x%08lx stream_manager=0x%08lx\r\n",
               (unsigned long)router_status,
               (unsigned long)Xil_In32((UINTPTR)STREAM_MANAGER_BASE));

    if ((cmac_tx_after - cmac_tx_before) < (u64)(packet_count + 1U) ||
        (cmac_rx_after - cmac_rx_before) < (u64)(packet_count + 1U) ||
        cmac_tx_good < (u64)(packet_count + 1U) ||
        cmac_rx_good < (u64)(packet_count + 1U) ||
        (nl_app_out_after - nl_app_out_before) < (u64)(packet_count + 1U) ||
        (nl_app_in_after - nl_app_in_before) < (u64)(packet_count + 1U) ||
        (cmac_read(CMAC_RSFEC_STATUS) & CMAC_RSFEC_HI_SER_MASK) != 0U ||
        cmac_read(CMAC_RSFEC_UNCORRECTED) != 0U) {
        fail_and_hold(RESULT_COUNTER_FAIL,
                      "V46_QSFP_INTERNAL_LOOPBACK_COUNTER_FAIL");
    }

    publish_result(RESULT_PASS);
    xil_printf("HELIX_QSFP_V46_INTERNAL_LOOPBACK_PASS\r\n");
    xil_printf("Proven: CMAC enabled, CAUI-4 GT/RS-FEC internal loopback, UDP control byte compare, and PS-QSFP data return to HELIX.\r\n");
    while (1) { __asm__ volatile("nop"); }
}
