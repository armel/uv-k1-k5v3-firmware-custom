/*
 * M5Unified hardware adapter for the Tab5 K1Viewer.
 *
 * Copyright 2026 Armel F4HWN
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TAB5_HARDWARE_H
#define TAB5_HARDWARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tab5_hardware_init(void);
lv_display_t *tab5_display_get(void);
lv_indev_t *tab5_touch_get(void);
bool tab5_display_lock(uint32_t timeout_ms);
void tab5_display_unlock(void);
void tab5_display_rotate(lv_display_rotation_t rotation);
void tab5_display_brightness_set(uint8_t percent);
const char *tab5_m5unified_version(void);
bool tab5_imu_available(void);
bool tab5_imu_read_accel(float accel[3]);
/* Register access on the internal I2C bus, under M5Unified's bus lock. */
bool tab5_internal_i2c_read(uint8_t address, uint8_t reg, uint8_t *data, size_t length);
bool tab5_internal_i2c_write(uint8_t address, uint8_t reg, const uint8_t *data,
                             size_t length);
bool tab5_speaker_amp_enable(void);
esp_err_t tab5_usb_power_enable(void);
esp_err_t tab5_external_power_enable(void);

#ifdef __cplusplus
}
#endif

#endif
