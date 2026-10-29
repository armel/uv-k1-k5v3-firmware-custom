/*
 * Standalone K1Viewer client for the M5Stack Tab5.
 *
 * Copyright 2026 Armel F4HWN
 * https://github.com/armel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "usb/cdc_acm_host.h"

#define K1_USB_VID             0x36B7u
#define K1_USB_PID             0xFFFFu
#define RADIO_WIDTH            128u
#define RADIO_HEIGHT           64u
#define RADIO_FRAME_BYTES      ((RADIO_WIDTH * RADIO_HEIGHT) / 8u)
#define DISPLAY_SCALE          10u
#define DISPLAY_WIDTH          (RADIO_WIDTH * DISPLAY_SCALE)
#define DISPLAY_HEIGHT         (RADIO_HEIGHT * DISPLAY_SCALE)
#define LCD_PIXEL_GAP          2u
#define LCD_PIXEL_SIZE         (DISPLAY_SCALE - LCD_PIXEL_GAP)
#define K1VIEWER_MAX_PAYLOAD   2048u
#define K1VIEWER_KEEPALIVE_MS  200u
#define FPS_SAMPLE_MS          1000u
#define USB_OPEN_TIMEOUT_MS    1000u
#define USB_TX_TIMEOUT_MS      100u
#define STATUS_X               20
#define STATUS_Y               -20
#define STATUS_MAX_CHARS       40
#define STATUS_CELL_WIDTH      16
#define STATUS_LABEL_WIDTH     (STATUS_MAX_CHARS * STATUS_CELL_WIDTH)
#define LED_RED_X              380
#define LED_GREEN_X            450
#define SLEEP_STATUS_X         530
#define SLEEP_STATUS_WIDTH     (5 * STATUS_CELL_WIDTH)
#define LED_INDICATOR_SIZE     48
#define LED_INDICATOR_Y        -8
#define LED_RED_ON_COLOR       0xFF3B30u
#define LED_RED_OFF_COLOR      0x451311u
#define LED_GREEN_ON_COLOR     0x34C759u
#define LED_GREEN_OFF_COLOR    0x103818u

enum {
    K1VIEWER_TYPE_SCREENSHOT = 0x01,
    K1VIEWER_TYPE_DIFF = 0x02,
    K1VIEWER_FLAG_DEEP_SLEEP = 1u << 0,
    K1VIEWER_FLAG_LED_RED = 1u << 1,
    K1VIEWER_FLAG_LED_GREEN = 1u << 2,
};

typedef enum {
    PARSER_SEEK,
    PARSER_HEADER_AA,
    PARSER_HEADER_55,
    PARSER_TYPE,
    PARSER_LENGTH_HIGH,
    PARSER_LENGTH_LOW,
    PARSER_PAYLOAD,
    PARSER_END,
} parser_state_t;

typedef enum {
    PALETTE_GREY,
    PALETTE_ORANGE,
    PALETTE_BLUE,
    PALETTE_COUNT,
} palette_id_t;

typedef struct {
    uint32_t foreground;
    uint32_t background;
} viewer_palette_t;

typedef struct {
    parser_state_t state;
    uint8_t flags;
    uint8_t type;
    uint16_t length;
    uint16_t position;
    uint8_t payload[K1VIEWER_MAX_PAYLOAD];
} viewer_parser_t;

typedef struct {
    uint8_t framebuffer[RADIO_FRAME_BYTES];
    uint8_t flags;
    uint8_t palette;
    uint32_t frame_count;
    bool usb_connected;
    bool frame_dirty;
} viewer_state_t;

static const char *TAG = "tab5-k1viewer";
static const viewer_palette_t palettes[PALETTE_COUNT] = {
    [PALETTE_GREY] = {.foreground = 0x2A213Fu, .background = 0xE5E0ECu},
    [PALETTE_ORANGE] = {.foreground = 0x41310Au, .background = 0xFFC125u},
    [PALETTE_BLUE] = {.foreground = 0x07223Au, .background = 0x1C86E4u},
};
static SemaphoreHandle_t state_mutex;
static SemaphoreHandle_t disconnected_sem;
static viewer_parser_t parser;
static viewer_state_t viewer;
static uint16_t *display_pixels;
static lv_obj_t *screen_canvas;
static lv_obj_t *status_label;
static lv_obj_t *sleep_status_label;
static lv_obj_t *led_red_indicator;
static lv_obj_t *led_green_indicator;
static lv_obj_t *palette_buttons[PALETTE_COUNT];

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(((uint16_t)(red & 0xF8u) << 8) |
                      ((uint16_t)(green & 0xFCu) << 3) |
                      ((uint16_t)blue >> 3));
}

static uint16_t rgb565_hex(uint32_t color)
{
    return rgb565((color >> 16) & 0xFFu,
                  (color >> 8) & 0xFFu,
                  color & 0xFFu);
}

static uint16_t inactive_pixel_color(const viewer_palette_t *palette)
{
    const uint8_t bg_red = (palette->background >> 16) & 0xFFu;
    const uint8_t bg_green = (palette->background >> 8) & 0xFFu;
    const uint8_t bg_blue = palette->background & 0xFFu;
    const uint8_t fg_red = (palette->foreground >> 16) & 0xFFu;
    const uint8_t fg_green = (palette->foreground >> 8) & 0xFFu;
    const uint8_t fg_blue = palette->foreground & 0xFFu;

    return rgb565((bg_red * 95u + fg_red * 5u + 50u) / 100u,
                  (bg_green * 95u + fg_green * 5u + 50u) / 100u,
                  (bg_blue * 95u + fg_blue * 5u + 50u) / 100u);
}

static void parser_reset(void)
{
    parser.state = PARSER_SEEK;
    parser.flags = 0;
    parser.type = 0;
    parser.length = 0;
    parser.position = 0;
}

static void viewer_apply_frame(void)
{
    bool valid = false;

    xSemaphoreTake(state_mutex, portMAX_DELAY);

    if (parser.type == K1VIEWER_TYPE_SCREENSHOT &&
        parser.length == RADIO_FRAME_BYTES) {
        memcpy(viewer.framebuffer, parser.payload, RADIO_FRAME_BYTES);
        valid = true;
    } else if (parser.type == K1VIEWER_TYPE_DIFF &&
               parser.length > 0 &&
               (parser.length % 9u) == 0) {
        valid = true;
        for (uint16_t offset = 0; offset < parser.length; offset += 9u) {
            if (parser.payload[offset] >= 128u) {
                valid = false;
                break;
            }
        }
        for (uint16_t offset = 0; valid && offset < parser.length; offset += 9u) {
            const uint8_t chunk = parser.payload[offset];
            memcpy(&viewer.framebuffer[(uint16_t)chunk * 8u],
                   &parser.payload[offset + 1u], 8u);
        }
    }

    if (valid) {
        viewer.flags = parser.flags;
        viewer.frame_count++;
        viewer.frame_dirty = true;
    }

    xSemaphoreGive(state_mutex);
}

static void parser_seek(uint8_t byte)
{
    if (byte == 0xFFu) {
        parser.flags = 0;
        parser.state = PARSER_HEADER_AA;
    } else if ((byte & 0xF0u) == 0xF0u) {
        parser.flags = byte & 0x0Fu;
        parser.state = PARSER_HEADER_AA;
    } else if (byte == 0xAAu) {
        parser.flags = 0;
        parser.state = PARSER_HEADER_55;
    }
}

static void parser_push(uint8_t byte)
{
    switch (parser.state) {
    case PARSER_SEEK:
        parser_seek(byte);
        break;

    case PARSER_HEADER_AA:
        if (byte == 0xAAu) {
            parser.state = PARSER_HEADER_55;
        } else {
            parser_reset();
            parser_seek(byte);
        }
        break;

    case PARSER_HEADER_55:
        if (byte == 0x55u) {
            parser.state = PARSER_TYPE;
        } else {
            parser_reset();
            parser_seek(byte);
        }
        break;

    case PARSER_TYPE:
        parser.type = byte;
        parser.state = PARSER_LENGTH_HIGH;
        break;

    case PARSER_LENGTH_HIGH:
        parser.length = (uint16_t)byte << 8;
        parser.state = PARSER_LENGTH_LOW;
        break;

    case PARSER_LENGTH_LOW:
        parser.length |= byte;
        parser.position = 0;
        if (parser.length > K1VIEWER_MAX_PAYLOAD) {
            parser_reset();
        } else if (parser.length == 0) {
            parser.state = PARSER_END;
        } else {
            parser.state = PARSER_PAYLOAD;
        }
        break;

    case PARSER_PAYLOAD:
        parser.payload[parser.position++] = byte;
        if (parser.position == parser.length) {
            parser.state = PARSER_END;
        }
        break;

    case PARSER_END:
        if (byte == 0x0Au) {
            viewer_apply_frame();
        }
        parser_reset();
        break;
    }
}

static bool usb_rx_callback(const uint8_t *data, size_t data_len, void *user_arg)
{
    (void)user_arg;

    for (size_t i = 0; i < data_len; i++) {
        parser_push(data[i]);
    }
    return true;
}

static void usb_event_callback(const cdc_acm_host_dev_event_data_t *event,
                               void *user_ctx)
{
    (void)user_ctx;

    switch (event->type) {
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        ESP_LOGI(TAG, "K1 disconnected");
        ESP_ERROR_CHECK_WITHOUT_ABORT(cdc_acm_host_close(event->data.cdc_hdl));
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        viewer.usb_connected = false;
        viewer.frame_dirty = true;
        xSemaphoreGive(state_mutex);
        xSemaphoreGive(disconnected_sem);
        break;

    case CDC_ACM_HOST_ERROR:
        ESP_LOGW(TAG, "CDC-ACM error: %d", event->data.error);
        break;

    case CDC_ACM_HOST_SERIAL_STATE:
        break;

    default:
        ESP_LOGD(TAG, "Unhandled CDC event: %d", event->type);
        break;
    }
}

static void render_radio_frame(const uint8_t *framebuffer, uint8_t palette_id)
{
    if (palette_id >= PALETTE_COUNT) {
        palette_id = PALETTE_GREY;
    }
    const viewer_palette_t *palette = &palettes[palette_id];
    const uint16_t foreground = rgb565_hex(palette->foreground);
    const uint16_t background = rgb565_hex(palette->background);
    const uint16_t inactive = inactive_pixel_color(palette);
    uint16_t scanline[DISPLAY_WIDTH];

    for (uint16_t y = 0; y < RADIO_HEIGHT; y++) {
        for (uint16_t x = 0; x < DISPLAY_WIDTH; x++) {
            scanline[x] = background;
        }

        for (uint16_t x = 0; x < RADIO_WIDTH; x++) {
            const uint16_t bit_index = y * RADIO_WIDTH + x;
            const bool pixel_on =
                (framebuffer[bit_index >> 3] & (1u << (bit_index & 7u))) != 0;
            const uint16_t color = pixel_on ? foreground : inactive;
            const uint16_t scaled_x = x * DISPLAY_SCALE;
            for (uint8_t sx = 0; sx < LCD_PIXEL_SIZE; sx++) {
                scanline[scaled_x + sx] = color;
            }
        }

        const uint16_t scaled_y = y * DISPLAY_SCALE;
        for (uint8_t sy = 0; sy < LCD_PIXEL_SIZE; sy++) {
            memcpy(&display_pixels[(scaled_y + sy) * DISPLAY_WIDTH],
                   scanline, sizeof(scanline));
        }

        for (uint16_t x = 0; x < DISPLAY_WIDTH; x++) {
            scanline[x] = background;
        }
        for (uint8_t sy = LCD_PIXEL_SIZE; sy < DISPLAY_SCALE; sy++) {
            memcpy(&display_pixels[(scaled_y + sy) * DISPLAY_WIDTH],
                   scanline, sizeof(scanline));
        }
    }
}

static void led_indicator_set(lv_obj_t *indicator, bool on,
                              uint32_t on_color, uint32_t off_color)
{
    lv_obj_set_style_bg_color(
        indicator, lv_color_hex(on ? on_color : off_color), 0);
    lv_obj_set_style_border_color(indicator, lv_color_hex(on_color), 0);
    lv_obj_set_style_border_opa(
        indicator, on ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_shadow_color(indicator, lv_color_hex(on_color), 0);
    lv_obj_set_style_shadow_width(indicator, on ? 12 : 0, 0);
    lv_obj_set_style_shadow_opa(
        indicator, on ? LV_OPA_50 : LV_OPA_TRANSP, 0);
}

static void render_task(void *arg)
{
    (void)arg;
    uint8_t framebuffer[RADIO_FRAME_BYTES] = {0};
    uint8_t flags = 0;
    uint8_t palette = PALETTE_GREY;
    uint32_t frame_count = 0;
    uint32_t fps_window_frame_count = 0;
    uint32_t fps = 0;
    TickType_t fps_window_start = xTaskGetTickCount();
    bool fps_connected = false;
    bool connected = false;
    bool redraw = true;
    char status[96];

    while (true) {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        if (viewer.frame_dirty) {
            memcpy(framebuffer, viewer.framebuffer, sizeof(framebuffer));
            flags = viewer.flags;
            palette = viewer.palette;
            frame_count = viewer.frame_count;
            connected = viewer.usb_connected;
            viewer.frame_dirty = false;
            redraw = true;
        }
        xSemaphoreGive(state_mutex);

        const TickType_t now = xTaskGetTickCount();
        if (connected != fps_connected) {
            fps_connected = connected;
            fps_window_start = now;
            fps_window_frame_count = frame_count;
            fps = 0;
            redraw = true;
        } else if (connected &&
                   now - fps_window_start >= pdMS_TO_TICKS(FPS_SAMPLE_MS)) {
            const TickType_t elapsed = now - fps_window_start;
            const uint32_t frames = frame_count - fps_window_frame_count;
            fps = (frames * configTICK_RATE_HZ + elapsed / 2u) / elapsed;
            if (fps > 99u) {
                fps = 99u;
            }
            fps_window_start = now;
            fps_window_frame_count = frame_count;
            redraw = true;
        }

        if (redraw) {
            render_radio_frame(framebuffer, palette);

            if (connected) {
                snprintf(status, sizeof(status),
                         "K1 CONNECTED  %02" PRIu32 " FPS", fps);
            } else {
                snprintf(status, sizeof(status),
                         "Connect K1 to the Tab5 USB-A port");
            }

            if (bsp_display_lock(1000)) {
                lv_obj_invalidate(screen_canvas);
                lv_label_set_text(status_label, status);
                if (connected) {
                    lv_obj_set_hidden(led_red_indicator, false);
                    lv_obj_set_hidden(led_green_indicator, false);
                    led_indicator_set(
                        led_red_indicator,
                        (flags & K1VIEWER_FLAG_LED_RED) != 0,
                        LED_RED_ON_COLOR, LED_RED_OFF_COLOR);
                    led_indicator_set(
                        led_green_indicator,
                        (flags & K1VIEWER_FLAG_LED_GREEN) != 0,
                        LED_GREEN_ON_COLOR, LED_GREEN_OFF_COLOR);
                    lv_label_set_text(
                        sleep_status_label,
                        (flags & K1VIEWER_FLAG_DEEP_SLEEP) ? "SLEEP" : "");
                } else {
                    lv_obj_set_hidden(led_red_indicator, true);
                    lv_obj_set_hidden(led_green_indicator, true);
                    lv_label_set_text(sleep_status_label, "");
                }
                bsp_display_unlock();
            }
            redraw = false;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void palette_buttons_update(uint8_t selected)
{
    for (uint8_t i = 0; i < PALETTE_COUNT; i++) {
        lv_obj_set_style_outline_width(palette_buttons[i],
                                       i == selected ? 4 : 0, 0);
    }
}

static void palette_event_callback(lv_event_t *event)
{
    const uint8_t palette = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (palette >= PALETTE_COUNT) {
        return;
    }

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    viewer.palette = palette;
    viewer.frame_dirty = true;
    xSemaphoreGive(state_mutex);

    palette_buttons_update(palette);
}

static void usb_task(void *arg)
{
    (void)arg;
    static const uint8_t keepalive[] = {0x55, 0xAA, 0x00, 0x00};
    const cdc_acm_host_device_config_t device_config = {
        .connection_timeout_ms = USB_OPEN_TIMEOUT_MS,
        .out_buffer_size = 64,
        .in_buffer_size = 2048,
        .user_arg = NULL,
        .event_cb = usb_event_callback,
        .data_cb = usb_rx_callback,
    };

    ESP_ERROR_CHECK(bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV, true));
    ESP_ERROR_CHECK(cdc_acm_host_install(NULL));

    while (true) {
        cdc_acm_dev_hdl_t device = NULL;
        ESP_LOGI(TAG, "Waiting for K1 USB CDC %04X:%04X", K1_USB_VID, K1_USB_PID);

        const esp_err_t open_result =
            cdc_acm_host_open(K1_USB_VID, K1_USB_PID, 0, &device_config, &device);
        if (open_result != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        parser_reset();
        xSemaphoreTake(disconnected_sem, 0);

        const cdc_acm_line_coding_t line_coding = {
            .dwDTERate = 38400,
            .bCharFormat = 0,
            .bParityType = 0,
            .bDataBits = 8,
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(cdc_acm_host_line_coding_set(device, &line_coding));
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            cdc_acm_host_set_control_line_state(device, true, false));

        xSemaphoreTake(state_mutex, portMAX_DELAY);
        viewer.usb_connected = true;
        viewer.frame_dirty = true;
        xSemaphoreGive(state_mutex);

        ESP_LOGI(TAG, "K1 connected");
        while (xSemaphoreTake(disconnected_sem, 0) != pdTRUE) {
            const esp_err_t tx_result = cdc_acm_host_data_tx_blocking(
                device, keepalive, sizeof(keepalive), USB_TX_TIMEOUT_MS);
            if (tx_result != ESP_OK && tx_result != ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Keepalive failed: %s", esp_err_to_name(tx_result));
            }
            vTaskDelay(pdMS_TO_TICKS(K1VIEWER_KEEPALIVE_MS));
        }
    }
}

static lv_obj_t *create_status_label(lv_obj_t *root, int32_t x, int32_t width)
{
    lv_obj_t *label = lv_label_create(root);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF0F4EE), 0);
    lv_obj_set_style_text_font(label, &lv_font_unscii_16, 0);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, x, STATUS_Y);
    return label;
}

static lv_obj_t *create_led_indicator(lv_obj_t *root, int32_t x,
                                      uint32_t on_color, uint32_t off_color)
{
    lv_obj_t *indicator = lv_obj_create(root);
    lv_obj_remove_style_all(indicator);
    lv_obj_set_size(indicator, LED_INDICATOR_SIZE, LED_INDICATOR_SIZE);
    lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(indicator, 3, 0);
    lv_obj_set_clickable(indicator, false);
    lv_obj_set_scrollable(indicator, false);
    lv_obj_align(indicator, LV_ALIGN_BOTTOM_LEFT, x, LED_INDICATOR_Y);
    led_indicator_set(indicator, false, on_color, off_color);
    lv_obj_set_hidden(indicator, true);
    return indicator;
}

static void display_init(void)
{
    lv_display_t *display = bsp_display_start();
    assert(display != NULL);

    bsp_display_rotate(display, LV_DISPLAY_ROTATION_90);
    ESP_ERROR_CHECK(bsp_display_backlight_on());

    display_pixels = heap_caps_calloc(
        DISPLAY_WIDTH * DISPLAY_HEIGHT, sizeof(*display_pixels),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(display_pixels != NULL);

    if (bsp_display_lock(0)) {
        lv_obj_t *root = lv_screen_active();
        lv_obj_set_style_bg_color(root, lv_color_hex(0x111411), 0);

        screen_canvas = lv_canvas_create(root);
        lv_canvas_set_buffer(screen_canvas, display_pixels,
                             DISPLAY_WIDTH, DISPLAY_HEIGHT,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_align(screen_canvas, LV_ALIGN_TOP_MID, 0, 0);

        status_label =
            create_status_label(root, STATUS_X, STATUS_LABEL_WIDTH);
        sleep_status_label =
            create_status_label(root, SLEEP_STATUS_X, SLEEP_STATUS_WIDTH);
        led_red_indicator = create_led_indicator(
            root, LED_RED_X, LED_RED_ON_COLOR, LED_RED_OFF_COLOR);
        led_green_indicator = create_led_indicator(
            root, LED_GREEN_X, LED_GREEN_ON_COLOR, LED_GREEN_OFF_COLOR);
        lv_label_set_text(status_label, "Starting USB...");
        lv_label_set_text(sleep_status_label, "");

        lv_obj_t *palette_bar = lv_obj_create(root);
        lv_obj_remove_style_all(palette_bar);
        lv_obj_set_size(palette_bar, 210, 64);
        lv_obj_set_flex_flow(palette_bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(palette_bar, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(palette_bar, LV_ALIGN_BOTTOM_RIGHT, -20, -8);

        for (uint8_t i = 0; i < PALETTE_COUNT; i++) {
            palette_buttons[i] = lv_button_create(palette_bar);
            lv_obj_set_size(palette_buttons[i], 48, 48);
            lv_obj_set_style_radius(palette_buttons[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(
                palette_buttons[i], lv_color_hex(palettes[i].background), 0);
            lv_obj_set_style_border_color(
                palette_buttons[i], lv_color_hex(palettes[i].foreground), 0);
            lv_obj_set_style_border_width(palette_buttons[i], 3, 0);
            lv_obj_set_style_outline_color(
                palette_buttons[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_outline_pad(palette_buttons[i], 2, 0);
            lv_obj_set_style_shadow_width(palette_buttons[i], 0, 0);
            lv_obj_add_event_cb(palette_buttons[i], palette_event_callback,
                                LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        }
        palette_buttons_update(PALETTE_GREY);

        bsp_display_unlock();
    }
}

void app_main(void)
{
    state_mutex = xSemaphoreCreateMutex();
    disconnected_sem = xSemaphoreCreateBinary();
    assert(state_mutex != NULL);
    assert(disconnected_sem != NULL);

    parser_reset();
    display_init();

    assert(xTaskCreate(render_task, "viewer-render", 8192, NULL, 5, NULL) == pdPASS);
    assert(xTaskCreate(usb_task, "viewer-usb", 6144, NULL, 8, NULL) == pdPASS);
}
