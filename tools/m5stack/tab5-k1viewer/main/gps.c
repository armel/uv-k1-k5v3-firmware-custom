/*
 * M5Stack Mini GPS/BDS unit input for the Tab5 K1Viewer.
 *
 * Copyright 2026 Armel F4HWN
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gps.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GPS_UART             UART_NUM_1
#define GPS_UART_TX          GPIO_NUM_53  /* yellow: GPS unit RX */
#define GPS_UART_RX          GPIO_NUM_54  /* white: GPS unit TX */
#define GPS_UART_BAUD        9600
#define GPS_UART_RX_BYTES    512
#define GPS_BAUD_TRY_MS      3000u  /* per rate while no valid sentence */
#define GPS_LINE_BYTES       128
#define GPS_TASK_STACK_BYTES 4096
#define GPS_TASK_PRIORITY    4
#define GPS_EXT5V_EN         IO_EXPANDER_PIN_NUM_2

static const char *TAG = "tab5-gps";
static gps_status_callback_t status_callback;
static void *status_context;

typedef enum {
    GSV_SOURCE_GP,
    GSV_SOURCE_BDS,
    GSV_SOURCE_GL,
    GSV_SOURCE_GA,
    GSV_SOURCE_QZ,
    GSV_SOURCE_GI,
    GSV_SOURCE_GN,
    GSV_SOURCE_OTHER,
    GSV_SOURCE_COUNT,
} gsv_source_id_t;

typedef struct {
    bool seen;
    uint8_t satellites;
    uint8_t strongest_snr;
    bool strongest_snr_known;
} gsv_source_t;

typedef struct {
    gsv_source_t sources[GSV_SOURCE_COUNT];
} gsv_tracker_t;

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static bool checksum_ok(char *line, char **checksum)
{
    if (line[0] != '$') {
        return false;
    }

    char *star = strchr(line, '*');
    if (star == NULL || star[1] == '\0' || star[2] == '\0' || star[3] != '\0') {
        return false;
    }

    uint8_t value = 0;
    for (const char *p = line + 1; p < star; p++) {
        value ^= (uint8_t)*p;
    }

    const int high = hex_digit(star[1]);
    const int low = hex_digit(star[2]);
    if (high < 0 || low < 0 || value != (uint8_t)((high << 4) | low)) {
        return false;
    }

    *checksum = star;
    return true;
}

/* Convert ddmm.mmmm or dddmm.mmmm to signed centi-arcseconds later. */
static bool parse_coordinate(const char *text, uint8_t degree_digits,
                             uint32_t limit_cas, uint32_t *coordinate)
{
    if (strlen(text) < (size_t)degree_digits + 2u) {
        return false;
    }

    uint32_t degrees = 0;
    for (uint8_t i = 0; i < degree_digits; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        degrees = degrees * 10u + (uint32_t)(text[i] - '0');
    }

    const char *minutes = text + degree_digits;
    if (minutes[0] < '0' || minutes[0] > '9' ||
        minutes[1] < '0' || minutes[1] > '9') {
        return false;
    }
    const uint32_t whole_minutes =
        (uint32_t)(minutes[0] - '0') * 10u + (uint32_t)(minutes[1] - '0');
    if (whole_minutes >= 60u) {
        return false;
    }

    uint32_t fraction = 0;
    uint32_t scale = 1;
    const char *p = minutes + 2;
    if (*p == '.') {
        p++;
        uint8_t digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (digits < 6u) {
                fraction = fraction * 10u + (uint32_t)(*p - '0');
                scale *= 10u;
                digits++;
            }
            p++;
        }
        if (digits == 0u) {
            return false;
        }
    }
    if (*p != '\0') {
        return false;
    }

    const uint32_t fractional_cas =
        (uint32_t)(((uint64_t)fraction * 6000u + scale / 2u) / scale);
    const uint32_t value = degrees * 360000u + whole_minutes * 6000u + fractional_cas;
    if (value > limit_cas) {
        return false;
    }

    *coordinate = value;
    return true;
}

