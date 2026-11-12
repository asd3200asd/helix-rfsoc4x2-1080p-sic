#include "udp_driver.h"

#include "udp_dma_driver.h"
#include "cmd_parser.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_io.h"
#include "xparameters.h"
#include "xstatus.h"

#include <stdio.h>
#include <string.h>

/*
 * V46 replaces HELIX's private 10GbE UDP stack with the NetworkLayer from
 * strath-sdr/rfsoc_qsfp_offload.  These offsets are the packaged
 * NetworkLayer 1.0 AXI-Lite contract (also recorded in network_layer.json).
 */
#define QSFP_NL_BASE                 0xB0110000U
#define QSFP_MODE_GPIO_BASE          0xB0120000U
#define QSFP_CMAC_BASE               0xB0100000U

/* Exact offsets from the generated Vivado 2024.1 CMAC v3.1 register header. */
#define CMAC_CONFIGURATION_TX_REG1   0x000CU
#define CMAC_CONFIGURATION_RX_REG1   0x0014U
#define CMAC_CORE_MODE               0x0020U
#define CMAC_CORE_VERSION            0x0024U
#define CMAC_STAT_TX_STATUS          0x0200U
#define CMAC_STAT_RX_STATUS          0x0204U
#define CMAC_RSFEC_CONFIG            0x1000U
#define CMAC_RSFEC_ENABLE            0x107CU
#define CMAC_ENABLE                  0x00000001U
#define CMAC_RSFEC_CORRECT_INDICATE  0x00000003U
#define CMAC_RSFEC_TX_RX_ENABLE      0x00000003U

#define NL_MAC_LO                    0x0010U
#define NL_MAC_HI                    0x0014U
#define NL_IP_ADDRESS                0x0018U
#define NL_GATEWAY                   0x001CU
#define NL_IP_MASK                   0x0020U
#define NL_SOCKET_THEIR_IP           0x0810U
#define NL_SOCKET_THEIR_PORT         0x0890U
#define NL_SOCKET_MY_PORT            0x0910U
#define NL_SOCKET_VALID              0x0990U
#define NL_SOCKET_COUNT              0x0A10U
#define NL_ARP_DISCOVERY             0x1010U
#define NL_SOCKET_STRIDE             8U
#define NL_EXPECTED_SOCKETS          16U

#define GPIO_DATA                    0x0000U
#define GPIO2_DATA                   0x0008U
#define GPIO_TRI                     0x0004U
#define GPIO2_TRI                    0x000CU
#define ROUTER_MODE_PRODUCTION       0U
#define ROUTER_CLEAR_STATUS          (1U << 2U)

#define FPGA_IP                      0xC0A80A02U /* 192.168.10.2 */
#define HOST_IP                      0xC0A80A01U /* 192.168.10.1 */
#define GATEWAY_IP                   0xC0A80A01U
#define NETWORK_MASK                0xFFFFFF00U
#define FPGA_MAC_LO                  0x00001002U
#define FPGA_MAC_HI                  0x00000200U /* 02:00:00:00:10:02 */

#define UDP_DATA_PORT                10001U
#define UDP_CONTROL_PORT             10000U
#define UDP_METADATA_PORT            10002U

static u32 tx_buf[16] __attribute__((aligned(64)));
static u32 recv_buf[16] __attribute__((aligned(64)));

