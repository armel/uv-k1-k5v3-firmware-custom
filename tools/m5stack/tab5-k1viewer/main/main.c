/*
 * Standalone K1Viewer client for the M5Stack Tab5.
 *
 * Copyright 2026 Armel F4HWN
 * https://github.com/armel
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmi270.h"
#include "bsp/esp-bsp.h"
#include "esp_app_desc.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gps.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pl2303_vcp.h"
#include "usb/cdc_acm_host.h"
#include "usb/vcp_ch34x.h"
#include "usb/vcp_cp210x.h"
#include "usb/vcp_ftdi.h"
#include "uv_k1_skin_layout.h"

#define K1_USB_VID             0x36B7u
#define K1_USB_PID             0xFFFFu
#define RADIO_WIDTH            128u
#define RADIO_HEIGHT           64u
#define RADIO_FRAME_BYTES      ((RADIO_WIDTH * RADIO_HEIGHT) / 8u)
#define K1VIEWER_MAX_PAYLOAD   2048u
#define K1VIEWER_KEEPALIVE_MS  200u
#define K1_UART_BAUD           38400u
#define USB_OPEN_ATTEMPT_MS    200u
#define USB_TX_TIMEOUT_MS      100u
#define AUTO_ROTATE            1            /* follow the Tab5 orientation with its IMU */
#define FIXED_ROTATION         LV_DISPLAY_ROTATION_0    /* used when AUTO_ROTATE is 0 */
/* Landscape rotation that puts the USB-A port on the left: it shows the screen
 * alone, the other landscape rotation shows the screen with the keypad. Swap
 * with LV_DISPLAY_ROTATION_90 if they come out the other way round. */
#define SCREEN_ROTATION        LV_DISPLAY_ROTATION_270
#define IMU_PERIOD_MS          100u
#define IMU_SETTLE_SAMPLES     5u           /* same orientation for 0.5 s before turning */
#define IMU_BOOT_PERIOD_MS     20u          /* at boot, the first clear reading decides */
#define IMU_BOOT_SAMPLES       15u          /* then the Tab5 is taken as lying flat */
#define BOOT_ORIENTATION_MS    1500u        /* longest wait for the IMU, backlight off */
#define IMU_TILT_G             0.6f         /* gravity along an edge needed to pick a side */
#define IMU_FLAT_G             0.8f         /* lying flat above this: keep the orientation */
/* Accelerometer axes along the long and short edges of the screen (0 = X,
 * 1 = Y, 2 = Z) and their sign when that edge points up. Flip a sign if the
 * picture turns the wrong way; swap the axes if portrait and landscape are
 * exchanged. Orientation changes are logged with the raw readings. */
#define IMU_LONG_AXIS          1
#define IMU_LONG_SIGN          1.0f
#define IMU_SHORT_AXIS         0
#define IMU_SHORT_SIGN         1.0f
#define KEY_LONG_PRESS_MS      500u
#define KEY_REPEAT_MS          120u
#define OUTBOUND_QUEUE_LENGTH  16u
#define KEY_CLICK              1            /* click on the Tab5 speaker for each key sent */
#define KEY_CLICK_VOLUME       70           /* 0 to 100: -50 dB to 0 dB */
#define KEY_CLICK_RATE         48000u
#define RENDER_PERIOD_MS       20u
#define BACKLIGHT_DIM_PERCENT  10           /* below the dimmest of backlight_levels */
#define BACKLIGHT_FADE_STEP    3            /* percent per render tick while dimming */
#define BACKLIGHT_IDLE_MS      120000u      /* no radio, no touch: dim after */
#define SETTINGS_NAMESPACE     "k1viewer"
#define SETTINGS_SAVE_DELAY_MS 1000u        /* save once the settings stop changing */
#define PLATE_RADIUS           8
/* Two taps on the plate this close make a double tap, wherever they land on
 * it; a single tap acts once this delay has passed. */
#define PLATE_DOUBLE_TAP_MS    400u
#define TOAST_PADDING          24           /* panel around the toast image, and its radius */
#define TOAST_BORDER_COLOR     0x2C3036u
#define TOAST_SHOW_MS          1200u
#define TOAST_FADE_MS          300u
#define LCD_INACTIVE_MIX       0.05f
#define LCD_SHADOW_MIX         0.18f
#define LCD_SHADOW_DY          2u           /* the shadow is also one pixel to the right */
#define LCD_SLEEP_COLOR        0x252925u    /* glass with the backlight off */
#define LCD_SLEEP_MIX          0.6f
#define LED_GLOW_ALPHA         0.6f
#define TORCH_COLOR            0xEAF3FFu    /* cool white LED light */
#define TORCH_HALO_ALPHA       0.9f
#define TORCH_BEAM_ALPHA       0.5f
#define CONNECT_FADE_MS        400u
#define CONNECT_PULSE_MS       1400u
#define CONNECT_PULSE_MIN_OPA  LV_OPA_40
#define CONNECT_VERSION_COLOR  0x5A5F66u    /* as the license line above it */
#define GPS_DEBUG_REFRESH_MS   250u
#define GPS_DATA_TIMEOUT_MS    3000u
#define GPS_SEARCH_BLINK_MS    500u
#define GPS_STATUS_SIZE        10
#define GPS_PANEL_WIDTH        390
#define GPS_PANEL_HEIGHT       390
#define GPS_PANEL_COLOR        SKIN_TOAST_COLOR
#define GPS_PANEL_BORDER_COLOR TOAST_BORDER_COLOR
#define GPS_PANEL_TEXT_COLOR   0xE8EDF2u
#define GPS_PANEL_LABEL_COLOR  0xA7AFB8u
#define GPS_FIX_COLOR          0x6BE585u
#define GPS_DATA_COLOR         0x3D93E6u
#define GPS_ERROR_COLOR        0xFF6B6Bu
#define BACKGROUND_COLOR       0x0C0D0Fu
#define KEY_PRESSED_COLOR      0xFFFFFFu
#define KEY_LONG_COLOR         0x3D93E6u
#define SIDE_KEY_GLOW          18

enum {
    K1VIEWER_TYPE_SCREENSHOT = 0x01,
    K1VIEWER_TYPE_DIFF = 0x02,
    K1VIEWER_TYPE_KEY = 0x03,
    K1VIEWER_TYPE_KEY_LONG = 0x04,
    K1VIEWER_TYPE_GPS = 0x07,
    K1VIEWER_FLAG_DEEP_SLEEP = 1u << 0,
    K1VIEWER_FLAG_LED_RED = 1u << 1,
    K1VIEWER_FLAG_LED_GREEN = 1u << 2,
    K1VIEWER_FLAG_FLASHLIGHT = 1u << 3,
};

/* Firmware key codes of the '<' and '>' keys, which auto-repeat when held. */
enum {
    RADIO_KEY_UP = 0x0B,
    RADIO_KEY_DOWN = 0x0C,
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
    PALETTE_K1,
    PALETTE_GREY,
    PALETTE_ORANGE,
    PALETTE_BLUE,
    PALETTE_WHITE,
    PALETTE_OLIVE,
    PALETTE_COUNT,
} palette_id_t;

typedef enum {
    LED_GREEN,
    LED_RED,
    LED_COUNT,
} led_id_t;

typedef struct {
    uint32_t foreground;
    uint32_t background;
} viewer_palette_t;

/* RGB565 colours of the radio LCD for one palette and backlight state. */
typedef struct {
    uint16_t glass;
    uint16_t inactive;
    uint16_t shadow;
    uint16_t foreground;
} lcd_colors_t;

typedef struct {
    uint32_t core;
    uint32_t center;
} led_colors_t;

typedef struct {
    parser_state_t state;
    uint8_t flags;
    uint8_t type;
    uint16_t length;
    uint16_t position;
    uint8_t payload[K1VIEWER_MAX_PAYLOAD];
} viewer_parser_t;

/* Display settings, kept in NVS across restarts. */
typedef struct {
    uint8_t palette;
    uint8_t brightness;         /* index in backlight_levels */
    bool invert;
    bool key_click;             /* click on the Tab5 speaker for each key sent */
} settings_t;

typedef struct {
    uint8_t framebuffer[RADIO_FRAME_BYTES];
    uint64_t dirty_rows;        /* bit n: LCD row n changed since the last render */
    uint8_t flags;
    settings_t settings;
    bool rotation_locked;       /* the IMU no longer turns the display */
    uint32_t frame_count;
    bool usb_connected;
    bool frame_dirty;
    lv_display_rotation_t rotation;
    gps_status_t gps;
    TickType_t gps_last_data_tick;
    TickType_t gps_last_sentence_tick;
    bool gps_started;
    bool gps_error;
} viewer_state_t;

typedef struct {
    uint8_t code;
    bool long_press;
} radio_key_t;

typedef enum {
    OUTBOUND_KEY,
    OUTBOUND_GPS,
} outbound_type_t;

typedef struct {
    outbound_type_t type;
    union {
        radio_key_t key;
        gps_fix_t gps;
    } data;
} outbound_message_t;

typedef enum {
    VIEW_PORTRAIT,
    VIEW_LANDSCAPE,             /* large screen and keypad */
    VIEW_SCREEN,                /* screen only */
    VIEW_COUNT,
} view_id_t;

/* One LVGL screen per orientation, built from its skin layout. */
typedef struct {
    const skin_layout_t *layout;
    const char *name;
    const uint8_t *skin_start;
    const uint8_t *skin_end;
    lv_image_dsc_t skin;
    lv_obj_t *screen;
    lv_obj_t *lcd_canvas;
    uint16_t *lcd_pixels;
    uint32_t lcd_stride;        /* in pixels */
    lv_obj_t *led_canvases[LED_COUNT];
    lv_obj_t *torch_canvas;
    lv_obj_t *connect_screen;
    lv_obj_t *gps_button;
    lv_obj_t *gps_status_dot;
    lv_obj_t *gps_debug_panel;
    lv_obj_t *gps_debug_label;
} view_t;

static const char *TAG = "tab5-k1viewer";
static const viewer_palette_t palettes[PALETTE_COUNT] = {
    [PALETTE_K1] = {.foreground = 0x17191Bu, .background = 0xE2E6DFu},
    [PALETTE_GREY] = {.foreground = 0x2A213Fu, .background = 0xE5E0ECu},
    [PALETTE_ORANGE] = {.foreground = 0x41310Au, .background = 0xFFC125u},
    [PALETTE_BLUE] = {.foreground = 0x07223Au, .background = 0x1C86E4u},
    [PALETTE_WHITE] = {.foreground = 0x000000u, .background = 0xFFFFFFu},
    [PALETTE_OLIVE] = {.foreground = 0x000000u, .background = 0x394D2Du},
};
static const led_colors_t led_colors[LED_COUNT] = {
    [LED_GREEN] = {.core = 0x16C94Au, .center = 0xEAFFEFu},
    [LED_RED] = {.core = 0xE5281Eu, .center = 0xFFE3DFu},
};
/* Backlight levels picked with the plate, brightest first: one toast image each. */
static const int backlight_levels[SKIN_TOAST_BRIGHTNESS_LEVELS] = {100, 75, 50, 25};