static uint8_t split_fields(char *text, char **fields, uint8_t capacity)
{
    uint8_t count = 0;
    for (char *p = text; count < capacity; count++) {
        fields[count] = p;
        char *comma = strchr(p, ',');
        if (comma == NULL) {
            count++;
            break;
        }
        *comma = '\0';
        p = comma + 1;
    }
    return count;
}

static bool parse_u8(const char *text, uint8_t maximum, uint8_t *value)
{
    if (*text == '\0') {
        return false;
    }

    uint32_t parsed = 0;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        parsed = parsed * 10u + (uint32_t)(*p - '0');
        if (parsed > maximum) {
            return false;
        }
    }
    *value = (uint8_t)parsed;
    return true;
}

static void parse_gga(char **fields, uint8_t count, gps_status_t *status)
{
    uint8_t quality = 0;
    uint8_t satellites = 0;
    if (count < 8u || !parse_u8(fields[6], UINT8_MAX, &quality) ||
        !parse_u8(fields[7], UINT8_MAX, &satellites)) {
        return;
    }
    status->satellites_used = satellites;
    status->satellites_used_known = true;

    /* A positive GGA quality confirms that the receiver has a solution, but
     * forwarding still waits for an active RMC sentence with coordinates. */
    if (quality == 0u && status->state < GPS_STATE_SEARCHING) {
        status->state = GPS_STATE_SEARCHING;
    }
}

static gsv_source_id_t gsv_source_id(const char *sentence)
{
    if (memcmp(sentence, "GP", 2u) == 0) {
        return GSV_SOURCE_GP;
    }
    if (memcmp(sentence, "BD", 2u) == 0 || memcmp(sentence, "GB", 2u) == 0) {
        return GSV_SOURCE_BDS;
    }
    if (memcmp(sentence, "GL", 2u) == 0) {
        return GSV_SOURCE_GL;
    }
    if (memcmp(sentence, "GA", 2u) == 0) {
        return GSV_SOURCE_GA;
    }
    if (memcmp(sentence, "GQ", 2u) == 0 || memcmp(sentence, "QZ", 2u) == 0) {
        return GSV_SOURCE_QZ;
    }
    if (memcmp(sentence, "GI", 2u) == 0) {
        return GSV_SOURCE_GI;
    }
    if (memcmp(sentence, "GN", 2u) == 0) {
        return GSV_SOURCE_GN;
    }
    return GSV_SOURCE_OTHER;
}

static void publish_gsv(const gsv_tracker_t *tracker, gps_status_t *status)
{
    uint16_t satellites = 0;
    bool satellites_known = false;
    if (tracker->sources[GSV_SOURCE_GN].seen) {
        satellites = tracker->sources[GSV_SOURCE_GN].satellites;
        satellites_known = true;
    } else {
        for (uint8_t i = 0; i < GSV_SOURCE_GN; i++) {
            if (tracker->sources[i].seen) {
                satellites += tracker->sources[i].satellites;
                satellites_known = true;
            }
        }
        if (tracker->sources[GSV_SOURCE_OTHER].seen) {
            satellites += tracker->sources[GSV_SOURCE_OTHER].satellites;
            satellites_known = true;
        }
    }

    status->satellites_in_view =
        satellites > UINT8_MAX ? UINT8_MAX : (uint8_t)satellites;
    status->satellites_in_view_known = satellites_known;

    uint8_t strongest_snr = 0;
    bool strongest_snr_known = false;
    for (uint8_t i = 0; i < GSV_SOURCE_COUNT; i++) {
        const gsv_source_t *source = &tracker->sources[i];
        if (source->strongest_snr_known &&
            (!strongest_snr_known || source->strongest_snr > strongest_snr)) {
            strongest_snr = source->strongest_snr;
            strongest_snr_known = true;
        }
    }
    status->strongest_snr = strongest_snr;
    status->strongest_snr_known = strongest_snr_known;
}