static int enable_cmac(void)
{
    u32 tx_config;
    u32 rx_config;
    u32 tx_status;
    u32 rx_status;

    tx_config = Xil_In32((UINTPTR)QSFP_CMAC_BASE +
                         CMAC_CONFIGURATION_TX_REG1);
    rx_config = Xil_In32((UINTPTR)QSFP_CMAC_BASE +
                         CMAC_CONFIGURATION_RX_REG1);
    Xil_Out32((UINTPTR)QSFP_CMAC_BASE + CMAC_RSFEC_CONFIG,
              CMAC_RSFEC_CORRECT_INDICATE);
    Xil_Out32((UINTPTR)QSFP_CMAC_BASE + CMAC_RSFEC_ENABLE,
              CMAC_RSFEC_TX_RX_ENABLE);
    Xil_Out32((UINTPTR)QSFP_CMAC_BASE + CMAC_CONFIGURATION_TX_REG1,
              tx_config | CMAC_ENABLE);
    Xil_Out32((UINTPTR)QSFP_CMAC_BASE + CMAC_CONFIGURATION_RX_REG1,
              rx_config | CMAC_ENABLE);

    tx_config = Xil_In32((UINTPTR)QSFP_CMAC_BASE +
                         CMAC_CONFIGURATION_TX_REG1);
    rx_config = Xil_In32((UINTPTR)QSFP_CMAC_BASE +
                         CMAC_CONFIGURATION_RX_REG1);
    if ((tx_config & CMAC_ENABLE) == 0U ||
        (rx_config & CMAC_ENABLE) == 0U ||
        (Xil_In32((UINTPTR)QSFP_CMAC_BASE + CMAC_RSFEC_ENABLE) &
         CMAC_RSFEC_TX_RX_ENABLE) != CMAC_RSFEC_TX_RX_ENABLE) {
        printf("ERROR: CMAC TX/RX/RS-FEC enable readback failed: tx=0x%08lx rx=0x%08lx fec=0x%08lx\r\n",
               (unsigned long)tx_config, (unsigned long)rx_config,
               (unsigned long)Xil_In32((UINTPTR)QSFP_CMAC_BASE +
                                        CMAC_RSFEC_ENABLE));
        return XST_FAILURE;
    }

    /* Read twice because CMAC status contains clear-on-read sticky fields. */
    (void)Xil_In32((UINTPTR)QSFP_CMAC_BASE + CMAC_STAT_TX_STATUS);
    (void)Xil_In32((UINTPTR)QSFP_CMAC_BASE + CMAC_STAT_RX_STATUS);
    tx_status = Xil_In32((UINTPTR)QSFP_CMAC_BASE + CMAC_STAT_TX_STATUS);
    rx_status = Xil_In32((UINTPTR)QSFP_CMAC_BASE + CMAC_STAT_RX_STATUS);
    printf("QSFP CMAC enabled: mode=0x%08lx version=0x%08lx tx_status=0x%08lx rx_status=0x%08lx\r\n",
           (unsigned long)Xil_In32((UINTPTR)QSFP_CMAC_BASE + CMAC_CORE_MODE),
           (unsigned long)Xil_In32((UINTPTR)QSFP_CMAC_BASE + CMAC_CORE_VERSION),
           (unsigned long)tx_status, (unsigned long)rx_status);
    return XST_SUCCESS;
}

static void nl_write(u32 offset, u32 value)
{
    Xil_Out32((UINTPTR)QSFP_NL_BASE + offset, value);
}

static u32 nl_read(u32 offset)
{
    return Xil_In32((UINTPTR)QSFP_NL_BASE + offset);
}

static void configure_socket(u32 index, u32 port, int valid)
{
    const u32 stride = index * NL_SOCKET_STRIDE;
    nl_write(NL_SOCKET_THEIR_IP + stride, valid ? HOST_IP : 0U);
    nl_write(NL_SOCKET_THEIR_PORT + stride, valid ? port : 0U);
    nl_write(NL_SOCKET_MY_PORT + stride, valid ? port : 0U);
    nl_write(NL_SOCKET_VALID + stride, valid ? 1U : 0U);
}

