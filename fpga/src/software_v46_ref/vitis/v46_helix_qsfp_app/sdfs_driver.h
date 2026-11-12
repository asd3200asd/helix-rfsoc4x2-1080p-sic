
#ifndef __SDFS_DRIVER_H_
#define __SDFS_DRIVER_H_

#include "xil_io.h"

int read_ip_from_file(char *path, u8 *ip_add, u8 *msk, u8 *gw_add);
int write_to_file(char *path, u8* buf,size_t len);
u32 read_from_file(char *path, int16_t * buf);
int SdFs_init(void);

#endif