static void parse_gsv(char **fields, uint8_t count, gps_status_t *status,
                      gsv_tracker_t *tracker)
{
    uint8_t message_count = 0;
    uint8_t message_number = 0;
    uint8_t satellites = 0;
    if (count < 4u || !parse_u8(fields[1], UINT8_MAX, &message_count) ||
        !parse_u8(fields[2], UINT8_MAX, &message_number) ||
        !parse_u8(fields[3], UINT8_MAX, &satellites) || message_count == 0u ||
        message_number == 0u || message_number > message_count) {
        return;
    }

    gsv_source_t *source = &tracker->sources[gsv_source_id(fields[0])];
    if (!source->seen || message_number == 1u) {
        source->strongest_snr = 0;
        source->strongest_snr_known = false;
    }
    source->seen = true;
    source->satellites = satellites;

    /* Each GSV satellite group has SVID, elevation, azimuth and C/N0. */
    for (uint8_t i = 7u; i < count && i <= 19u; i += 4u) {
        uint8_t snr = 0;
        if (parse_u8(fields[i], 99u, &snr) &&
            (!source->strongest_snr_known || snr > source->strongest_snr)) {
            source->strongest_snr = snr;
            source->strongest_snr_known = true;
        }
    }
    publish_gsv(tracker, status);
}

static void parse_rmc(char **fields, uint8_t count, gps_status_t *status)
{
    if (count < 3u || strlen(fields[2]) != 1u ||
        (fields[2][0] != 'A' && fields[2][0] != 'V')) {
        return;
    }

    status->rmc_sentences++;
    if (fields[2][0] == 'V') {
        status->state = GPS_STATE_SEARCHING;
        return;
    }

    if (count < 7u || strlen(fields[4]) != 1u || strlen(fields[6]) != 1u) {
        return;
    }

    uint32_t latitude = 0;
    uint32_t longitude = 0;
    if (!parse_coordinate(fields[3], 2u, 32400000u, &latitude) ||
        !parse_coordinate(fields[5], 3u, 64800000u, &longitude) ||
        (fields[4][0] != 'N' && fields[4][0] != 'S') ||
        (fields[6][0] != 'E' && fields[6][0] != 'W')) {
        return;
    }

    status->fix.lat_cas = fields[4][0] == 'S' ? -(int32_t)latitude : (int32_t)latitude;
    status->fix.lon_cas = fields[6][0] == 'W' ? -(int32_t)longitude : (int32_t)longitude;
    status->has_fix = true;
    status->fix_count++;
    status->state = GPS_STATE_FIX;
}

static void process_sentence(char *line, gps_status_t *status,
                             gsv_tracker_t *gsv_tracker)
{
    status->sentences_received++;

    char *star = NULL;
    if (!checksum_ok(line, &star)) {
        status->checksum_errors++;
        return;
    }
    *star = '\0';
    status->valid_sentences++;
    if (status->state < GPS_STATE_NMEA) {
        status->state = GPS_STATE_NMEA;
    }

    char *fields[24] = {0};
    const uint8_t count = split_fields(line + 1, fields,
                                       sizeof(fields) / sizeof(fields[0]));
    if (count == 0u || strlen(fields[0]) != 5u) {
        return;
    }
    if (memcmp(fields[0] + 2, "RMC", 3u) == 0) {
        parse_rmc(fields, count, status);
    } else if (memcmp(fields[0] + 2, "GGA", 3u) == 0) {
        parse_gga(fields, count, status);
    } else if (memcmp(fields[0] + 2, "GSV", 3u) == 0) {
        parse_gsv(fields, count, status, gsv_tracker);
    }
}

