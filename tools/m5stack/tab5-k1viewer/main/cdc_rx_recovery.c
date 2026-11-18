/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The upstream CDC-ACM driver stops polling an IN endpoint after a transfer
 * error. Its public API does not expose endpoint recovery or the device
 * handle behind an open CDC interface, so this shim uses the pinned 2.4.1
 * private device structure for both operations. */

#include "cdc_rx_recovery.h"

#include "esp_private/cdc_host_common.h"

/* Flush and clear an IN endpoint that a transfer error has halted. The USB
 * host only flushes a halted pipe: ESP_OK here proves that no transfer of
 * this endpoint is queued, so its callback cannot run until the resubmission.
 * A pipe that is still active is refused with ESP_ERR_INVALID_STATE and left
 * untouched. */
static esp_err_t flush_halted_endpoint(cdc_dev_t *cdc_dev,
                                       const usb_transfer_t *transfer,
                                       const bool *polling)
{
    if (transfer == NULL || *polling) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = usb_host_endpoint_flush(cdc_dev->dev_hdl,
                                                     transfer->bEndpointAddress);
    if (result != ESP_OK) {
        return result;
    }

    return usb_host_endpoint_clear(cdc_dev->dev_hdl,
                                   transfer->bEndpointAddress);
}

static esp_err_t resubmit_endpoint(usb_transfer_t *transfer, bool *polling)
{
    /* Publish the active state before submission. A transfer can fail on the
     * other core immediately after it is queued, and its callback must be the
     * last writer when that happens. */
    *polling = true;
    const esp_err_t result = usb_host_transfer_submit(transfer);
    if (result == ESP_OK || result == ESP_ERR_NOT_FINISHED) {
        return ESP_OK;
    }
    *polling = false;
    return result;
}

esp_err_t cdc_rx_stopped_endpoints(cdc_acm_dev_hdl_t cdc_hdl,
                                   cdc_rx_endpoint_mask_t *endpoints)
{
    if (cdc_hdl == NULL || endpoints == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const cdc_dev_t *cdc_dev = (const cdc_dev_t *)cdc_hdl;
    uint32_t stopped = CDC_RX_ENDPOINT_NONE;
    if (cdc_dev->data.in_xfer != NULL && !cdc_dev->data.in_polling) {
        stopped |= CDC_RX_ENDPOINT_DATA;
    }
    if (cdc_dev->notif.xfer != NULL && !cdc_dev->notif.xfer_polling) {
        stopped |= CDC_RX_ENDPOINT_NOTIFICATION;
    }
    *endpoints = (cdc_rx_endpoint_mask_t)stopped;
    return ESP_OK;
}

esp_err_t cdc_rx_endpoint_halted(cdc_acm_dev_hdl_t cdc_hdl,
                                 cdc_rx_endpoint_mask_t endpoint,
                                 bool *halted)
{
    if (cdc_hdl == NULL || halted == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cdc_dev_t *cdc_dev = (cdc_dev_t *)cdc_hdl;
    const usb_transfer_t *transfer;
    const bool *polling;
    if (endpoint == CDC_RX_ENDPOINT_DATA) {
        transfer = cdc_dev->data.in_xfer;
        polling = &cdc_dev->data.in_polling;
    } else if (endpoint == CDC_RX_ENDPOINT_NOTIFICATION) {
        transfer = cdc_dev->notif.xfer;
        polling = &cdc_dev->notif.xfer_polling;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    if (transfer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    *halted = false;
    if (*polling) {
        return ESP_OK;
    }

    /* Only an error halts the pipe of an open endpoint, and the USB host
     * flushes nothing but a halted pipe: the flush is the test. It refuses an
     * active pipe without touching it (the USB host logs "EP command error"),
     * and empties nothing on a halted one, whose failed transfer has already
     * been returned. */
    const esp_err_t result = usb_host_endpoint_flush(cdc_dev->dev_hdl,
                                                     transfer->bEndpointAddress);
    if (result == ESP_OK) {
        *halted = true;
        return ESP_OK;
    }
    return result == ESP_ERR_INVALID_STATE ? ESP_OK : result;
}

esp_err_t cdc_rx_get_device_id(cdc_acm_dev_hdl_t cdc_hdl,
                               uint16_t *vid, uint16_t *pid)
{
    if (cdc_hdl == NULL || vid == NULL || pid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const cdc_dev_t *cdc_dev = (const cdc_dev_t *)cdc_hdl;
    const usb_device_desc_t *descriptor = NULL;
    const esp_err_t result =
        usb_host_get_device_descriptor(cdc_dev->dev_hdl, &descriptor);
    if (result != ESP_OK) {
        return result;
    }
    if (descriptor == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *vid = descriptor->idVendor;
    *pid = descriptor->idProduct;
    return ESP_OK;
}

esp_err_t cdc_rx_recover_bulk_in(cdc_acm_dev_hdl_t cdc_hdl,
                                 void (*before_submit)(void))
{
    if (cdc_hdl == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cdc_dev_t *cdc_dev = (cdc_dev_t *)cdc_hdl;
    usb_transfer_t *transfer = cdc_dev->data.in_xfer;
    if (transfer == NULL || cdc_dev->data.in_mps == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = flush_halted_endpoint(cdc_dev, transfer,
                                             &cdc_dev->data.in_polling);
    if (result != ESP_OK) {
        return result;
    }

    /* The pipe is flushed and nothing is queued on it: the IN callback cannot
     * touch the transfer or the receive state of the caller until it is
     * submitted again. */
    uint8_t **data_buffer = (uint8_t **)&transfer->data_buffer;
    *data_buffer = cdc_dev->data.in_data_buffer_base;
    transfer->num_bytes = transfer->data_buffer_size;
    transfer->num_bytes -= transfer->data_buffer_size % cdc_dev->data.in_mps;
    if (before_submit != NULL) {
        before_submit();
    }

    return resubmit_endpoint(transfer, &cdc_dev->data.in_polling);
}

esp_err_t cdc_rx_recover_notification(cdc_acm_dev_hdl_t cdc_hdl)
{
    if (cdc_hdl == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cdc_dev_t *cdc_dev = (cdc_dev_t *)cdc_hdl;
    const esp_err_t result =
        flush_halted_endpoint(cdc_dev, cdc_dev->notif.xfer,
                              &cdc_dev->notif.xfer_polling);
    if (result != ESP_OK) {
        return result;
    }
    return resubmit_endpoint(cdc_dev->notif.xfer, &cdc_dev->notif.xfer_polling);
}
