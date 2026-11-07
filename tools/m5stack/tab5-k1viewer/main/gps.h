/*
 * M5Stack GPS unit input for the Tab5 K1Viewer.
 *
 * Copyright 2026 Armel F4HWN
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TAB5_K1VIEWER_GPS_H
#define TAB5_K1VIEWER_GPS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int32_t lat_cas;
    int32_t lon_cas;
} gps_fix_t;

typedef enum {
    GPS_STATE_NO_DATA,
    GPS_STATE_DATA,
    GPS_STATE_NMEA,
    GPS_STATE_SEARCHING,
    GPS_STATE_FIX,
} gps_state_t;

typedef struct {
    gps_state_t state;
    uint32_t bytes_received;
    uint32_t sentences_received;
    uint32_t valid_sentences;
    uint32_t checksum_errors;
    uint32_t rmc_sentences;
    uint32_t fix_count;
    uint8_t satellites_used;
    bool satellites_used_known;
    uint8_t satellites_in_view;
    bool satellites_in_view_known;
    uint8_t strongest_snr;
    bool strongest_snr_known;
    bool has_fix;
    gps_fix_t fix;
} gps_status_t;

typedef void (*gps_status_callback_t)(const gps_status_t *status, void *context);

esp_err_t gps_unit_start(gps_status_callback_t callback, void *context);

#endif
