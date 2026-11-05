/*
 * Copyright (c) 2022, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

/* ================ USB common Configuration ================ */

#define CONFIG_USB_PRINTF(...) //printf(__VA_ARGS__)

#define usb_malloc(size) malloc(size)
#define usb_free(ptr)    free(ptr)

#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL USB_DBG_ERROR
#endif

/* Enable print with color */
#define CONFIG_USB_PRINTF_COLOR_ENABLE

/* data align size when use dma */
#ifndef CONFIG_USB_ALIGN_SIZE
#define CONFIG_USB_ALIGN_SIZE 4
#endif

/* attribute data into no cache ram */
#define USB_NOCACHE_RAM_SECTION __attribute__((section(".noncacheable")))

/* ================= USB Device Stack Configuration ================ */

/* Ep0 max transfer buffer, specially for receiving data from ep0 out */
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 256

/* Setup packet log for debug */
// #define CONFIG_USBDEV_SETUP_LOG_PRINT

/* Check if the input descriptor is correct */
// #define CONFIG_USBDEV_DESC_CHECK

/* Enable test mode */
// #define CONFIG_USBDEV_TEST_MODE

#ifndef CONFIG_USBDEV_MSC_BLOCK_SIZE
#define CONFIG_USBDEV_MSC_BLOCK_SIZE 512
#endif

#ifndef CONFIG_USBDEV_MSC_MANUFACTURER_STRING
#define CONFIG_USBDEV_MSC_MANUFACTURER_STRING ""
#endif

#ifndef CONFIG_USBDEV_MSC_PRODUCT_STRING
#define CONFIG_USBDEV_MSC_PRODUCT_STRING ""
#endif

#ifndef CONFIG_USBDEV_MSC_VERSION_STRING
#define CONFIG_USBDEV_MSC_VERSION_STRING "0.01"
#endif

// #define CONFIG_USBDEV_MSC_THREAD

#ifdef CONFIG_USBDEV_MSC_THREAD
#ifndef CONFIG_USBDEV_MSC_STACKSIZE
#define CONFIG_USBDEV_MSC_STACKSIZE 2048
#endif

#ifndef CONFIG_USBDEV_MSC_PRIO
#define CONFIG_USBDEV_MSC_PRIO 4
#endif
#endif

#ifndef CONFIG_USBDEV_AUDIO_VERSION
#define CONFIG_USBDEV_AUDIO_VERSION 0x0100
#endif

#ifndef CONFIG_USBDEV_AUDIO_MAX_CHANNEL
#define CONFIG_USBDEV_AUDIO_MAX_CHANNEL 8
#endif


/* ================ USB Device Port Configuration ================*/
#include "py32f0xx.h"

#define USBD_IRQn       USB_IRQn

#define USBD_IRQHandler USB_IRQHandler

typedef struct
{
    uint8_t *buf;
    const uint32_t size;
    volatile uint32_t *write_pointer;
} cdc_acm_rx_buf_t;

void cdc_acm_init(cdc_acm_rx_buf_t rx_buf);
void cdc_acm_data_send_with_dtr(const uint8_t *buf, uint32_t size);
void cdc_acm_data_send_with_dtr_async(const uint8_t *buf, uint32_t size);

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
/* Overlay-app serial port (app_overlay.c). */
#include <stdbool.h>
extern volatile bool     ep_tx_busy_flag;
extern volatile uint8_t  dtr_enable;
extern volatile uint32_t cdc_acm_rx_total;
extern volatile uint8_t  cdc_acm_config_count;
bool cdc_acm_start_send(const uint8_t *buf, uint32_t size);
void cdc_acm_cancel_send(void);
void usbd_ep_cancel_write(const uint8_t ep);
#endif

#endif