/* PNGs from assets/ as raw RGB565, linked by main/CMakeLists.txt. */
extern const uint8_t uv_k1_skin_rgb565_start[];
extern const uint8_t uv_k1_skin_rgb565_end[];
extern const uint8_t uv_k1_landscape_skin_rgb565_start[];
extern const uint8_t uv_k1_landscape_skin_rgb565_end[];
extern const uint8_t uv_k1_screen_skin_rgb565_start[];
extern const uint8_t uv_k1_screen_skin_rgb565_end[];
extern const uint8_t k1_connect_icon_rgb565_start[];
extern const uint8_t k1_connect_icon_rgb565_end[];
extern const uint8_t k1_connect_icon_kplug_rgb565_start[];
extern const uint8_t k1_connect_icon_kplug_rgb565_end[];
extern const uint8_t k1_connect_text_rgb565_start[];
extern const uint8_t k1_connect_text_rgb565_end[];
extern const uint8_t k1_connect_text_kplug_rgb565_start[];
extern const uint8_t k1_connect_text_kplug_rgb565_end[];
extern const uint8_t k1_connect_credit_rgb565_start[];
extern const uint8_t k1_connect_credit_rgb565_end[];
extern const uint8_t k1_toast_locked_rgb565_start[];
extern const uint8_t k1_toast_locked_rgb565_end[];
extern const uint8_t k1_toast_unlocked_rgb565_start[];
extern const uint8_t k1_toast_unlocked_rgb565_end[];
extern const uint8_t k1_toast_beep_on_rgb565_start[];
extern const uint8_t k1_toast_beep_on_rgb565_end[];
extern const uint8_t k1_toast_beep_off_rgb565_start[];
extern const uint8_t k1_toast_beep_off_rgb565_end[];
extern const uint8_t k1_toast_brightness_0_rgb565_start[];
extern const uint8_t k1_toast_brightness_0_rgb565_end[];
extern const uint8_t k1_toast_brightness_1_rgb565_start[];
extern const uint8_t k1_toast_brightness_1_rgb565_end[];
extern const uint8_t k1_toast_brightness_2_rgb565_start[];
extern const uint8_t k1_toast_brightness_2_rgb565_end[];
extern const uint8_t k1_toast_brightness_3_rgb565_start[];
extern const uint8_t k1_toast_brightness_3_rgb565_end[];

static SemaphoreHandle_t state_mutex;
static SemaphoreHandle_t disconnected_sem;
static SemaphoreHandle_t orientation_ready;
static QueueHandle_t outbound_queue;
static nvs_handle_t settings_nvs;
static bool settings_ready;
#if KEY_CLICK
static TaskHandle_t volatile click_task_handle;    /* set once the speaker is ready */
#endif
static viewer_parser_t parser;
static viewer_state_t viewer;
static lv_display_t *display;
static lv_image_dsc_t connect_icon_image;                   /* USB-C cable */
static lv_image_dsc_t connect_icon_kplug_image;             /* K-plug cable */
static lv_image_dsc_t connect_text_image;
static lv_image_dsc_t connect_text_kplug_image;
static lv_image_dsc_t connect_credit_image;
static lv_image_dsc_t toast_lock_images[2];                 /* unlocked, locked */
static lv_image_dsc_t toast_beep_images[2];                 /* off, on */
static lv_image_dsc_t toast_brightness_images[SKIN_TOAST_BRIGHTNESS_LEVELS];
static lv_obj_t *toast;
static lv_obj_t *toast_image;
static lv_timer_t *plate_tap_timer;
static bool plate_tap_pending;          /* a first tap waits for a second one */
static bool gps_debug_visible;
static view_t views[VIEW_COUNT] = {
    [VIEW_PORTRAIT] = {
        .layout = &skin_portrait,
        .name = "portrait skin",
        .skin_start = uv_k1_skin_rgb565_start,
        .skin_end = uv_k1_skin_rgb565_end,
    },
    [VIEW_LANDSCAPE] = {
        .layout = &skin_landscape,
        .name = "landscape skin",
        .skin_start = uv_k1_landscape_skin_rgb565_start,
        .skin_end = uv_k1_landscape_skin_rgb565_end,
    },
    [VIEW_SCREEN] = {
        .layout = &skin_screen,
        .name = "screen skin",
        .skin_start = uv_k1_screen_skin_rgb565_start,
        .skin_end = uv_k1_screen_skin_rgb565_end,
    },
};

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