static int configure_networklayer(void)
{
    u32 i;
    u32 socket_count;

    nl_write(NL_MAC_LO, FPGA_MAC_LO);
    nl_write(NL_MAC_HI, FPGA_MAC_HI);
    nl_write(NL_IP_ADDRESS, FPGA_IP);
    nl_write(NL_GATEWAY, GATEWAY_IP);
    nl_write(NL_IP_MASK, NETWORK_MASK);

    socket_count = nl_read(NL_SOCKET_COUNT);
    if (socket_count < 3U || socket_count > NL_EXPECTED_SOCKETS) {
        printf("ERROR: NetworkLayer socket count=%lu, expected 3..16\r\n",
               (unsigned long)socket_count);
        return XST_FAILURE;
    }

    for (i = 0U; i < socket_count; ++i) {
        configure_socket(i, 0U, 0);
    }
    configure_socket(0U, UDP_DATA_PORT, 1);
    configure_socket(1U, UDP_CONTROL_PORT, 1);
    configure_socket(2U, UDP_METADATA_PORT, 1);

    /* Mode 0 keeps PS MM2S replies on the control socket. */
    Xil_Out32((UINTPTR)QSFP_MODE_GPIO_BASE + GPIO_TRI, 0U);
    Xil_Out32((UINTPTR)QSFP_MODE_GPIO_BASE + GPIO2_TRI, 0xFFFFFFFFU);
    Xil_Out32((UINTPTR)QSFP_MODE_GPIO_BASE + GPIO_DATA,
              ROUTER_MODE_PRODUCTION | ROUTER_CLEAR_STATUS);
    for (i = 0U; i < 10000U; ++i) {
        __asm__ volatile("nop");
    }
    Xil_Out32((UINTPTR)QSFP_MODE_GPIO_BASE + GPIO_DATA,
              ROUTER_MODE_PRODUCTION);

    /* Rising edge starts hardware ARP discovery. */
    nl_write(NL_ARP_DISCOVERY, 0U);
    nl_write(NL_ARP_DISCOVERY, 1U);
    nl_write(NL_ARP_DISCOVERY, 0U);

    if (nl_read(NL_IP_ADDRESS) != FPGA_IP ||
        nl_read(NL_SOCKET_MY_PORT) != UDP_DATA_PORT ||
        nl_read(NL_SOCKET_MY_PORT + NL_SOCKET_STRIDE) != UDP_CONTROL_PORT ||
        nl_read(NL_SOCKET_MY_PORT + 2U * NL_SOCKET_STRIDE) != UDP_METADATA_PORT) {
        printf("ERROR: NetworkLayer configuration readback failed\r\n");
        return XST_FAILURE;
    }

    printf("QSFP NetworkLayer: FPGA 192.168.10.2, host 192.168.10.1\r\n");
    printf("HELIX UDP sockets: data=%u control=%u metadata=%u\r\n",
           UDP_DATA_PORT, UDP_CONTROL_PORT, UDP_METADATA_PORT);
    printf("Router status=0x%08lx\r\n",
           (unsigned long)Xil_In32((UINTPTR)QSFP_MODE_GPIO_BASE + GPIO2_DATA));
    return XST_SUCCESS;
}

int recvUDP(u8 *RxBufferPtr, u32 len)
{
    return recv(RxBufferPtr, (u16)len);
}

int sendUDPcmd(u32 value)
{
    int status;

    memcpy(tx_buf, &value, sizeof(value));
    status = send(tx_buf, sizeof(value));
    if (status != XST_SUCCESS) {
        printf("QSFP control packet transmission failed\r\n");
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}

static void process_request(void *unused)
{
    int status;
    (void)unused;

    while (1) {
        memset(recv_buf, 0, sizeof(recv_buf));
        if (recv((u8 *)recv_buf, sizeof(recv_buf)) == XST_SUCCESS) {
            status = cmdParse((char *)recv_buf);
            if (status != 0) {
                printf("Command ERROR: %d\r\n", status);
            }
        }
    }
}

void create_udp_task(void)
{
    if (xTaskCreate(process_request, "process_cmd", 1024, NULL, 1, NULL) !=
        pdPASS) {
        printf("ERROR: unable to create QSFP control task\r\n");
    }
}

int configUDPServer(convData_t *cmdArrgs)
{
    /*
     * Kept for host API compatibility.  NetworkLayer uses AXIS TKEEP/TLAST;
     * it does not have the old UDP stack IFG/payload-size registers.
     */
	printf("QSFP NetworkLayer uses TLAST/TKEEP; legacy IFG=%lu size=%lu ignored\r\n",
	       (unsigned long)cmdArrgs[0].u, (unsigned long)cmdArrgs[1].u);
	return XST_SUCCESS;
}

int UDP10G_init(void)
{
    int status = enable_cmac();
    if (status != XST_SUCCESS) {
        return status;
    }
    status = configure_networklayer();
    if (status != XST_SUCCESS) {
        return status;
    }
    return init_udp_dma_driver();
}
