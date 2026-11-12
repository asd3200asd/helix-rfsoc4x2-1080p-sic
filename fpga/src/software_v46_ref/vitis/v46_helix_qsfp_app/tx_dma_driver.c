
#include "tx_dma_driver.h"
#include "tools.h"
#include "xaxidma.h"
#include "xaxidma_hw.h"
#include "xtime_l.h"

#define DDR_ADDR	XPAR_PSU_DDR_0_S_AXI_BASEADDR

/************************** Variable Definitions *****************************/
/*
 * Device instance definitions
 */
XAxiDma AxiDmaTX;

u32 *Packet = (u32 *) DDR_ADDR + 0x100000;

#define DMA_TX_TIMEOUT_SECONDS 1U
#define DMA_TX_RESET_LIMIT 1000000U
#define DMA_TX_MAX_BYTES ((1U << XPAR_TX_BLOCKS_AXI_DMA_0_SG_LENGTH_WIDTH) - 1U)

static u32 dma_tx_status(void)
{
	return Xil_In32(XPAR_TX_BLOCKS_AXI_DMA_0_BASEADDR +
			XAXIDMA_TX_OFFSET + XAXIDMA_SR_OFFSET);
}

static void print_dma_tx_status(const char *tag)
{
	u32 status = dma_tx_status();
	printf("%s: MM2S_DMASR=0x%08lx", tag, (unsigned long)status);
	if (status & XAXIDMA_HALTED_MASK) printf(" HALTED");
	if (status & XAXIDMA_IDLE_MASK) printf(" IDLE");
	if (status & XAXIDMA_ERR_INTERNAL_MASK) printf(" DMA_INT_ERR");
	if (status & XAXIDMA_ERR_SLAVE_MASK) printf(" DMA_SLV_ERR");
	if (status & XAXIDMA_ERR_DECODE_MASK) printf(" DMA_DEC_ERR");
	if (status & XAXIDMA_IRQ_IOC_MASK) printf(" IOC");
	if (status & XAXIDMA_IRQ_ERROR_MASK) printf(" ERROR_IRQ");
	printf("\r\n");
}

static int reset_dma_tx(void)
{
	u32 count;

	XAxiDma_Reset(&AxiDmaTX);
	for (count = 0U; count < DMA_TX_RESET_LIMIT; ++count) {
		if (XAxiDma_ResetIsDone(&AxiDmaTX)) return XST_SUCCESS;
	}
	return XST_FAILURE;
}


int SendPacketsDMA(const void *buf, u32 size){
	int Status;
	XTime start_time;
	XTime now;
	XTime timeout_ticks =
		(XTime)DMA_TX_TIMEOUT_SECONDS * (XTime)COUNTS_PER_SECOND;
	u32 dma_status;

	if ((buf == NULL) || (size == 0U) || (size > DMA_TX_MAX_BYTES)) {
		printf("DMA ERROR: invalid buffer/size %lu (maximum %lu)\r\n",
				(unsigned long)size, (unsigned long)DMA_TX_MAX_BYTES);
		return XST_FAILURE;
	}

	/* Disable interrupts, we use polling mode*/
	XAxiDma_IntrDisable(&AxiDmaTX, XAXIDMA_IRQ_ALL_MASK,
						XAXIDMA_DMA_TO_DEVICE);


	/* Flush the buffers before the DMA transfer, in case the Data Cache
	 * is enabled
	 */
	Xil_DCacheFlushRange((UINTPTR)buf, size);

	Status = XAxiDma_SimpleTransfer(&AxiDmaTX,(UINTPTR) buf,
			size, XAXIDMA_DMA_TO_DEVICE);

	if (Status != XST_SUCCESS) {
		printf("DMA ERROR: %d \r\n", Status);
		return XST_FAILURE;
	}


	XTime_GetTime(&start_time);
	while (XAxiDma_Busy(&AxiDmaTX,XAXIDMA_DMA_TO_DEVICE)) {
		dma_status = dma_tx_status();
		if (dma_status & XAXIDMA_ERR_ALL_MASK) {
			print_dma_tx_status("DMA ERROR");
			return XST_FAILURE;
		}
		XTime_GetTime(&now);
		if ((now - start_time) >= timeout_ticks) {
			print_dma_tx_status("DMA TIMEOUT");
			printf("No AXI error means the HELIX stream stopped on TREADY/TLAST/reset/clock.\r\n");
			reset_dma_tx();
			return XST_FAILURE;
		}
	}

	print_dma_tx_status("DMA COMPLETE");
	return XST_SUCCESS;
}

int DMA_TX_driver_init(void)
{
	XAxiDma_Config *CfgPtr;
	int Status;

	/* Initialize the XAxiDma device.
	 */
	CfgPtr = XAxiDma_LookupConfig(DMA_TX_DEV_ID);
	if (!CfgPtr) {
		printf("No config found for %d\r\n", DMA_TX_DEV_ID);
		return XST_FAILURE;
	}

	Status = XAxiDma_CfgInitialize(&AxiDmaTX, CfgPtr);
	if (Status != XST_SUCCESS) {
		printf("Initialization failed %d\r\n", Status);
		return XST_FAILURE;
	}

	if(XAxiDma_HasSg(&AxiDmaTX)){
		printf("Device configured as SG mode \r\n");
		return XST_FAILURE;
	}

	if (reset_dma_tx() != XST_SUCCESS) {
		printf("DMA TX reset timeout\r\n");
		return XST_FAILURE;
	}

	printf("DMA TX initialized: device=%u base=0x%08lx max_bytes=%lu\r\n",
			(unsigned int)DMA_TX_DEV_ID,
			(unsigned long)XPAR_TX_BLOCKS_AXI_DMA_0_BASEADDR,
			(unsigned long)DMA_TX_MAX_BYTES);

	return XST_SUCCESS;
}