static float clampf(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

static uint32_t mix_hex(uint32_t from, uint32_t to, float amount)
{
    uint32_t result = 0;

    for (uint32_t shift = 0; shift <= 16u; shift += 8u) {
        const float a = (float)((from >> shift) & 0xFFu);
        const float b = (float)((to >> shift) & 0xFFu);
        result |= (uint32_t)lroundf(a + (b - a) * amount) << shift;
    }
    return result;
}

static void parser_reset(void)
{
    parser.state = PARSER_SEEK;
    parser.flags = 0;
    parser.type = 0;
    parser.length = 0;
    parser.position = 0;
}

/* Apply a received frame. A screenshot changes every LCD row; a diff is a list
 * of 9-byte chunks, an index then 8 bytes, each chunk being half an LCD row. */
static void viewer_apply_frame(void)
{
    bool valid = false;

    xSemaphoreTake(state_mutex, portMAX_DELAY);

    if (parser.type == K1VIEWER_TYPE_SCREENSHOT &&
        parser.length == RADIO_FRAME_BYTES) {
        memcpy(viewer.framebuffer, parser.payload, RADIO_FRAME_BYTES);
        viewer.dirty_rows = UINT64_MAX;
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
            viewer.dirty_rows |= 1ull << (chunk >> 1);
        }
    }

    if (valid) {
        viewer.usb_connected = true;
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
        memset(viewer.framebuffer, 0, sizeof(viewer.framebuffer));
        viewer.dirty_rows = UINT64_MAX;
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

static bool radio_pixel(const uint8_t *framebuffer, uint32_t x, uint32_t y)
{
    const uint32_t bit_index = y * RADIO_WIDTH + x;

    return (framebuffer[bit_index >> 3] & (1u << (bit_index & 7u))) != 0;
}

/* LCD colours of a palette, ink and paper swapped when inverted. In deep sleep
 * the glass darkens, as when the radio turns its backlight off. */
static lcd_colors_t lcd_colors(uint8_t palette_id, bool invert, bool asleep)
{
    if (palette_id >= PALETTE_COUNT) {
        palette_id = PALETTE_K1;
    }
    const viewer_palette_t *palette = &palettes[palette_id];
    const uint32_t ink = invert ? palette->background : palette->foreground;
    const uint32_t paper = invert ? palette->foreground : palette->background;
    const uint32_t background = asleep ? mix_hex(paper, LCD_SLEEP_COLOR, LCD_SLEEP_MIX) : paper;
    const uint32_t inactive = mix_hex(background, ink, LCD_INACTIVE_MIX);

    return (lcd_colors_t){
        .glass = rgb565_hex(background),
        .inactive = rgb565_hex(inactive),
        .shadow = rgb565_hex(mix_hex(inactive, ink, LCD_SHADOW_MIX)),
        .foreground = rgb565_hex(ink),
    };
}

/* Mark screen rows first to last of the LCD of a view for redraw. */
static void lcd_invalidate_rows(const view_t *view, int32_t first, int32_t last)
{
    const skin_layout_t *layout = view->layout;
    const int32_t x = layout->glass.x + layout->lcd.x;
    const int32_t y = layout->glass.y + layout->lcd.y;
    const lv_area_t area = {
        .x1 = x,
        .y1 = y + first,
        .x2 = x + layout->lcd.w - 1,
        .y2 = y + last,
    };

    lv_obj_invalidate_area(view->lcd_canvas, &area);
}

/* Draw the radio LCD into the glass of a view: each LCD pixel is lcd_cell_w
 * screen pixels wide and 1.5 times as tall (the real K1 aspect), with a soft
 * drop shadow one pixel right and LCD_SHADOW_DY down. Only the LCD rows set in
 * rows (bit n: radio row n) are drawn and marked for redraw, unless full also
 * repaints the glass and redraws the whole canvas. */
static void render_radio_frame(const view_t *view, const uint8_t *framebuffer,
                               const lcd_colors_t *colors, uint64_t rows, bool full)
{
    const skin_layout_t *layout = view->layout;
    const skin_rect_t *glass = &layout->glass;
    const skin_rect_t *lcd = &layout->lcd;
    int32_t run_start = -1;

    if (full) {
        for (int32_t y = 0; y < glass->h; y++) {
            uint16_t *row = &view->lcd_pixels[y * view->lcd_stride];
            for (int32_t x = 0; x < glass->w; x++) {
                row[x] = colors->glass;
            }
        }
        rows = UINT64_MAX;
    }
    /* The shadow of a row falls on the top of the next one. */
    rows |= rows << 1;

    for (int32_t sy = 0; sy < lcd->h; sy++) {
        const uint32_t ly = (uint32_t)sy * layout->lcd_row_num / layout->lcd_row_den;

        if (((rows >> ly) & 1u) == 0) {
            if (run_start >= 0 && !full) {
                lcd_invalidate_rows(view, run_start, sy - 1);
            }
            run_start = -1;
            continue;
        }
        if (run_start < 0) {
            run_start = sy;
        }

        const bool shadow_row = sy >= (int32_t)LCD_SHADOW_DY;
        const uint32_t shadow_ly = shadow_row
            ? (uint32_t)(sy - LCD_SHADOW_DY) * layout->lcd_row_num / layout->lcd_row_den
            : 0;
        uint16_t *out = &view->lcd_pixels[(lcd->y + sy) * view->lcd_stride + lcd->x];

        for (uint32_t lx = 0; lx < RADIO_WIDTH; lx++) {
            const bool lit = radio_pixel(framebuffer, lx, ly);
            /* The shadow of a screen pixel comes from the one on its left: in
             * the previous LCD column for the first pixel of a cell. */
            const bool shadow_here = shadow_row && radio_pixel(framebuffer, lx, shadow_ly);
            const bool shadow_left = shadow_row && lx > 0 &&
                                     radio_pixel(framebuffer, lx - 1u, shadow_ly);

            for (uint32_t c = 0; c < layout->lcd_cell_w; c++) {
                *out++ = lit ? colors->foreground
                       : ((c == 0 ? shadow_left : shadow_here) ? colors->shadow : colors->inactive);
            }
        }
    }

    if (full) {
        lv_obj_invalidate(view->lcd_canvas);
    } else if (run_start >= 0) {
        lcd_invalidate_rows(view, run_start, lcd->h - 1);
    }
}

/* Draw the lit RX/TX light pipe (trapezoid) with its glow, as ARGB8888. A
 * vertical pipe is the same trapezoid turned a quarter turn counter-clockwise
 * about its centre. */
static void led_render(const skin_led_t *led, uint32_t *pixels, uint32_t stride,
                       const led_colors_t *colors)
{
    const float height = led->bottom_y - led->top_y;
    const float half_width = (led->bottom_x1 - led->bottom_x0) / 2.0f;
    const float center_x = (led->top_x0 + led->top_x1 + led->bottom_x0 + led->bottom_x1) / 4.0f;
    const float center_y = led->top_y + height * 0.4f;
    const float pivot_y = (led->top_y + led->bottom_y) / 2.0f;

    for (int32_t y = 0; y < led->box.h; y++) {
        for (int32_t x = 0; x < led->box.w; x++) {
            /* Screen pixel, brought back into the unturned trapezoid. */
            const float sx = (float)(led->box.x + x) + 0.5f;
            const float sy = (float)(led->box.y + y) + 0.5f;
            const float px = led->vertical ? center_x - (sy - pivot_y) : sx;
            const float py = led->vertical ? pivot_y + (sx - center_x) : sy;
            const float t = clampf((py - led->top_y) / height, 0.0f, 1.0f);
            const float left = led->top_x0 + (led->bottom_x0 - led->top_x0) * t;
            const float right = led->top_x1 + (led->bottom_x1 - led->top_x1) * t;
            const float dx = fmaxf(fmaxf(left - px, px - right), 0.0f);
            const float dy = fmaxf(fmaxf(led->top_y - py, py - led->bottom_y), 0.0f);
            const float inside = fminf(fminf(px - left, right - px),
                                       fminf(py - led->top_y, led->bottom_y - py));
            const float coverage = clampf(inside + 0.5f, 0.0f, 1.0f);
            const float fade = clampf(1.0f - sqrtf(dx * dx + dy * dy) / led->glow, 0.0f, 1.0f);
            const float glow = LED_GLOW_ALPHA * fade * fade;
            const float alpha = coverage + (1.0f - coverage) * glow;
            const float distance = hypotf((px - center_x) / half_width, (py - center_y) / height);
            uint32_t core = mix_hex(colors->center, colors->core, clampf(distance, 0.0f, 1.0f));

            if (py - led->top_y > 1.5f && py - led->top_y < 3.5f &&
                px > left + 2.5f && px < right - 2.5f) {
                core = mix_hex(core, 0xFFFFFFu, 0.6f);
            }

            /* The light pipe sits over its own glow, which uses the core colour. */
            const uint32_t color =
                alpha > 0.0f ? mix_hex(colors->core, core, coverage / alpha) : 0;
            pixels[y * stride + x] =
                ((uint32_t)lroundf(alpha * 255.0f) << 24) | (color & 0xFFFFFFu);
        }
    }
}

/* Draw the lit torch as ARGB8888: the visible part of the lens, a halo, and,
 * unless the layout turns it off, a soft beam fanning out up to the top of the
 * screen. */
static void torch_render(const skin_torch_t *torch, uint32_t *pixels, uint32_t stride)
{
    const float halo_rx = torch->rx + torch->halo;
    const float halo_ry = torch->ry + torch->halo;

    for (int32_t y = 0; y < torch->box.h; y++) {
        const float py = (float)(torch->box.y + y) + 0.5f;
        const float dy = py - torch->y;

        for (int32_t x = 0; x < torch->box.w; x++) {
            const float px = (float)(torch->box.x + x) + 0.5f;
            const float dx = px - torch->x;

            /* Lens, cut by its housing edge when the skin hides part of it. */
            const float lens = sqrtf((dx * dx) / (torch->rx * torch->rx) +
                                     (dy * dy) / (torch->ry * torch->ry));
            const float core = clampf((1.0f - lens) * fminf(torch->rx, torch->ry) + 0.5f, 0.0f, 1.0f) *
                               clampf(torch->top - py + 0.5f, 0.0f, 1.0f);

            const float halo_fade = clampf(1.0f - sqrtf((dx * dx) / (halo_rx * halo_rx) +
                                                        (dy * dy) / (halo_ry * halo_ry)),
                                           0.0f, 1.0f);
            const float halo = TORCH_HALO_ALPHA * halo_fade * halo_fade;

            float beam = 0.0f;
            if (dy < 0.0f && torch->beam > 0.0f) {
                const float rise = -dy;
                const float across = clampf(1.0f - fabsf(dx) / (torch->rx + rise * torch->beam_spread),
                                            0.0f, 1.0f);
                beam = torch->beam * TORCH_BEAM_ALPHA * (1.0f - rise / torch->y) * across * across;
            }

            const float alpha = 1.0f - (1.0f - core) * (1.0f - halo) * (1.0f - beam);
            pixels[y * stride + x] = ((uint32_t)lroundf(alpha * 255.0f) << 24) |
                                     mix_hex(TORCH_COLOR, 0xFFFFFFu, core);
        }
    }
}

#if KEY_CLICK
enum {
    CLICK_SHORT = 1,
    CLICK_LONG = 2,
};

/* Sine burst with a 1 ms attack, an exponential decay and a 1 ms release, as
 * interleaved 16-bit stereo samples. */
static int16_t *click_create(float frequency, uint32_t duration_ms, float decay_ms, size_t *bytes)
{
    const uint32_t frames = KEY_CLICK_RATE * duration_ms / 1000u;
    const float edge = (float)KEY_CLICK_RATE / 1000.0f;
    int16_t *samples = malloc(frames * 2u * sizeof(int16_t));

    *bytes = frames * 2u * sizeof(int16_t);
    if (samples == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < frames; i++) {
        const float t = (float)i / (float)KEY_CLICK_RATE;
        const float envelope = fminf(fminf((float)i / edge, (float)(frames - i) / edge), 1.0f) *
                               expf(-t * 1000.0f / decay_ms);
        const int16_t value =
            (int16_t)lroundf(16000.0f * envelope * sinf(6.2831853f * frequency * t));
        samples[2u * i] = value;
        samples[2u * i + 1u] = value;
    }
    return samples;
}

/* Play the key clicks on the Tab5 speaker, away from the LVGL task: a short
 * tick for a key, a lower and longer one for a long press. */
static void click_task(void *arg)
{
    (void)arg;
    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = 16,
        .channel = 2,
        .sample_rate = KEY_CLICK_RATE,
    };
    size_t bytes[2];
    int16_t *sounds[2] = {
        click_create(2400.0f, 15u, 4.0f, &bytes[0]),
        click_create(1600.0f, 60u, 18.0f, &bytes[1]),
    };
    esp_codec_dev_handle_t speaker = bsp_audio_codec_speaker_init();

    if (speaker == NULL || sounds[0] == NULL || sounds[1] == NULL ||
        esp_codec_dev_open(speaker, &format) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Speaker not available, the keys stay silent");
        free(sounds[0]);
        free(sounds[1]);
        vTaskDelete(NULL);
        return;
    }
    esp_codec_dev_set_out_vol(speaker, KEY_CLICK_VOLUME);
    click_task_handle = xTaskGetCurrentTaskHandle();

    while (true) {
        uint32_t sound = 0;
        xTaskNotifyWait(0, UINT32_MAX, &sound, portMAX_DELAY);
        if (sound == CLICK_SHORT || sound == CLICK_LONG) {
            esp_codec_dev_write(speaker, sounds[sound - 1u], (int)bytes[sound - 1u]);
        }
    }
}
#endif

#if KEY_CLICK
static void click_play(bool long_press)
{
    TaskHandle_t click = click_task_handle;
    if (click != NULL) {
        xTaskNotify(click, long_press ? CLICK_LONG : CLICK_SHORT, eSetValueWithOverwrite);
    }
}
#endif

static void radio_key_send(uint8_t code, bool long_press)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    const bool connected = viewer.usb_connected;
    const bool key_click = viewer.settings.key_click;
    xSemaphoreGive(state_mutex);

    if (!connected) {
        return;
    }

    const outbound_message_t message = {
        .type = OUTBOUND_KEY,
        .data.key = {.code = code, .long_press = long_press},
    };
    if (xQueueSend(outbound_queue, &message, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Key queue full, key 0x%02X dropped", code);
        return;
    }
#if KEY_CLICK
    if (key_click) {
        click_play(long_press);
    }
#else
    (void)key_click;
#endif
}

static void gps_status_received(const gps_status_t *status, void *context)
{
    (void)context;
    const TickType_t now = xTaskGetTickCount();

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    const bool new_fix = status->fix_count != viewer.gps.fix_count;
    if (status->bytes_received != viewer.gps.bytes_received) {
        viewer.gps_last_data_tick = now;
    }
    if (status->valid_sentences != viewer.gps.valid_sentences) {
        viewer.gps_last_sentence_tick = now;
    }
    viewer.gps = *status;
    xSemaphoreGive(state_mutex);

    if (!new_fix) {
        return;
    }
    const outbound_message_t message = {
        .type = OUTBOUND_GPS,
        .data.gps = status->fix,
    };
    /* A newer fix will arrive soon, so a full queue needs no retry. */
    xQueueSend(outbound_queue, &message, 0);
}

/*
 * Touch keys, mirroring the radio (keypad and F1/F2 side keys):
 * - '<' and '>' step on press, then repeat every KEY_REPEAT_MS once held
 *   for KEY_LONG_PRESS_MS;
 * - other keys send a short press on release, or a long press as soon as
 *   they have been held for KEY_LONG_PRESS_MS (the key turns blue).
 * USER_1 marks a key whose long press was sent, USER_2 a key whose press
 * was lost: neither sends a short press on release.
 */
static void key_event_callback(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *target = lv_event_get_current_target_obj(event);
    const uint8_t key = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    const bool repeat = key == RADIO_KEY_UP || key == RADIO_KEY_DOWN;

    switch (code) {
    case LV_EVENT_PRESSED:
        lv_obj_remove_state(target, LV_STATE_USER_1 | LV_STATE_USER_2);
        if (repeat) {
            radio_key_send(key, false);
        }
        break;

    case LV_EVENT_LONG_PRESSED:
        if (repeat) {
            radio_key_send(key, false);
        } else {
            radio_key_send(key, true);
            lv_obj_add_state(target, LV_STATE_USER_1);
        }
        break;

    case LV_EVENT_LONG_PRESSED_REPEAT:
        if (repeat) {
            radio_key_send(key, false);
        }
        break;

    case LV_EVENT_PRESS_LOST:
        lv_obj_add_state(target, LV_STATE_USER_2);
        break;

    case LV_EVENT_RELEASED:
        if (!repeat && !lv_obj_has_state(target, LV_STATE_USER_1) &&
            !lv_obj_has_state(target, LV_STATE_USER_2)) {
            radio_key_send(key, false);
        }
        lv_obj_remove_state(target, LV_STATE_USER_1 | LV_STATE_USER_2);
        break;

    default:
        break;
    }
}

static void connect_screen_opa_cb(void *obj, int32_t value)
{
    lv_obj_set_style_opa(obj, (lv_opa_t)value, 0);
}

static void connect_screen_faded_out(lv_anim_t *anim)
{
    (void)anim;
    for (uint8_t i = 0; i < VIEW_COUNT; i++) {
        if (lv_obj_get_style_opa(views[i].connect_screen, 0) == LV_OPA_TRANSP) {
            lv_obj_set_hidden(views[i].connect_screen, true);
        }
    }
}

static void connect_icon_pulse_cb(void *obj, int32_t value)
{
    lv_obj_set_style_image_opa(obj, (lv_opa_t)value, 0);
}

static void connect_pulse_start(lv_obj_t *icon, lv_obj_t *text_kplug, bool dim);

/* The icon has dimmed: the cable changes here, where the swap blends into the
 * breathing, and the K-plug message fades in or out over the USB-C one. Both
 * messages share their title, so only the cable name seems to change. */
static void connect_pulse_dimmed(lv_anim_t *anim)
{
    lv_obj_t *icon = anim->var;
    lv_obj_t *text_kplug = lv_anim_get_user_data(anim);

    if (connect_icon_kplug_image.data != NULL) {
        const bool to_kplug = lv_image_get_src(icon) != &connect_icon_kplug_image;
        lv_image_set_src(icon, to_kplug ? &connect_icon_kplug_image : &connect_icon_image);

        if (text_kplug != NULL) {
            lv_anim_t fade;
            lv_anim_init(&fade);
            lv_anim_set_var(&fade, text_kplug);
            lv_anim_set_exec_cb(&fade, connect_screen_opa_cb);
            lv_anim_set_values(&fade, lv_obj_get_style_opa(text_kplug, 0),
                               to_kplug ? LV_OPA_COVER : LV_OPA_TRANSP);
            lv_anim_set_duration(&fade, CONNECT_FADE_MS);
            lv_anim_set_path_cb(&fade, lv_anim_path_ease_in_out);
            lv_anim_start(&fade);
        }
    }
    connect_pulse_start(icon, text_kplug, false);
}

static void connect_pulse_brightened(lv_anim_t *anim)
{
    connect_pulse_start(anim->var, lv_anim_get_user_data(anim), true);
}

/* Slow breathing of the icon while waiting for the radio, one half at a time
 * (down to CONNECT_PULSE_MIN_OPA, then back up) so that the cable can change
 * at the dim point of every breath. */
static void connect_pulse_start(lv_obj_t *icon, lv_obj_t *text_kplug, bool dim)
{
    lv_anim_t pulse;
    lv_anim_init(&pulse);
    lv_anim_set_var(&pulse, icon);
    lv_anim_set_user_data(&pulse, text_kplug);
    lv_anim_set_exec_cb(&pulse, connect_icon_pulse_cb);
    lv_anim_set_values(&pulse, dim ? LV_OPA_COVER : CONNECT_PULSE_MIN_OPA,
                       dim ? CONNECT_PULSE_MIN_OPA : LV_OPA_COVER);
    lv_anim_set_duration(&pulse, CONNECT_PULSE_MS);
    lv_anim_set_path_cb(&pulse, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&pulse, dim ? connect_pulse_dimmed : connect_pulse_brightened);
    lv_anim_start(&pulse);
}

/* Fade the black "connect the radio" screens in or out over the radio, in
 * every view. Once faded out they are hidden, so they no longer catch
 * the touches. */
static void connect_screen_show(bool show)
{
    for (uint8_t i = 0; i < VIEW_COUNT; i++) {
        lv_obj_t *screen = views[i].connect_screen;
        lv_anim_t anim;

        lv_anim_delete(screen, connect_screen_opa_cb);
        lv_obj_set_hidden(screen, false);

        lv_anim_init(&anim);
        lv_anim_set_var(&anim, screen);
        lv_anim_set_exec_cb(&anim, connect_screen_opa_cb);
        lv_anim_set_values(&anim, lv_obj_get_style_opa(screen, 0),
                           show ? LV_OPA_COVER : LV_OPA_TRANSP);
        lv_anim_set_duration(&anim, CONNECT_FADE_MS);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
        if (!show) {
            lv_anim_set_completed_cb(&anim, connect_screen_faded_out);
        }
        lv_anim_start(&anim);
    }
}

/* Portrait shows the whole radio; landscape shows the screen alone with the
 * USB-A port on the left (SCREEN_ROTATION), the screen and keypad otherwise. */
static view_t *view_for_rotation(lv_display_rotation_t rotation)
{
    if (rotation == SCREEN_ROTATION) {
        return &views[VIEW_SCREEN];
    }
    if (rotation == LV_DISPLAY_ROTATION_90 || rotation == LV_DISPLAY_ROTATION_270) {
        return &views[VIEW_LANDSCAPE];
    }
    return &views[VIEW_PORTRAIT];
}

static settings_t saved_settings = {.palette = PALETTE_K1, .key_click = true};

static void settings_init(void)
{
    esp_err_t result = nvs_flash_init();

    viewer.settings = saved_settings;
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        result = nvs_flash_init();
    }
    if (result == ESP_OK) {
        result = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &settings_nvs);
    }
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Settings storage not available: %s", esp_err_to_name(result));
        return;
    }
    settings_ready = true;

    uint8_t value;
    if (nvs_get_u8(settings_nvs, "palette", &value) == ESP_OK && value < PALETTE_COUNT) {
        saved_settings.palette = value;
    }
    if (nvs_get_u8(settings_nvs, "bright", &value) == ESP_OK &&
        value < SKIN_TOAST_BRIGHTNESS_LEVELS) {
        saved_settings.brightness = value;
    }
    if (nvs_get_u8(settings_nvs, "invert", &value) == ESP_OK) {
        saved_settings.invert = value != 0;
    }
    if (nvs_get_u8(settings_nvs, "click", &value) == ESP_OK) {
        saved_settings.key_click = value != 0;
    }
    viewer.settings = saved_settings;
}

