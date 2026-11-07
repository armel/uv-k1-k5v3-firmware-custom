/*
 * PL2303 USB-to-UART support for the Tab5 K1Viewer.
 *
 * Copyright 2026 Armel F4HWN
 * https://github.com/armel
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_private/cdc_host_common.h"
#include "pl2303_vcp.h"
#include "usb/usb_types_ch9.h"

#define PL2303_VID                       0x067Bu
#define PL2303_CLASS_WRITE               0x21u
#define PL2303_SET_LINE_CODING           0x20u
#define PL2303_SET_CONTROL_LINE_STATE    0x22u
#define PL2303_VENDOR_READ               0x01u
#define PL2303_VENDOR_WRITE              0x01u
#define PL2303_VENDOR_READ_HXN           0x81u
#define PL2303_VENDOR_WRITE_HXN          0x80u
#define PL2303_READ_TYPE_HX_STATUS       0x8080u
#define PL2303_RESET_UPSTREAM_VALUE      0x08u
#define PL2303_RESET_DOWNSTREAM_VALUE    0x09u
#define PL2303_HXN_RESET_REG             0x07u
#define PL2303_HXN_RESET_UPSTREAM_PIPE   0x02u
#define PL2303_HXN_RESET_DOWNSTREAM_PIPE 0x01u
#define PL2303_FLOW_CONTROL_REG          0x00u
#define PL2303_FLOW_CONTROL_MASK         0xF0u
#define PL2303_HXN_FLOW_CONTROL_REG      0x0Au
#define PL2303_HXN_FLOW_CONTROL_MASK     0x1Cu
#define PL2303_HXN_FLOW_CONTROL_NONE     0x1Cu

typedef struct {
    uint8_t interface_idx;
    bool hxn;
} pl2303_context_t;

static const char *TAG = "PL2303";

static esp_err_t pl2303_vendor_read(cdc_acm_dev_hdl_t cdc_hdl, bool hxn,
                                    uint16_t value, uint8_t *data)
{
    return cdc_acm_host_send_custom_request(
        cdc_hdl,
        USB_BM_REQUEST_TYPE_TYPE_VENDOR | USB_BM_REQUEST_TYPE_RECIP_DEVICE |
            USB_BM_REQUEST_TYPE_DIR_IN,
        hxn ? PL2303_VENDOR_READ_HXN : PL2303_VENDOR_READ,
        value, 0, 1, data);
}

static esp_err_t pl2303_vendor_write(cdc_acm_dev_hdl_t cdc_hdl, bool hxn,
                                     uint16_t value, uint16_t index)
{
    return cdc_acm_host_send_custom_request(
        cdc_hdl,
        USB_BM_REQUEST_TYPE_TYPE_VENDOR | USB_BM_REQUEST_TYPE_RECIP_DEVICE |
            USB_BM_REQUEST_TYPE_DIR_OUT,
        hxn ? PL2303_VENDOR_WRITE_HXN : PL2303_VENDOR_WRITE,
        value, index, 0, NULL);
}

static esp_err_t pl2303_legacy_init(cdc_acm_dev_hdl_t cdc_hdl)
{
    uint8_t value;

    ESP_RETURN_ON_ERROR(pl2303_vendor_read(cdc_hdl, false, 0x8484, &value), TAG,
                        "Initial read 1 failed");
    ESP_RETURN_ON_ERROR(pl2303_vendor_write(cdc_hdl, false, 0x0404, 0), TAG,
                        "Initial write 1 failed");
    ESP_RETURN_ON_ERROR(pl2303_vendor_read(cdc_hdl, false, 0x8484, &value), TAG,
                        "Initial read 2 failed");
    ESP_RETURN_ON_ERROR(pl2303_vendor_read(cdc_hdl, false, 0x8383, &value), TAG,
                        "Initial read 3 failed");
    ESP_RETURN_ON_ERROR(pl2303_vendor_read(cdc_hdl, false, 0x8484, &value), TAG,
                        "Initial read 4 failed");
    ESP_RETURN_ON_ERROR(pl2303_vendor_write(cdc_hdl, false, 0x0404, 1), TAG,
                        "Initial write 2 failed");
    ESP_RETURN_ON_ERROR(pl2303_vendor_read(cdc_hdl, false, 0x8484, &value), TAG,
                        "Initial read 5 failed");
    ESP_RETURN_ON_ERROR(pl2303_vendor_read(cdc_hdl, false, 0x8383, &value), TAG,
                        "Initial read 6 failed");
    ESP_RETURN_ON_ERROR(pl2303_vendor_write(cdc_hdl, false, 0, 1), TAG,
                        "Initial write 3 failed");
    ESP_RETURN_ON_ERROR(pl2303_vendor_write(cdc_hdl, false, 1, 0), TAG,
                        "Initial write 4 failed");
    return pl2303_vendor_write(cdc_hdl, false, 2, 0x44);
}

static esp_err_t pl2303_disable_flow_control(cdc_acm_dev_hdl_t cdc_hdl,
                                             bool hxn)
{
    const uint8_t reg =
        hxn ? PL2303_HXN_FLOW_CONTROL_REG : PL2303_FLOW_CONTROL_REG;
    const uint8_t mask =
        hxn ? PL2303_HXN_FLOW_CONTROL_MASK : PL2303_FLOW_CONTROL_MASK;
    const uint8_t setting = hxn ? PL2303_HXN_FLOW_CONTROL_NONE : 0;
    uint8_t value;

    ESP_RETURN_ON_ERROR(
        pl2303_vendor_read(cdc_hdl, hxn, hxn ? reg : (uint8_t)(reg | 0x80u),
                           &value),
        TAG, "Flow-control read failed");
    value = (uint8_t)((value & (uint8_t)~mask) | setting);
    return pl2303_vendor_write(cdc_hdl, hxn, reg, value);
}

static esp_err_t pl2303_reset_data_pipes(cdc_acm_dev_hdl_t cdc_hdl, bool hxn)
{
    if (hxn) {
        ESP_RETURN_ON_ERROR(
            pl2303_vendor_write(
                cdc_hdl, true, PL2303_HXN_RESET_REG,
                PL2303_HXN_RESET_UPSTREAM_PIPE |
                    PL2303_HXN_RESET_DOWNSTREAM_PIPE),
            TAG, "HXN data-pipe reset failed");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(
        pl2303_vendor_write(cdc_hdl, false, PL2303_RESET_UPSTREAM_VALUE, 0),
        TAG, "Upstream pipe reset failed");
    ESP_RETURN_ON_ERROR(
        pl2303_vendor_write(cdc_hdl, false, PL2303_RESET_DOWNSTREAM_VALUE, 0),
        TAG, "Downstream pipe reset failed");
    return ESP_OK;
}

static esp_err_t pl2303_line_coding_set(
    cdc_acm_dev_hdl_t cdc_hdl,
    const cdc_acm_line_coding_t *line_coding)
{
    ESP_RETURN_ON_FALSE(line_coding != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Line coding is missing");
    ESP_RETURN_ON_FALSE(line_coding->dwDTERate != 0 &&
                            line_coding->bCharFormat <= 2 &&
                            line_coding->bParityType <= 4 &&
                            line_coding->bDataBits >= 5 &&
                            line_coding->bDataBits <= 8,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid line coding");

    const pl2303_context_t *context = cdc_hdl->intf_func.user_data;
    const uint32_t baud = line_coding->dwDTERate;
    uint8_t data[7] = {
        (uint8_t)baud,
        (uint8_t)(baud >> 8),
        (uint8_t)(baud >> 16),
        (uint8_t)(baud >> 24),
        line_coding->bCharFormat,
        line_coding->bParityType,
        line_coding->bDataBits,
    };

    return cdc_acm_host_send_custom_request(
        cdc_hdl, PL2303_CLASS_WRITE, PL2303_SET_LINE_CODING, 0,
        context->interface_idx, sizeof(data), data);
}

static esp_err_t pl2303_set_control_line_state(cdc_acm_dev_hdl_t cdc_hdl,
                                                bool dtr, bool rts)
{
    const pl2303_context_t *context = cdc_hdl->intf_func.user_data;
    const uint16_t state = (dtr ? 1u : 0u) | (rts ? 2u : 0u);

    return cdc_acm_host_send_custom_request(
        cdc_hdl, PL2303_CLASS_WRITE, PL2303_SET_CONTROL_LINE_STATE, state,
        context->interface_idx, 0, NULL);
}

static void pl2303_delete(cdc_acm_dev_hdl_t cdc_hdl)
{
    if (cdc_hdl != NULL) {
        free(cdc_hdl->intf_func.user_data);
        cdc_hdl->intf_func.user_data = NULL;
    }
}

esp_err_t pl2303_vcp_open(uint16_t pid, uint8_t interface_idx,
                          const cdc_acm_host_device_config_t *dev_config,
                          cdc_acm_dev_hdl_t *cdc_hdl_ret)
{
    ESP_RETURN_ON_FALSE(dev_config != NULL && cdc_hdl_ret != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");

    cdc_acm_dev_hdl_t cdc_hdl = NULL;
    esp_err_t result = cdc_acm_host_open(PL2303_VID, pid, interface_idx,
                                         dev_config, &cdc_hdl);
    if (result != ESP_OK) {
        return result;
    }

    pl2303_context_t *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        cdc_acm_host_close(cdc_hdl);
        return ESP_ERR_NO_MEM;
    }
    context->interface_idx = interface_idx;

    uint8_t status;
    context->hxn = pl2303_vendor_read(cdc_hdl, false,
                                       PL2303_READ_TYPE_HX_STATUS,
                                       &status) != ESP_OK;

    cdc_hdl->intf_func.user_data = context;
    cdc_hdl->intf_func.line_coding_set = pl2303_line_coding_set;
    cdc_hdl->intf_func.set_control_line_state =
        pl2303_set_control_line_state;
    cdc_hdl->intf_func.del = pl2303_delete;

    if (!context->hxn) {
        result = pl2303_legacy_init(cdc_hdl);
    }
    if (result == ESP_OK) {
        result = pl2303_reset_data_pipes(cdc_hdl, context->hxn);
    }
    if (result == ESP_OK) {
        result = pl2303_disable_flow_control(cdc_hdl, context->hxn);
    }
    if (result != ESP_OK) {
        cdc_acm_host_close(cdc_hdl);
        return result;
    }

    ESP_LOGI(TAG, "Opened %s device", context->hxn ? "HXN" : "TA/HX");
    *cdc_hdl_ret = cdc_hdl;
    return ESP_OK;
}
