
#ifndef __DMA_DRIVER_H_
#define __DMA_DRIVER_H_

#define DMA_TX_DEV_ID		XPAR_AXIDMA_1_DEVICE_ID

#include "xil_io.h"

int SendPacketsDMA(const void *buf, u32 size);
int DMA_TX_driver_init(void);

#endif