/* Save the settings once they have stayed the same for SETTINGS_SAVE_DELAY_MS,
 * so going through the palettes or the backlight levels writes only once. */
static void settings_track(const settings_t *current, TickType_t now)
{
    static settings_t seen = {.palette = UINT8_MAX};
    static TickType_t changed;

    if (memcmp(current, &seen, sizeof(seen)) != 0) {
        seen = *current;
        changed = now;
    }
    if (!settings_ready || memcmp(current, &saved_settings, sizeof(saved_settings)) == 0 ||
        now - changed < pdMS_TO_TICKS(SETTINGS_SAVE_DELAY_MS)) {
        return;
    }
    if (nvs_set_u8(settings_nvs, "palette", current->palette) != ESP_OK ||
        nvs_set_u8(settings_nvs, "bright", current->brightness) != ESP_OK ||
        nvs_set_u8(settings_nvs, "invert", current->invert) != ESP_OK ||
        nvs_set_u8(settings_nvs, "click", current->key_click) != ESP_OK ||
        nvs_commit(settings_nvs) != ESP_OK) {
        ESP_LOGW(TAG, "Settings not saved");
    }
    saved_settings = *current;
}

/* Fade the backlight down as soon as the radio enters deep sleep, like its own
 * backlight, or once it has been unplugged for BACKLIGHT_IDLE_MS without a
 * touch on the screen; restore it at once to on_percent when the radio wakes
 * up, is plugged in, or the screen is touched. A dimmer level chosen on the
 * plate fades in the same way. */
static void backlight_update(int *level, int on_percent, bool connected, bool asleep,
                             uint32_t unplugged_ms)
{
    /* A plain read of the time of the last touch: no display lock needed. */
    const bool dim = connected
        ? asleep
        : unplugged_ms >= BACKLIGHT_IDLE_MS &&
          lv_display_get_inactive_time(display) >= BACKLIGHT_IDLE_MS;
    const int target = dim ? BACKLIGHT_DIM_PERCENT : on_percent;
    int next = target;

    if (target < *level) {
        next = *level - BACKLIGHT_FADE_STEP;
        if (next < target) {
            next = target;
        }
    }
    if (next != *level) {
        bsp_display_brightness_set(next);
        *level = next;
    }
}

static void gps_coordinate_format(char *text, size_t size, int32_t coordinate)
{
    int64_t magnitude = coordinate;
    const bool negative = magnitude < 0;
    if (negative) {
        magnitude = -magnitude;
    }
    const uint64_t scaled = ((uint64_t)magnitude * 100000u + 180000u) / 360000u;
    snprintf(text, size, "%s%llu.%05llu", negative ? "-" : "",
             (unsigned long long)(scaled / 100000u),
             (unsigned long long)(scaled % 100000u));
}

static void gps_aprs_coordinate_format(char *text, size_t size,
                                       int32_t coordinate, bool longitude)
{
    int64_t magnitude = coordinate;
    const bool negative = magnitude < 0;
    if (negative) {
        magnitude = -magnitude;
    }
    const uint64_t units = ((uint64_t)magnitude + 30u) / 60u;
    const uint64_t degrees = units / 6000u;
    const uint64_t minutes = units % 6000u;
    const char hemisphere = longitude
        ? (negative ? 'W' : 'E')
        : (negative ? 'S' : 'N');
    if (longitude) {
        snprintf(text, size, "%03llu%02llu.%02llu%c",
                 (unsigned long long)degrees,
                 (unsigned long long)(minutes / 100u),
                 (unsigned long long)(minutes % 100u), hemisphere);
    } else {
        snprintf(text, size, "%02llu%02llu.%02llu%c",
                 (unsigned long long)degrees,
                 (unsigned long long)(minutes / 100u),
                 (unsigned long long)(minutes % 100u), hemisphere);
    }
}

static void gps_age_format(char *text, size_t size, bool known,
                           TickType_t now, TickType_t then)
{
    if (!known) {
        snprintf(text, size, "--");
        return;
    }
    const uint32_t age_ms = pdTICKS_TO_MS(now - then);
    if (age_ms < 10000u) {
        snprintf(text, size, "%lu.%lus", (unsigned long)(age_ms / 1000u),
                 (unsigned long)((age_ms % 1000u) / 100u));
    } else if (age_ms < 1000000u) {
        snprintf(text, size, "%lus", (unsigned long)(age_ms / 1000u));
    } else {
        snprintf(text, size, ">999s");
    }
}

static void gps_debug_update(const gps_status_t *status, bool started, bool error,
                             TickType_t last_data, TickType_t last_sentence,
                             TickType_t now)
{
    const char *state = "START";
    const char *receiver = "STARTING";
    const char *stream = "NONE";
    uint32_t color = 0xA7AFB8u;
    const bool has_data = status->bytes_received != 0u;
    const uint32_t data_age = has_data ? pdTICKS_TO_MS(now - last_data) : UINT32_MAX;
    const bool has_nmea = status->valid_sentences != 0u;
    const uint32_t nmea_age = has_nmea
        ? pdTICKS_TO_MS(now - last_sentence)
        : UINT32_MAX;
    const bool module_present =
        started && has_data && data_age <= GPS_DATA_TIMEOUT_MS;
    const bool nmea_fresh = has_nmea && nmea_age <= GPS_DATA_TIMEOUT_MS;
    const bool live_fix =
        module_present && nmea_fresh && status->state == GPS_STATE_FIX;
    const bool searching =
        !error && module_present && nmea_fresh &&
        status->state == GPS_STATE_SEARCHING;
    const bool search_blink_on =
        ((pdTICKS_TO_MS(now) / GPS_SEARCH_BLINK_MS) & 1u) == 0u;

    if (error) {
        state = "ERROR";
        receiver = "ERROR";
        stream = "ERROR";
        color = GPS_ERROR_COLOR;
    } else if (!started) {
        state = "START";
    } else if (!module_present) {
        state = "NO DATA";
        receiver = "ABSENT";
        stream = has_nmea ? "STALE" : "NONE";
        color = GPS_ERROR_COLOR;
    } else if (!nmea_fresh) {
        state = "RAW DATA";
        receiver = "DETECTED";
        stream = "RAW";
        color = GPS_DATA_COLOR;
    } else {
        receiver = "DETECTED";
        stream = "VALID";
        switch (status->state) {
        case GPS_STATE_FIX:
            state = "FIX";
            color = GPS_FIX_COLOR;
            break;
        case GPS_STATE_SEARCHING:
            state = "SEARCH";
            color = GPS_DATA_COLOR;
            break;
        case GPS_STATE_NMEA:
            state = "NMEA";
            color = GPS_DATA_COLOR;
            break;
        case GPS_STATE_DATA:
        case GPS_STATE_NO_DATA:
        default:
            state = "RAW DATA";
            color = GPS_DATA_COLOR;
            break;
        }
    }
    char age[12];
    gps_age_format(age, sizeof(age), status->valid_sentences != 0u,
                   now, last_sentence);
    char satellites_used[4];
    if (status->satellites_used_known) {
        snprintf(satellites_used, sizeof(satellites_used), "%u",
                 status->satellites_used);
    } else {
        snprintf(satellites_used, sizeof(satellites_used), "--");
    }
    char satellites_in_view[4];
    if (status->satellites_in_view_known) {
        snprintf(satellites_in_view, sizeof(satellites_in_view), "%u",
                 status->satellites_in_view);
    } else {
        snprintf(satellites_in_view, sizeof(satellites_in_view), "--");
    }
    char strongest_snr[12];
    if (status->strongest_snr_known) {
        snprintf(strongest_snr, sizeof(strongest_snr), "%u dB-Hz",
                 status->strongest_snr);
    } else {
        snprintf(strongest_snr, sizeof(strongest_snr), "--");
    }
    char latitude[16] = "--.-----";
    char longitude[16] = "--.-----";
    char aprs_latitude[10] = "--------";
    char aprs_longitude[11] = "---------";
    if (status->has_fix) {
        gps_coordinate_format(latitude, sizeof(latitude), status->fix.lat_cas);
        gps_coordinate_format(longitude, sizeof(longitude), status->fix.lon_cas);
        gps_aprs_coordinate_format(aprs_latitude, sizeof(aprs_latitude),
                                   status->fix.lat_cas, false);
        gps_aprs_coordinate_format(aprs_longitude, sizeof(aprs_longitude),
                                   status->fix.lon_cas, true);
    }

    for (uint8_t i = 0; i < VIEW_COUNT; i++) {
        lv_obj_t *status_dot = views[i].gps_status_dot;
        if (status_dot != NULL) {
            const bool visible =
                (error || module_present) && (!searching || search_blink_on);
            lv_obj_set_hidden(status_dot, !visible);
            lv_obj_set_style_bg_color(status_dot, lv_color_hex(color), 0);
        }

        lv_obj_t *label = views[i].gps_debug_label;
        lv_label_set_text_fmt(label,
                              "%s\n%s\n%s\n%s\n\n"
                              "%lu\n%lu\n%lu\n%s\n\n"
                              "%s\n%s\n%s\n\n"
                              "%s\n%s\n%s %s",
                              state, receiver, stream, live_fix ? "YES" : "NO",
                              (unsigned long)status->bytes_received,
                              (unsigned long)status->valid_sentences,
                              (unsigned long)status->checksum_errors,
                              age, satellites_used, satellites_in_view,
                              strongest_snr, latitude, longitude,
                              aprs_latitude, aprs_longitude);
    }
}

