
#ifndef __TX_DRIVER_H_
#define __TX_DRIVER_H_

#include "cmd_parser.h"

#define FIFO_SIZE 64*1024*4

int triggerTX(convData_t *cmdArrgs);
int tx_xbar_init(void);
int loadSSB(convData_t* cmdArrgs);


#endif

