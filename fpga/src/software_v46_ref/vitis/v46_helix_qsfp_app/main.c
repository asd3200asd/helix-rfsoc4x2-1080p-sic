#include <stdio.h>
#include "xparameters.h"
#include "FreeRTOS.h"
#include "task.h"


#include "xrfclk.h"
#include "gpio_driver.h"
#include "tx_driver.h"
#include "rx_driver.h"
#include "rfdc_driver.h"
#include "sdfs_driver.h"
#include "udp_driver.h"
#include "tools.h"
#include "tx_dma_driver.h"

#define DEBUG


void main_thread(void *p);

#define THREAD_STACKSIZE 1024


int init_drivers()
{
	int status;

	if (clockCFG_init(LMK_122_88MHZ) != XST_SUCCESS) {
		printf("ERROR: Failed to configure RF clock\n");
		return XST_FAILURE;
	}
	//clockCFG_init(LMK_110MHZ);

	status = RFDC_driver_init();
	if (status) {
		printf("ERROR: Failed to run RFDC Driver initialization\n");
		return XST_FAILURE;
	}

	status = conv_init();
	if (status) {
		printf("ERROR: Failed to run converters initialization\n");
		return XST_FAILURE;
	}

	status = NCO_init();
	if (status) {
		printf("ERROR: Failed to run NCO initialization\n");
		return XST_FAILURE;
	}

	status = config_gpio();
	if (status) {
		printf("ERROR: Failed to run GPIO initialization\n");
		return XST_FAILURE;
	}

	status = SdFs_init();
	if (status) {
		printf("ERROR: Failed to run SdFs initialization\n");
		return XST_FAILURE;
	}

	status = DMA_TX_driver_init();
	if (status) {
		printf("ERROR: Failed to run DMA DAC initialization\n");
		return XST_FAILURE;
	}


	/* V46 keeps the legacy API name but initializes QSFP100G NetworkLayer. */
	status = UDP10G_init();
	if (status) {
		printf("ERROR: Failed to run QSFP NetworkLayer/control DMA initialization\n");
		return XST_FAILURE;
	}

	status = tx_xbar_init();
	if (status) {
		printf("ERROR: Failed to run tx xbar\n");
		return XST_FAILURE;
	}

	status = rx_xbar_init();
	if (status) {
		printf("ERROR: Failed to run rx xbar\n");
		return XST_FAILURE;
	}


#ifdef INTERRUPTS
	status = intr_init();
	if (status) {
		printf("ERROR: Failed to run interrupt driver initialization\n");
		return XST_FAILURE;
	}
#endif

    printf("HELIX_QSFP_V46_CONTROL_READY\r\n");
    return 0;

}


void main_thread(void *p)
{
	//initialize modules (DMA, UDP, RF-IP...)
    if (init_drivers() != XST_SUCCESS) {
        printf("ERROR: HELIX driver initialization stopped\r\n");
        vTaskDelete(NULL);
        return;
    }

    //create UDP clien task
    create_udp_task();

	vTaskDelete(NULL);

	return;
}

int main()
{
	BaseType_t task_status;

	task_status = xTaskCreate(main_thread,"main_thread",1024 , NULL,
			2, NULL);
	if (task_status != pdPASS) {
		printf("ERROR: unable to create HELIX main task\r\n");
		return 1;
	}
	vTaskStartScheduler();
	printf("ERROR: FreeRTOS scheduler returned unexpectedly\r\n");
	return 1;
}