static void render_task(void *arg)
{
    (void)arg;
    uint8_t framebuffer[RADIO_FRAME_BYTES] = {0};
    uint64_t rows = 0;
    uint8_t flags = 0;
    settings_t settings = {.palette = PALETTE_K1};
    bool connected = false;
    bool shown_connected = false;
    bool redraw = true;
    lv_display_rotation_t rotation = FIXED_ROTATION;
    lv_display_rotation_t shown_rotation = FIXED_ROTATION;
    view_t *shown_view = NULL;
    lcd_colors_t shown_colors = {0};
    int backlight = 0;
    TickType_t connected_tick = xTaskGetTickCount();
    TickType_t gps_refresh_tick = 0;

#if AUTO_ROTATE
    /* The backlight stays off until the IMU gives the orientation of the Tab5,
     * so the first picture is already the right way round. */
    xSemaphoreTake(orientation_ready, pdMS_TO_TICKS(BOOT_ORIENTATION_MS));
#endif

    while (true) {
        const TickType_t now = xTaskGetTickCount();
        gps_status_t gps_status;
        TickType_t gps_last_data;
        TickType_t gps_last_sentence;
        bool gps_started;
        bool gps_error;

        xSemaphoreTake(state_mutex, portMAX_DELAY);
        gps_status = viewer.gps;
        gps_last_data = viewer.gps_last_data_tick;
        gps_last_sentence = viewer.gps_last_sentence_tick;
        gps_started = viewer.gps_started;
        gps_error = viewer.gps_error;
        if (viewer.frame_dirty) {
            memcpy(framebuffer, viewer.framebuffer, sizeof(framebuffer));
            rows |= viewer.dirty_rows;
            viewer.dirty_rows = 0;
            flags = viewer.flags;
            settings = viewer.settings;
            connected = viewer.usb_connected;
            rotation = viewer.rotation;
            viewer.frame_dirty = false;
            redraw = true;
        }
        xSemaphoreGive(state_mutex);

        const bool asleep = (flags & K1VIEWER_FLAG_DEEP_SLEEP) != 0;
        const bool gps_refresh = now - gps_refresh_tick >= pdMS_TO_TICKS(GPS_DEBUG_REFRESH_MS);

        if ((redraw || gps_refresh) && bsp_display_lock(1000)) {
            if (redraw) {
                const bool led_red = connected && (flags & K1VIEWER_FLAG_LED_RED) != 0;
                const bool led_green =
                    connected && !led_red && (flags & K1VIEWER_FLAG_LED_GREEN) != 0;
                const bool torch = connected && (flags & K1VIEWER_FLAG_FLASHLIGHT) != 0;
                const lcd_colors_t colors = lcd_colors(settings.palette, settings.invert, asleep);
                view_t *view = view_for_rotation(rotation);
                /* A new palette, backlight state or orientation redraws the whole
                 * LCD; otherwise only the rows the radio changed. */
                bool full = memcmp(&colors, &shown_colors, sizeof(colors)) != 0;

                if (shown_view == NULL || rotation != shown_rotation) {
                    bsp_display_rotate(display, rotation);
                    shown_rotation = rotation;
                    full = true;
                }
                if (view != shown_view) {
                    lv_screen_load(view->screen);
                    full = true;
                }
                if (full || rows != 0) {
                    render_radio_frame(view, framebuffer, &colors, rows, full);
                }
                for (uint8_t i = 0; i < VIEW_COUNT; i++) {
                    lv_obj_set_hidden(views[i].led_canvases[LED_RED], !led_red);
                    lv_obj_set_hidden(views[i].led_canvases[LED_GREEN], !led_green);
                    lv_obj_set_hidden(views[i].torch_canvas, !torch);
                }
                if (connected != shown_connected) {
                    connect_screen_show(!connected);
                    shown_connected = connected;
                }
                if (shown_view == NULL) {
                    /* First picture: on the panel before the backlight comes on. */
                    lv_refr_now(display);
                }
                shown_view = view;
                shown_colors = colors;
                rows = 0;
                redraw = false;
            }
            if (gps_refresh) {
                gps_debug_update(&gps_status, gps_started, gps_error,
                                 gps_last_data, gps_last_sentence, now);
                gps_refresh_tick = now;
            }
            bsp_display_unlock();
        }

        if (shown_view != NULL) {
            if (connected) {
                connected_tick = now;
            }
            backlight_update(&backlight, backlight_levels[settings.brightness], connected, asleep,
                             pdTICKS_TO_MS(now - connected_tick));
        }
        settings_track(&settings, now);

        vTaskDelay(pdMS_TO_TICKS(RENDER_PERIOD_MS));
    }
}

/* Tap the radio LCD to change palette, hold it to invert the LCD. */
static void palette_event_callback(lv_event_t *event)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    if (lv_event_get_code(event) == LV_EVENT_LONG_PRESSED) {
        viewer.settings.invert = !viewer.settings.invert;
    } else {
        viewer.settings.palette = (uint8_t)((viewer.settings.palette + 1u) % PALETTE_COUNT);
    }
    viewer.frame_dirty = true;
    xSemaphoreGive(state_mutex);
}

static void toast_opa_cb(void *obj, int32_t value)
{
    lv_obj_set_style_opa(obj, (lv_opa_t)value, 0);
}

static void toast_faded_out(lv_anim_t *anim)
{
    (void)anim;
    lv_obj_set_hidden(toast, true);
}

/* Show a toast in the middle of the display for TOAST_SHOW_MS, then fade it
 * out. It sits on the top layer, so it stays centred whatever the view. */
static void toast_show(const lv_image_dsc_t *image)
{
    if (toast == NULL || image->data == NULL) {
        return;
    }
    lv_image_set_src(toast_image, image);
    lv_anim_delete(toast, toast_opa_cb);
    lv_obj_set_style_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_hidden(toast, false);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, toast);
    lv_anim_set_exec_cb(&anim, toast_opa_cb);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_delay(&anim, TOAST_SHOW_MS);
    lv_anim_set_duration(&anim, TOAST_FADE_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&anim, toast_faded_out);
    lv_anim_start(&anim);
}

/* A single tap on the plate, once no second tap came: next backlight level. */
static void plate_tap_timeout(lv_timer_t *timer)
{
    lv_timer_pause(timer);
    plate_tap_pending = false;

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    viewer.settings.brightness =
        (uint8_t)((viewer.settings.brightness + 1u) % SKIN_TOAST_BRIGHTNESS_LEVELS);
    viewer.frame_dirty = true;
    const uint8_t brightness = viewer.settings.brightness;
    xSemaphoreGive(state_mutex);

    toast_show(&toast_brightness_images[brightness]);
}

/* The QUANSHENG plate: tap it to step through the backlight levels, tap it
 * twice to switch the key beep on or off, hold it to lock or unlock the
 * orientation. Like a key, it turns blue once held. */
static void plate_event_callback(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *target = lv_event_get_current_target_obj(event);

    switch (code) {
    case LV_EVENT_PRESSED:
        lv_obj_remove_state(target, LV_STATE_USER_1);
        break;

    case LV_EVENT_SHORT_CLICKED: {
        /* Not LVGL's double click: it also wants both taps within a few
         * pixels, which fingers seldom manage. */
        if (!plate_tap_pending) {
            plate_tap_pending = true;
            lv_timer_reset(plate_tap_timer);
            lv_timer_resume(plate_tap_timer);
            break;
        }
        plate_tap_pending = false;
        lv_timer_pause(plate_tap_timer);
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        viewer.settings.key_click = !viewer.settings.key_click;
        const bool key_click = viewer.settings.key_click;
        xSemaphoreGive(state_mutex);

        toast_show(&toast_beep_images[key_click]);
#if KEY_CLICK
        if (key_click) {
            click_play(false);
        }
#endif
        break;
    }

    case LV_EVENT_LONG_PRESSED: {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        viewer.rotation_locked = !viewer.rotation_locked;
        const bool locked = viewer.rotation_locked;
        xSemaphoreGive(state_mutex);

        lv_obj_add_state(target, LV_STATE_USER_1);
        ESP_LOGI(TAG, "Orientation %s", locked ? "locked" : "unlocked");
        toast_show(&toast_lock_images[locked]);
        break;
    }

    default:
        break;
    }
}

static esp_err_t usb_send(cdc_acm_dev_hdl_t device, const uint8_t *packet, size_t length)
{
    const esp_err_t result =
        cdc_acm_host_data_tx_blocking(device, packet, length, USB_TX_TIMEOUT_MS);
    if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "USB send failed: %s", esp_err_to_name(result));
    }
    return result;
}

static bool usb_open_can_retry(esp_err_t result)
{
    return result == ESP_ERR_NOT_FOUND || result == ESP_ERR_TIMEOUT;
}

