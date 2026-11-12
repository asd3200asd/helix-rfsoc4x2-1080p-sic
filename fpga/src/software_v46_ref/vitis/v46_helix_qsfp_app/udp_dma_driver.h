#ifndef __UDP_DMA_DRIVER_H_
#define __UDP_DMA_DRIVER_H_

#include "xil_types.h"

int init_udp_dma_driver(void);
u8 recv(u8 *RxBufferPtr, u16 len);
u8 send(u32 *TxBufferPtr, u16 len);

#endif

