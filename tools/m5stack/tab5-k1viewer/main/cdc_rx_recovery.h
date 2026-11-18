/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "usb/cdc_acm_host.h"

typedef enum {
    CDC_RX_ENDPOINT_NONE = 0,
    CDC_RX_ENDPOINT_DATA = 1u << 0,
    CDC_RX_ENDPOINT_NOTIFICATION = 1u << 1,
} cdc_rx_endpoint_mask_t;

/* IN endpoints whose poll flag is clear. A clear flag is only a hint: the
 * driver also clears it while a completed transfer runs its callback. */
esp_err_t cdc_rx_stopped_endpoints(cdc_acm_dev_hdl_t cdc_hdl,
                                   cdc_rx_endpoint_mask_t *endpoints);
/* Tell whether one IN endpoint (DATA or NOTIFICATION) is really halted by a
 * transfer error, whatever its callback is doing on the other core. */
esp_err_t cdc_rx_endpoint_halted(cdc_acm_dev_hdl_t cdc_hdl,
                                 cdc_rx_endpoint_mask_t endpoint,
                                 bool *halted);
esp_err_t cdc_rx_get_device_id(cdc_acm_dev_hdl_t cdc_hdl,
                               uint16_t *vid, uint16_t *pid);
/* Restart a halted BULK IN endpoint. before_submit, when not NULL, runs once
 * the pipe is flushed and before it is resubmitted: no data callback can run
 * meanwhile, so it may reset the state that callback owns. Returns
 * ESP_ERR_INVALID_STATE without side effects while the endpoint still polls. */
esp_err_t cdc_rx_recover_bulk_in(cdc_acm_dev_hdl_t cdc_hdl,
                                 void (*before_submit)(void));
esp_err_t cdc_rx_recover_notification(cdc_acm_dev_hdl_t cdc_hdl);