static esp_err_t usb_open_radio(const cdc_acm_host_device_config_t *config,
                                cdc_acm_dev_hdl_t *device,
                                const char **transport)
{
    esp_err_t result = cdc_acm_host_open(K1_USB_VID, K1_USB_PID, 0, config, device);
    if (result == ESP_OK) {
        *transport = "native USB-C";
        return ESP_OK;
    }
    if (!usb_open_can_retry(result)) {
        return result;
    }

    result = ch34x_vcp_open(CH34X_PID_AUTO, 0, config, device);
    if (result == ESP_OK) {
        *transport = "K cable (CH34x)";
        return ESP_OK;
    }
    if (!usb_open_can_retry(result)) {
        return result;
    }

    result = cp210x_vcp_open(CP210X_PID_AUTO, 0, config, device);
    if (result == ESP_OK) {
        *transport = "K cable (CP210x)";
        return ESP_OK;
    }
    if (!usb_open_can_retry(result)) {
        return result;
    }

    result = pl2303_vcp_open(PL2303_PID, 0, config, device);
    if (result == ESP_OK) {
        *transport = "K cable (PL2303)";
        return ESP_OK;
    }
    if (!usb_open_can_retry(result)) {
        return result;
    }

    result = ftdi_vcp_open(FTDI_PID_AUTO, 0, config, device);
    if (result == ESP_OK) {
        *transport = "K cable (FTDI)";
        return ESP_OK;
    }
    if (!usb_open_can_retry(result)) {
        return result;
    }

    result = cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID, 0, config, device);
    if (result == ESP_OK) {
        *transport = "K cable (CDC-ACM)";
    }
    return result;
}

static void write_i32_le(uint8_t *destination, int32_t value)
{
    const uint32_t bits = (uint32_t)value;
    destination[0] = (uint8_t)bits;
    destination[1] = (uint8_t)(bits >> 8);
    destination[2] = (uint8_t)(bits >> 16);
    destination[3] = (uint8_t)(bits >> 24);
}

static void usb_send_message(cdc_acm_dev_hdl_t device,
                             const outbound_message_t *message)
{
    if (message->type == OUTBOUND_KEY) {
        const radio_key_t *key = &message->data.key;
        const uint8_t packet[] = {
            0xAA, 0x55,
            key->long_press ? K1VIEWER_TYPE_KEY_LONG : K1VIEWER_TYPE_KEY,
            key->code,
        };
        usb_send(device, packet, sizeof(packet));
        return;
    }

    uint8_t packet[12] = {0xAA, 0x55, K1VIEWER_TYPE_GPS};
    write_i32_le(packet + 3, message->data.gps.lat_cas);
    write_i32_le(packet + 7, message->data.gps.lon_cas);
    for (uint8_t i = 2; i < sizeof(packet) - 1u; i++) {
        packet[sizeof(packet) - 1u] ^= packet[i];
    }
    usb_send(device, packet, sizeof(packet));
}

#define BATTERY_INA226_ADDR    0x41                       /* battery voltage and current */
#define CHARGE_ENABLE_PIN      IO_EXPANDER_PIN_NUM_7      /* expander 0x44: charger enable */

/* The charger is off at power-up and the battery never charges unless it is
 * enabled. Enable it when a battery is there: the INA226 bus voltage in the
 * range of the 2S pack. The outputs of expander 0x44 start high-impedance with
 * a pull-down, so the level only reaches the pin in push-pull mode. Called
 * once the BSP has set up that expander for the USB-A power. */
static void battery_charge_enable(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    i2c_master_dev_handle_t ina = NULL;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BATTERY_INA226_ADDR,
        .scl_speed_hz = 400000,
    };
    if (bus == NULL || i2c_master_probe(bus, BATTERY_INA226_ADDR, 50) != ESP_OK ||
        i2c_master_bus_add_device(bus, &config, &ina) != ESP_OK) {
        ESP_LOGW(TAG, "No INA226: battery charging left off");
        return;
    }
    const uint8_t reg = 0x02;                           /* bus voltage, 1.25 mV per bit */
    uint8_t raw[2] = {0};
    const bool read = i2c_master_transmit_receive(ina, &reg, 1, raw, 2, 50) == ESP_OK;
    i2c_master_bus_rm_device(ina);
    const float volts = read ? (float)((raw[0] << 8) | raw[1]) * 1.25e-3f : 0.0f;
    if (volts < 5.0f || volts > 9.0f) {
        ESP_LOGW(TAG, "No battery seen (%.2f V): charging left off", volts);
        return;
    }

    esp_io_expander_handle_t expander = bsp_io_expander1_init();
    if (expander == NULL ||
        esp_io_expander_set_dir(expander, CHARGE_ENABLE_PIN, IO_EXPANDER_OUTPUT) != ESP_OK ||
        esp_io_expander_set_level(expander, CHARGE_ENABLE_PIN, 1) != ESP_OK ||
        esp_io_expander_set_output_mode(expander, CHARGE_ENABLE_PIN,
                                        IO_EXPANDER_OUTPUT_MODE_PUSH_PULL) != ESP_OK) {
        ESP_LOGW(TAG, "Could not enable battery charging");
        return;
    }
    ESP_LOGI(TAG, "Battery %.2f V: charging enabled", volts);
}

static void usb_task(void *arg)
{
    (void)arg;
    static const uint8_t keepalive[] = {0x55, 0xAA, 0x00, 0x00};
    const cdc_acm_host_device_config_t device_config = {
        .connection_timeout_ms = USB_OPEN_ATTEMPT_MS,
        .out_buffer_size = 64,
        .in_buffer_size = 2048,
        .user_arg = NULL,
        .event_cb = usb_event_callback,
        .data_cb = usb_rx_callback,
    };

    ESP_ERROR_CHECK(bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV, true));
    battery_charge_enable();
    ESP_ERROR_CHECK(cdc_acm_host_install(NULL));

    while (true) {
        cdc_acm_dev_hdl_t device = NULL;
        const char *transport = NULL;
        ESP_LOGI(TAG, "Waiting for K1 USB-C or K cable");

        const esp_err_t open_result = usb_open_radio(&device_config, &device, &transport);
        if (open_result != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        parser_reset();
        xSemaphoreTake(disconnected_sem, 0);
        xQueueReset(outbound_queue);
        vTaskDelay(pdMS_TO_TICKS(10));

        const cdc_acm_line_coding_t line_coding = {
            .dwDTERate = K1_UART_BAUD,
            .bCharFormat = 0,
            .bParityType = 0,
            .bDataBits = 8,
        };
        const esp_err_t line_result =
            cdc_acm_host_line_coding_set(device, &line_coding);
        if (line_result != ESP_OK) {
            ESP_LOGW(TAG, "%s cannot be configured at %u baud: %s", transport,
                     (unsigned)K1_UART_BAUD, esp_err_to_name(line_result));
            cdc_acm_host_desc_print(device);
            ESP_ERROR_CHECK_WITHOUT_ABORT(cdc_acm_host_close(device));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        const esp_err_t control_result =
            cdc_acm_host_set_control_line_state(device, true, false);
        if (control_result != ESP_OK && control_result != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "%s control-line setup failed: %s", transport,
                     esp_err_to_name(control_result));
        }

        ESP_LOGI(TAG, "USB transport opened through %s at %u baud", transport,
                 (unsigned)K1_UART_BAUD);
        if (usb_send(device, keepalive, sizeof(keepalive)) != ESP_OK) {
            ESP_LOGW(TAG, "%s opened but rejected its first transfer", transport);
            cdc_acm_host_desc_print(device);
        }

        /* Only this task writes to the K1: touch keys and GPS fixes arrive
         * through the queue, with a keepalive whenever neither was sent. */
        while (xSemaphoreTake(disconnected_sem, 0) != pdTRUE) {
            outbound_message_t message;
            if (xQueueReceive(outbound_queue, &message,
                              pdMS_TO_TICKS(K1VIEWER_KEEPALIVE_MS)) == pdTRUE) {
                usb_send_message(device, &message);
            } else {
                usb_send(device, keepalive, sizeof(keepalive));
            }
        }
    }
}

static bool rgb565_image_init(lv_image_dsc_t *image, const char *name,
                              const uint8_t *start, const uint8_t *end,
                              uint32_t width, uint32_t height)
{
    const size_t size = (size_t)(end - start);
    const size_t expected = (size_t)width * height * sizeof(uint16_t);

    if (size != expected) {
        ESP_LOGE(TAG, "%s is %u bytes, expected %u", name, (unsigned)size, (unsigned)expected);
        return false;
    }

    /* PSRAM is faster to draw from than flash-mapped rodata. */
    uint8_t *pixels = heap_caps_aligned_alloc(64, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pixels == NULL) {
        ESP_LOGE(TAG, "No PSRAM for %s", name);
        return false;
    }
    memcpy(pixels, start, size);

    image->header.magic = LV_IMAGE_HEADER_MAGIC;
    image->header.cf = LV_COLOR_FORMAT_RGB565;
    image->header.flags = 0;
    image->header.w = width;
    image->header.h = height;
    image->header.stride = width * sizeof(uint16_t);
    image->data_size = size;
    image->data = pixels;
    return true;
}

/* Hidden ARGB8888 canvas for a light drawn over the skin (RX/TX LED, torch).
 * Its buffer and stride in pixels are returned for drawing. */
static lv_obj_t *create_light_canvas(lv_obj_t *root, int32_t x, int32_t y,
                                     int32_t w, int32_t h,
                                     uint32_t **pixels, uint32_t *stride)
{
    const uint32_t stride_bytes = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_ARGB8888);
    *pixels = heap_caps_aligned_calloc(64, stride_bytes, h, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(*pixels != NULL);
    *stride = stride_bytes / sizeof(uint32_t);

    lv_obj_t *canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(canvas, *pixels, w, h, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(canvas, x, y);
    lv_obj_set_hidden(canvas, true);
    return canvas;
}

/* Black screen with the "connect your UV-K1" icon and message, and the author,
 * license and version at the bottom, shown while no radio is connected. It
 * covers the radio and swallows its touches. */
static lv_obj_t *create_connect_screen(const view_t *view)
{
    const skin_layout_t *layout = view->layout;
    lv_obj_t *screen = lv_obj_create(view->screen);

    lv_obj_remove_style_all(screen);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_size(screen, layout->width, layout->height);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_clickable(screen, true);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_click_focusable(screen, false);

    lv_obj_t *icon = NULL;
    if (connect_icon_image.data != NULL) {
        icon = lv_image_create(screen);
        lv_image_set_src(icon, &connect_icon_image);
        lv_obj_set_pos(icon, layout->connect_icon_x, layout->connect_icon_y);
    }

    if (connect_text_image.data != NULL) {
        lv_obj_t *text = lv_image_create(screen);
        lv_image_set_src(text, &connect_text_image);
        lv_obj_set_pos(text, layout->connect_text_x, layout->connect_text_y);
    }

    /* The K-plug message lies over the USB-C one, transparent until the icon
     * shows the K-plug cable. */
    lv_obj_t *text_kplug = NULL;
    if (connect_text_kplug_image.data != NULL) {
        text_kplug = lv_image_create(screen);
        lv_image_set_src(text_kplug, &connect_text_kplug_image);
        lv_obj_set_pos(text_kplug, layout->connect_text_x, layout->connect_text_y);
        lv_obj_set_style_opa(text_kplug, LV_OPA_TRANSP, 0);
    }

    if (icon != NULL) {
        connect_pulse_start(icon, text_kplug, true);
    }

    if (connect_credit_image.data != NULL) {
        lv_obj_t *credit = lv_image_create(screen);
        lv_image_set_src(credit, &connect_credit_image);
        lv_obj_set_pos(credit, layout->connect_credit_x, layout->connect_credit_y);
    }

    /* The version comes from the build (project() in CMakeLists.txt), so it is
     * written here rather than drawn in the credit image. */
    lv_obj_t *version = lv_label_create(screen);
    lv_label_set_text_fmt(version, "Version %s", esp_app_get_description()->version);
    lv_obj_set_style_text_font(version, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(version, lv_color_hex(CONNECT_VERSION_COLOR), 0);
    lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(version, SKIN_CONNECT_CREDIT_W);
    lv_obj_set_pos(version, layout->connect_credit_x,
                   layout->connect_credit_y + SKIN_CONNECT_CREDIT_H + 1);
    return screen;
}

static void gps_debug_set_visible(bool visible)
{
    gps_debug_visible = visible;
    for (uint8_t i = 0; i < VIEW_COUNT; i++) {
        if (views[i].gps_debug_panel != NULL) {
            lv_obj_set_hidden(views[i].gps_debug_panel, !visible);
        }
    }
}

static void gps_button_event_callback(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED) {
        gps_debug_set_visible(!gps_debug_visible);
    }
}

static void gps_debug_event_callback(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
        gps_debug_set_visible(false);
    }
}