static void gps_task(void *arg)
{
    (void)arg;
    char line[GPS_LINE_BYTES];
    size_t length = 0;
    bool collecting = false;
    bool first_fix = true;
    uint8_t input[64];
    gps_status_t status = {0};
    gsv_tracker_t gsv_tracker = {0};

    /* The unit's rate: the Mini GPS/BDS (AT6558) ships at 9600, the GPS/BDS
     * Unit v1.1 (AT6668) at 115200, and a unit set up elsewhere may keep
     * another one. No valid sentence for GPS_BAUD_TRY_MS: the next rate. */
    static const int bauds[] = {GPS_UART_BAUD, 115200, 38400, 57600, 19200, 4800};
    uint8_t baud_index = 0;
    uint32_t baud_valid = 0;
    bool baud_found = false;
    TickType_t baud_tick = xTaskGetTickCount();

    while (true) {
        if (!baud_found && status.valid_sentences >= baud_valid + 3u) {
            baud_found = true;
            ESP_LOGI(TAG, "NMEA at %d baud", bauds[baud_index]);
        } else if (!baud_found &&
                   xTaskGetTickCount() - baud_tick >= pdMS_TO_TICKS(GPS_BAUD_TRY_MS)) {
            baud_index = (uint8_t)((baud_index + 1u) % (sizeof(bauds) / sizeof(bauds[0])));
            uart_set_baudrate(GPS_UART, (uint32_t)bauds[baud_index]);
            uart_flush_input(GPS_UART);
            collecting = false;
            length = 0;
            baud_valid = status.valid_sentences;
            baud_tick = xTaskGetTickCount();
            ESP_LOGI(TAG, "No NMEA, trying %d baud", bauds[baud_index]);
        }
        const int received = uart_read_bytes(GPS_UART, input, sizeof(input),
                                             pdMS_TO_TICKS(200));
        if (received > 0) {
            status.bytes_received += (uint32_t)received;
            if (status.state == GPS_STATE_NO_DATA) {
                status.state = GPS_STATE_DATA;
            }
            status_callback(&status, status_context);
        }
        for (int i = 0; i < received; i++) {
            const char c = (char)input[i];
            if (c == '$') {
                collecting = true;
                length = 0;
                line[length++] = c;
                continue;
            }
            if (!collecting) {
                continue;
            }
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                line[length] = '\0';
                const uint32_t previous_fix_count = status.fix_count;
                process_sentence(line, &status, &gsv_tracker);
                if (status.fix_count != previous_fix_count) {
                    if (first_fix) {
                        ESP_LOGI(TAG, "First valid RMC fix received");
                        first_fix = false;
                    }
                }
                status_callback(&status, status_context);
                collecting = false;
                length = 0;
                continue;
            }
            if (length + 1u >= sizeof(line)) {
                collecting = false;
                length = 0;
                continue;
            }
            line[length++] = c;
        }
    }
}

esp_err_t gps_unit_start(gps_status_callback_t callback, void *context)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_io_expander_handle_t expander = bsp_io_expander_init();
    if (expander == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(
        esp_io_expander_set_dir(expander, GPS_EXT5V_EN, IO_EXPANDER_OUTPUT), TAG,
        "Could not configure Grove 5 V enable");
    ESP_RETURN_ON_ERROR(
        esp_io_expander_set_level(expander, GPS_EXT5V_EN, 1), TAG,
        "Could not enable Grove 5 V supply");
    ESP_RETURN_ON_ERROR(
        esp_io_expander_set_output_mode(expander, GPS_EXT5V_EN,
                                        IO_EXPANDER_OUTPUT_MODE_PUSH_PULL), TAG,
        "Could not drive Grove 5 V enable");

    const uart_config_t config = {
        .baud_rate = GPS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_param_config(GPS_UART, &config), TAG,
                        "Could not configure GPS UART");
    ESP_RETURN_ON_ERROR(uart_set_pin(GPS_UART, GPS_UART_TX, GPS_UART_RX,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG,
                        "Could not route GPS UART");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(GPS_UART_RX, GPIO_PULLUP_ONLY), TAG,
                        "Could not enable GPS RX pull-up");
    ESP_RETURN_ON_ERROR(uart_driver_install(GPS_UART, GPS_UART_RX_BYTES, 0, 0,
                                            NULL, 0), TAG,
                        "Could not install GPS UART driver");

    status_callback = callback;
    status_context = context;
    if (xTaskCreate(gps_task, "viewer-gps", GPS_TASK_STACK_BYTES, NULL,
                    GPS_TASK_PRIORITY, NULL) != pdPASS) {
        uart_driver_delete(GPS_UART);
        status_callback = NULL;
        status_context = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "GPS unit listening on Grove UART at %d baud (other rates tried if no NMEA)",
             GPS_UART_BAUD);
    return ESP_OK;
}
