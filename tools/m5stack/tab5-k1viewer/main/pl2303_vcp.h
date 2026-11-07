/*
 * PL2303 USB-to-UART support for the Tab5 K1Viewer.
 *
 * Copyright 2026 Armel F4HWN
 * https://github.com/armel
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "usb/cdc_acm_host.h"

#define PL2303_PID 0x2303u

esp_err_t pl2303_vcp_open(uint16_t pid, uint8_t interface_idx,
                          const cdc_acm_host_device_config_t *dev_config,
                          cdc_acm_dev_hdl_t *cdc_hdl_ret);