static lv_obj_t *create_gps_button(lv_obj_t *parent, const skin_rect_t *rect,
                                   lv_obj_t **status_out)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_scrollable(button, false);
    lv_obj_set_scroll_on_focus(button, false);
    lv_obj_set_click_focusable(button, false);
    lv_obj_set_clickable(button, true);

    if (rect->w == 0 || rect->h == 0) {
        /* Screen-only mode has no keypad: keep an invisible diagnostic
         * long-press target in the top-left corner. */
        lv_obj_set_pos(button, 5, 5);
        lv_obj_set_size(button, 78, 40);
        lv_obj_add_event_cb(button, gps_button_event_callback,
                            LV_EVENT_LONG_PRESSED, NULL);
        *status_out = NULL;
        return button;
    }

    lv_obj_set_pos(button, rect->x, rect->y);
    lv_obj_set_size(button, rect->w, rect->h);
    lv_obj_set_style_radius(button, rect->h / 3, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(KEY_PRESSED_COLOR),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, gps_button_event_callback,
                        LV_EVENT_SHORT_CLICKED, NULL);

    lv_obj_t *status = lv_obj_create(button);
    lv_obj_remove_style_all(status);
    lv_obj_set_size(status, GPS_STATUS_SIZE, GPS_STATUS_SIZE);
    lv_obj_set_pos(status, rect->w - GPS_STATUS_SIZE - 9,
                   (rect->h - GPS_STATUS_SIZE) / 2);
    lv_obj_set_style_radius(status, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(status, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(status, lv_color_hex(GPS_PANEL_COLOR), 0);
    lv_obj_set_style_border_width(status, 1, 0);
    lv_obj_set_clickable(status, false);
    lv_obj_set_hidden(status, true);
    *status_out = status;
    return button;
}

static lv_obj_t *create_gps_debug_panel(lv_obj_t *parent, lv_obj_t **label_out)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, GPS_PANEL_WIDTH, GPS_PANEL_HEIGHT);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(GPS_PANEL_COLOR), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(GPS_PANEL_BORDER_COLOR), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_clickable(panel, true);
    lv_obj_set_scrollable(panel, false);
    lv_obj_add_event_cb(panel, gps_debug_event_callback,
                        LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_set_hidden(panel, true);

    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_pos(title, 16, 14);
    lv_obj_set_width(title, GPS_PANEL_WIDTH - 32);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(GPS_PANEL_TEXT_COLOR), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_clickable(title, false);
    lv_label_set_text(title, "GPS DIAGNOSTICS");

    lv_obj_t *divider = lv_obj_create(panel);
    lv_obj_remove_style_all(divider);
    lv_obj_set_pos(divider, 16, 42);
    lv_obj_set_size(divider, GPS_PANEL_WIDTH - 32, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(GPS_PANEL_BORDER_COLOR), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_clickable(divider, false);

    lv_obj_t *names = lv_label_create(panel);
    lv_obj_set_pos(names, 16, 52);
    lv_obj_set_size(names, 150, 300);
    lv_obj_set_style_text_font(names, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(names, lv_color_hex(GPS_PANEL_LABEL_COLOR), 0);
    lv_obj_set_style_text_align(names, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_line_space(names, 1, 0);
    lv_label_set_long_mode(names, LV_LABEL_LONG_CLIP);
    lv_obj_set_clickable(names, false);
    lv_label_set_text(names,
                      "State :\n"
                      "Module detected :\n"
                      "NMEA stream :\n"
                      "Fix valid :\n\n"
                      "Bytes received :\n"
                      "Valid sentences :\n"
                      "Checksum errors :\n"
                      "Last NMEA age :\n\n"
                      "Satellites used :\n"
                      "Satellites view :\n"
                      "Strongest SNR :\n\n"
                      "Last latitude :\n"
                      "Last longitude :\n"
                      "APRS last fix :");

    lv_obj_t *values = lv_label_create(panel);
    lv_obj_set_pos(values, 178, 52);
    lv_obj_set_size(values, GPS_PANEL_WIDTH - 194, 300);
    lv_obj_set_style_text_font(values, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(values, lv_color_hex(GPS_PANEL_TEXT_COLOR), 0);
    lv_obj_set_style_text_line_space(values, 1, 0);
    lv_label_set_long_mode(values, LV_LABEL_LONG_CLIP);
    lv_obj_set_clickable(values, false);
    lv_label_set_text(values,
                      "START\nSTARTING\nNONE\nNO\n\n"
                      "0\n0\n0\n--\n\n"
                      "--\n--\n--\n\n"
                      "--.-----\n--.-----\n-------- ---------");

    lv_obj_t *footer = lv_label_create(panel);
    lv_obj_set_pos(footer, 16, GPS_PANEL_HEIGHT - 28);
    lv_obj_set_width(footer, GPS_PANEL_WIDTH - 32);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(footer, lv_color_hex(GPS_PANEL_LABEL_COLOR), 0);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_clickable(footer, false);
    lv_label_set_text(footer, "Tap panel to close");

    *label_out = values;
    return panel;
}

/* Transparent key cover: white while pressed, blue once the long press is sent. */
static lv_obj_t *create_key_overlay(lv_obj_t *parent, int32_t x, int32_t y,
                                    int32_t w, int32_t h, int32_t radius)
{
    lv_obj_t *overlay = lv_obj_create(parent);

    lv_obj_remove_style_all(overlay);
    lv_obj_set_pos(overlay, x, y);
    lv_obj_set_size(overlay, w, h);
    lv_obj_set_style_radius(overlay, radius, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(KEY_PRESSED_COLOR), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(KEY_LONG_COLOR),
                              LV_STATE_PRESSED | LV_STATE_USER_1);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_40, LV_STATE_PRESSED | LV_STATE_USER_1);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_scroll_on_focus(overlay, false);
    lv_obj_set_click_focusable(overlay, false);
    return overlay;
}

static void create_key_targets(const view_t *view)
{
    const skin_layout_t *layout = view->layout;
    const skin_rect_t *plate = &layout->plate;
    lv_obj_t *plate_target = create_key_overlay(view->screen, plate->x, plate->y,
                                                plate->w, plate->h, PLATE_RADIUS);

    lv_obj_add_event_cb(plate_target, plate_event_callback, LV_EVENT_ALL, NULL);

    for (uint8_t i = 0; i < layout->key_count; i++) {
        const skin_key_t *key = &layout->keys[i];
        lv_obj_t *target = create_key_overlay(view->screen, key->key.x, key->key.y,
                                              key->key.w, key->key.h, layout->key_radius);
        lv_obj_add_event_cb(target, key_event_callback, LV_EVENT_ALL,
                            (void *)(uintptr_t)key->code);
    }

    /* F1 and F2 (rail keys in portrait, pills in landscape): a transparent
     * touch area, wider than the drawn key, takes the press and passes its
     * states to the cover of the drawn key, which also glows so the press
     * shows around the finger. */
    for (uint8_t i = 0; i < layout->side_key_count; i++) {
        const skin_side_key_t *key = &layout->side_keys[i];
        lv_obj_t *area = lv_obj_create(view->screen);

        lv_obj_remove_style_all(area);
        lv_obj_set_pos(area, key->hit.x, key->hit.y);
        lv_obj_set_size(area, key->hit.w, key->hit.h);
        lv_obj_set_scrollable(area, false);
        lv_obj_set_scroll_on_focus(area, false);
        lv_obj_set_click_focusable(area, false);
        lv_obj_set_state_trickle(area, true);
        lv_obj_set_overflow_visible(area, true);
        lv_obj_add_event_cb(area, key_event_callback, LV_EVENT_ALL,
                            (void *)(uintptr_t)key->code);

        lv_obj_t *overlay = create_key_overlay(area, key->key.x - key->hit.x,
                                               key->key.y - key->hit.y,
                                               key->key.w, key->key.h, key->radius);
        lv_obj_set_clickable(overlay, false);
        lv_obj_set_style_shadow_color(overlay, lv_color_hex(KEY_LONG_COLOR), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(overlay, SIDE_KEY_GLOW, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_opa(overlay, LV_OPA_60, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_opa(overlay, LV_OPA_COVER, LV_STATE_PRESSED | LV_STATE_USER_1);
    }
}

/* Build the screen of one orientation: skin, radio LCD, lights, touch keys
 * and the "connect the radio" cover. */
static void create_view(view_t *view)
{
    const skin_layout_t *layout = view->layout;
    const skin_rect_t *glass = &layout->glass;

    view->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(view->screen, lv_color_hex(BACKGROUND_COLOR), 0);
    lv_obj_set_scrollable(view->screen, false);

    if (rgb565_image_init(&view->skin, view->name, view->skin_start, view->skin_end,
                          layout->width, layout->height)) {
        lv_obj_t *skin = lv_image_create(view->screen);
        lv_image_set_src(skin, &view->skin);
        lv_obj_set_pos(skin, 0, 0);
    }

    const uint32_t stride = lv_draw_buf_width_to_stride(glass->w, LV_COLOR_FORMAT_RGB565);
    view->lcd_stride = stride / sizeof(uint16_t);
    view->lcd_pixels = heap_caps_aligned_calloc(64, stride, glass->h,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(view->lcd_pixels != NULL);

    /* The radio LCD replaces the glass of the skin; tap it to change palette,
     * hold it to invert it. */
    view->lcd_canvas = lv_canvas_create(view->screen);
    lv_canvas_set_buffer(view->lcd_canvas, view->lcd_pixels, glass->w, glass->h,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(view->lcd_canvas, glass->x, glass->y);
    lv_obj_set_clickable(view->lcd_canvas, true);
    lv_obj_add_event_cb(view->lcd_canvas, palette_event_callback, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(view->lcd_canvas, palette_event_callback, LV_EVENT_LONG_PRESSED, NULL);

    uint32_t *pixels;
    uint32_t stride_px;
    const skin_rect_t *box = &layout->led.box;
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        view->led_canvases[i] = create_light_canvas(view->screen, box->x, box->y, box->w, box->h,
                                                    &pixels, &stride_px);
        led_render(&layout->led, pixels, stride_px, &led_colors[i]);
    }
    box = &layout->torch.box;
    view->torch_canvas = create_light_canvas(view->screen, box->x, box->y, box->w, box->h,
                                             &pixels, &stride_px);
    torch_render(&layout->torch, pixels, stride_px);

    create_key_targets(view);
    view->gps_button = create_gps_button(view->screen, &layout->gps,
                                         &view->gps_status_dot);
    view->connect_screen = create_connect_screen(view);
    lv_obj_add_event_cb(view->connect_screen, gps_button_event_callback,
                        LV_EVENT_LONG_PRESSED, NULL);
    view->gps_debug_panel = create_gps_debug_panel(view->screen,
                                                   &view->gps_debug_label);
}

/* Hidden toast on the top layer, above every view: a rounded panel of the
 * colour the toast images are drawn on, larger than them by TOAST_PADDING so
 * their square corners stay inside its rounded ones. */
static void create_toast(void)
{
    toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(toast);
    lv_obj_set_size(toast, SKIN_TOAST_W + 2 * TOAST_PADDING, SKIN_TOAST_H + 2 * TOAST_PADDING);
    lv_obj_center(toast);
    lv_obj_set_style_radius(toast, TOAST_PADDING, 0);
    lv_obj_set_style_bg_color(toast, lv_color_hex(SKIN_TOAST_COLOR), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(TOAST_BORDER_COLOR), 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_clickable(toast, false);
    lv_obj_set_scrollable(toast, false);
    lv_obj_set_hidden(toast, true);

    toast_image = lv_image_create(toast);
    lv_obj_center(toast_image);
}

/* Build the three views with the backlight still off: the render task turns
 * the display, loads the view and lights it once the orientation is known. */
static void display_init(void)
{
    display = bsp_display_start();
    assert(display != NULL);
    /* The BSP logs every backlight change, and the backlight fades in steps. */
    esp_log_level_set("M5Stack Tab5", ESP_LOG_WARN);

    rgb565_image_init(&connect_icon_image, "connect icon",
                      k1_connect_icon_rgb565_start, k1_connect_icon_rgb565_end,
                      SKIN_CONNECT_ICON_W, SKIN_CONNECT_ICON_H);
    rgb565_image_init(&connect_icon_kplug_image, "connect K-plug icon",
                      k1_connect_icon_kplug_rgb565_start, k1_connect_icon_kplug_rgb565_end,
                      SKIN_CONNECT_ICON_W, SKIN_CONNECT_ICON_H);
    rgb565_image_init(&connect_text_image, "connect text",
                      k1_connect_text_rgb565_start, k1_connect_text_rgb565_end,
                      SKIN_CONNECT_TEXT_W, SKIN_CONNECT_TEXT_H);
    rgb565_image_init(&connect_text_kplug_image, "connect K-plug text",
                      k1_connect_text_kplug_rgb565_start, k1_connect_text_kplug_rgb565_end,
                      SKIN_CONNECT_TEXT_W, SKIN_CONNECT_TEXT_H);
    rgb565_image_init(&connect_credit_image, "connect credit",
                      k1_connect_credit_rgb565_start, k1_connect_credit_rgb565_end,
                      SKIN_CONNECT_CREDIT_W, SKIN_CONNECT_CREDIT_H);

    static const struct {
        lv_image_dsc_t *image;
        const char *name;
        const uint8_t *start;
        const uint8_t *end;
    } toasts[] = {
        {&toast_lock_images[0], "unlocked toast", k1_toast_unlocked_rgb565_start,
         k1_toast_unlocked_rgb565_end},
        {&toast_lock_images[1], "locked toast", k1_toast_locked_rgb565_start,
         k1_toast_locked_rgb565_end},
        {&toast_beep_images[0], "beep off toast", k1_toast_beep_off_rgb565_start,
         k1_toast_beep_off_rgb565_end},
        {&toast_beep_images[1], "beep on toast", k1_toast_beep_on_rgb565_start,
         k1_toast_beep_on_rgb565_end},
        {&toast_brightness_images[0], "brightness toast", k1_toast_brightness_0_rgb565_start,
         k1_toast_brightness_0_rgb565_end},
        {&toast_brightness_images[1], "brightness toast", k1_toast_brightness_1_rgb565_start,
         k1_toast_brightness_1_rgb565_end},
        {&toast_brightness_images[2], "brightness toast", k1_toast_brightness_2_rgb565_start,
         k1_toast_brightness_2_rgb565_end},
        {&toast_brightness_images[3], "brightness toast", k1_toast_brightness_3_rgb565_start,
         k1_toast_brightness_3_rgb565_end},
    };
    _Static_assert(SKIN_TOAST_BRIGHTNESS_LEVELS == 4, "one brightness toast per backlight level");
    for (size_t i = 0; i < sizeof(toasts) / sizeof(toasts[0]); i++) {
        rgb565_image_init(toasts[i].image, toasts[i].name, toasts[i].start, toasts[i].end,
                          SKIN_TOAST_W, SKIN_TOAST_H);
    }

    if (bsp_display_lock(0)) {
        for (uint8_t i = 0; i < VIEW_COUNT; i++) {
            create_view(&views[i]);
        }
        create_toast();
        plate_tap_timer = lv_timer_create(plate_tap_timeout, PLATE_DOUBLE_TAP_MS, NULL);
        lv_timer_pause(plate_tap_timer);

        lv_indev_t *touch = bsp_display_get_input_dev();
        if (touch != NULL) {
            lv_indev_set_long_press_time(touch, KEY_LONG_PRESS_MS);
            lv_indev_set_long_press_repeat_time(touch, KEY_REPEAT_MS);
        }
        bsp_display_unlock();
    }
}

#if AUTO_ROTATE
/* Orientation from gravity: the edge that points up decides. Returns false
 * when the Tab5 lies flat or is not tilted clearly enough towards an edge. */
static bool imu_orientation(const float accel[3], lv_display_rotation_t *rotation)
{
    const float along_long = IMU_LONG_SIGN * accel[IMU_LONG_AXIS];
    const float along_short = IMU_SHORT_SIGN * accel[IMU_SHORT_AXIS];

    if (fabsf(accel[2]) > IMU_FLAT_G) {
        return false;
    }
    if (fabsf(along_long) >= fabsf(along_short)) {
        if (fabsf(along_long) < IMU_TILT_G) {
            return false;
        }
        *rotation = along_long > 0.0f ? LV_DISPLAY_ROTATION_0 : LV_DISPLAY_ROTATION_180;
    } else {
        if (fabsf(along_short) < IMU_TILT_G) {
            return false;
        }
        *rotation = along_short > 0.0f ? LV_DISPLAY_ROTATION_90 : LV_DISPLAY_ROTATION_270;
    }
    return true;
}

static bool imu_read(bmi270_handle_t *imu, float accel[3], lv_display_rotation_t *rotation)
{
    return bmi270_get_acce_data(imu, &accel[0], &accel[1], &accel[2]) == ESP_OK &&
           imu_orientation(accel, rotation);
}

/* Hand a settled orientation to the render task, unless the plate locked it. */
static void imu_publish(lv_display_rotation_t rotation, const float accel[3])
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    if (!viewer.rotation_locked && viewer.rotation != rotation) {
        viewer.rotation = rotation;
        viewer.frame_dirty = true;
        ESP_LOGI(TAG, "Orientation %d (accel x=%.2f y=%.2f z=%.2f)",
                 (int)rotation, (double)accel[0], (double)accel[1], (double)accel[2]);
    }
    xSemaphoreGive(state_mutex);
}

/* Follow the orientation of the Tab5 with its BMI270 accelerometer. At boot
 * the first clear reading decides, while the display is still dark; then a
 * new orientation must hold for IMU_SETTLE_SAMPLES readings before the render
 * task turns the display. */
static void imu_task(void *arg)
{
    (void)arg;
    bmi270_handle_t *imu = NULL;
    const bmi270_config_t imu_config = {
        .acce_odr = BMI270_ACC_ODR_25_HZ,
        .acce_range = BMI270_ACC_RANGE_2_G,
        .gyro_odr = BMI270_GYR_ODR_25_HZ,
        .gyro_range = BMI270_GYR_RANGE_250_DPS,
    };
    /* The BMI270 shares the BSP I2C bus, set up by app_main, with the touch
     * controller. */
    const bmi270_driver_config_t driver_config = {
        .addr = BMI270_I2C_ADDRESS_L,
        .interface = BMI270_USE_I2C,
        .i2c_bus = bsp_i2c_get_handle(),
    };

    if (bmi270_create(&driver_config, &imu) != ESP_OK ||
        bmi270_start(imu, &imu_config) != ESP_OK) {
        ESP_LOGW(TAG, "IMU not available, the orientation stays fixed");
        xSemaphoreGive(orientation_ready);
        vTaskDelete(NULL);
        return;
    }

    float accel[3];
    lv_display_rotation_t rotation;
    lv_display_rotation_t candidate = FIXED_ROTATION;
    uint32_t stable = 0;

    /* Boot: no clear reading means the Tab5 lies flat, keep FIXED_ROTATION. */
    for (uint32_t i = 0; i < IMU_BOOT_SAMPLES; i++) {
        if (imu_read(imu, accel, &rotation)) {
            imu_publish(rotation, accel);
            candidate = rotation;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(IMU_BOOT_PERIOD_MS));
    }
    xSemaphoreGive(orientation_ready);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(IMU_PERIOD_MS));
        if (!imu_read(imu, accel, &rotation)) {
            continue;
        }
        if (rotation != candidate) {
            candidate = rotation;
            stable = 0;
        }
        /* Kept up once settled, so unlocking turns the display at once. */
        if (stable < IMU_SETTLE_SAMPLES) {
            stable++;
        }
        if (stable == IMU_SETTLE_SAMPLES) {
            imu_publish(candidate, accel);
        }
    }
}
#endif

void app_main(void)
{
    state_mutex = xSemaphoreCreateMutex();
    disconnected_sem = xSemaphoreCreateBinary();
    orientation_ready = xSemaphoreCreateBinary();
    outbound_queue = xQueueCreate(OUTBOUND_QUEUE_LENGTH, sizeof(outbound_message_t));
    assert(state_mutex != NULL);
    assert(disconnected_sem != NULL);
    assert(orientation_ready != NULL);
    assert(outbound_queue != NULL);

    parser_reset();
    viewer.rotation = FIXED_ROTATION;
    settings_init();
    /* The first pass of the render task takes the whole state. */
    viewer.frame_dirty = true;

    /* The IMU starts while the display is set up: both use the BSP I2C bus,
     * created once here. */
    ESP_ERROR_CHECK(bsp_i2c_init());
    const esp_err_t gps_result = gps_unit_start(gps_status_received, NULL);
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    viewer.gps_started = gps_result == ESP_OK;
    viewer.gps_error = gps_result != ESP_OK;
    xSemaphoreGive(state_mutex);
    if (gps_result != ESP_OK) {
        ESP_LOGW(TAG, "GPS input unavailable: %s", esp_err_to_name(gps_result));
    }
#if AUTO_ROTATE
    assert(xTaskCreate(imu_task, "viewer-imu", 4096, NULL, 4, NULL) == pdPASS);
#endif
    display_init();

    assert(xTaskCreate(render_task, "viewer-render", 8192, NULL, 5, NULL) == pdPASS);
    assert(xTaskCreate(usb_task, "viewer-usb", 6144, NULL, 8, NULL) == pdPASS);
#if KEY_CLICK
    assert(xTaskCreate(click_task, "viewer-click", 4096, NULL, 3, NULL) == pdPASS);
#endif
}
