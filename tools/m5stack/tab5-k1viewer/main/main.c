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
#include <string.h>

#include "bmi270.h"
#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "usb/cdc_acm_host.h"
#include "uv_k1_skin_layout.h"

#define K1_USB_VID             0x36B7u
#define K1_USB_PID             0xFFFFu
#define RADIO_WIDTH            128u
#define RADIO_HEIGHT           64u
#define RADIO_FRAME_BYTES      ((RADIO_WIDTH * RADIO_HEIGHT) / 8u)
#define K1VIEWER_MAX_PAYLOAD   2048u
#define K1VIEWER_KEEPALIVE_MS  200u
#define USB_OPEN_TIMEOUT_MS    1000u
#define USB_TX_TIMEOUT_MS      100u
#define AUTO_ROTATE            1            /* follow the Tab5 orientation with its IMU */
#define FIXED_ROTATION         LV_DISPLAY_ROTATION_0    /* used when AUTO_ROTATE is 0 */
/* Landscape rotation that puts the USB-A port on the left: it shows the screen
 * alone, the other landscape rotation shows the screen with the keypad. Swap
 * with LV_DISPLAY_ROTATION_90 if they come out the other way round. */
#define SCREEN_ROTATION        LV_DISPLAY_ROTATION_270
#define IMU_PERIOD_MS          100u
#define IMU_SETTLE_SAMPLES     5u           /* same orientation for 0.5 s before turning */
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
#define KEY_QUEUE_LENGTH       16u
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
#define BACKGROUND_COLOR       0x0C0D0Fu
#define KEY_PRESSED_COLOR      0xFFFFFFu
#define KEY_LONG_COLOR         0x3D93E6u
#define SIDE_KEY_GLOW          18

enum {
    K1VIEWER_TYPE_SCREENSHOT = 0x01,
    K1VIEWER_TYPE_DIFF = 0x02,
    K1VIEWER_TYPE_KEY = 0x03,
    K1VIEWER_TYPE_KEY_LONG = 0x04,
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

typedef struct {
    uint8_t framebuffer[RADIO_FRAME_BYTES];
    uint8_t flags;
    uint8_t palette;
    uint32_t frame_count;
    bool usb_connected;
    bool frame_dirty;
    lv_display_rotation_t rotation;
} viewer_state_t;

typedef struct {
    uint8_t code;
    bool long_press;
} radio_key_t;

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
} view_t;

static const char *TAG = "tab5-k1viewer";
static const viewer_palette_t palettes[PALETTE_COUNT] = {
    [PALETTE_K1] = {.foreground = 0x17191Bu, .background = 0xE2E6DFu},
    [PALETTE_GREY] = {.foreground = 0x2A213Fu, .background = 0xE5E0ECu},
    [PALETTE_ORANGE] = {.foreground = 0x41310Au, .background = 0xFFC125u},
    [PALETTE_BLUE] = {.foreground = 0x07223Au, .background = 0x1C86E4u},
};
static const led_colors_t led_colors[LED_COUNT] = {
    [LED_GREEN] = {.core = 0x16C94Au, .center = 0xEAFFEFu},
    [LED_RED] = {.core = 0xE5281Eu, .center = 0xFFE3DFu},
};

/* PNGs from assets/ as raw RGB565, linked by main/CMakeLists.txt. */
extern const uint8_t uv_k1_skin_rgb565_start[];
extern const uint8_t uv_k1_skin_rgb565_end[];
extern const uint8_t uv_k1_landscape_skin_rgb565_start[];
extern const uint8_t uv_k1_landscape_skin_rgb565_end[];
extern const uint8_t uv_k1_screen_skin_rgb565_start[];
extern const uint8_t uv_k1_screen_skin_rgb565_end[];
extern const uint8_t k1_connect_icon_rgb565_start[];
extern const uint8_t k1_connect_icon_rgb565_end[];
extern const uint8_t k1_connect_text_rgb565_start[];
extern const uint8_t k1_connect_text_rgb565_end[];

static SemaphoreHandle_t state_mutex;
static SemaphoreHandle_t disconnected_sem;
static QueueHandle_t key_queue;
static viewer_parser_t parser;
static viewer_state_t viewer;
static lv_display_t *display;
static lv_image_dsc_t connect_icon_image;
static lv_image_dsc_t connect_text_image;
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
        memset(viewer.framebuffer, 0, sizeof(viewer.framebuffer));
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

/* Draw the radio LCD into the glass of a view: each LCD pixel is lcd_cell_w
 * screen pixels wide and 1.5 times as tall (the real K1 aspect), with a soft
 * drop shadow one pixel right and LCD_SHADOW_DY down. In deep sleep the glass
 * darkens, as when the radio turns its backlight off. */
static void render_radio_frame(const view_t *view, const uint8_t *framebuffer,
                               uint8_t palette_id, bool asleep)
{
    const skin_layout_t *layout = view->layout;
    const skin_rect_t *glass = &layout->glass;
    const skin_rect_t *lcd = &layout->lcd;

    if (palette_id >= PALETTE_COUNT) {
        palette_id = PALETTE_K1;
    }
    const viewer_palette_t *palette = &palettes[palette_id];
    const uint32_t background = asleep
        ? mix_hex(palette->background, LCD_SLEEP_COLOR, LCD_SLEEP_MIX)
        : palette->background;
    const uint32_t inactive_hex =
        mix_hex(background, palette->foreground, LCD_INACTIVE_MIX);
    const uint16_t glass_color = rgb565_hex(background);
    const uint16_t inactive = rgb565_hex(inactive_hex);
    const uint16_t shadow =
        rgb565_hex(mix_hex(inactive_hex, palette->foreground, LCD_SHADOW_MIX));
    const uint16_t foreground = rgb565_hex(palette->foreground);

    for (int32_t y = 0; y < glass->h; y++) {
        uint16_t *row = &view->lcd_pixels[y * view->lcd_stride];
        for (int32_t x = 0; x < glass->w; x++) {
            row[x] = glass_color;
        }
    }

    for (int32_t sy = 0; sy < lcd->h; sy++) {
        const uint32_t ly = (uint32_t)sy * layout->lcd_row_num / layout->lcd_row_den;
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
                *out++ = lit ? foreground
                       : ((c == 0 ? shadow_left : shadow_here) ? shadow : inactive);
            }
        }
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

static void radio_key_send(uint8_t code, bool long_press)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    const bool connected = viewer.usb_connected;
    xSemaphoreGive(state_mutex);

    if (!connected) {
        return;
    }

    const radio_key_t key = {.code = code, .long_press = long_press};
    if (xQueueSend(key_queue, &key, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Key queue full, key 0x%02X dropped", code);
    }
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

static void render_task(void *arg)
{
    (void)arg;
    uint8_t framebuffer[RADIO_FRAME_BYTES] = {0};
    uint8_t flags = 0;
    uint8_t palette = PALETTE_K1;
    bool connected = false;
    bool shown_connected = false;
    bool redraw = true;
    lv_display_rotation_t rotation = lv_display_get_rotation(display);
    lv_display_rotation_t shown_rotation = rotation;

    while (true) {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        if (viewer.frame_dirty) {
            memcpy(framebuffer, viewer.framebuffer, sizeof(framebuffer));
            flags = viewer.flags;
            palette = viewer.palette;
            connected = viewer.usb_connected;
            rotation = viewer.rotation;
            viewer.frame_dirty = false;
            redraw = true;
        }
        xSemaphoreGive(state_mutex);

        if (redraw) {
            const bool asleep = (flags & K1VIEWER_FLAG_DEEP_SLEEP) != 0;
            const bool led_red = connected && (flags & K1VIEWER_FLAG_LED_RED) != 0;
            const bool led_green =
                connected && !led_red && (flags & K1VIEWER_FLAG_LED_GREEN) != 0;
            const bool torch = connected && (flags & K1VIEWER_FLAG_FLASHLIGHT) != 0;

            if (bsp_display_lock(1000)) {
                view_t *view = view_for_rotation(rotation);
                if (rotation != shown_rotation) {
                    bsp_display_rotate(display, rotation);
                    lv_screen_load(view->screen);
                    shown_rotation = rotation;
                }
                render_radio_frame(view, framebuffer, palette, asleep);
                lv_obj_invalidate(view->lcd_canvas);
                for (uint8_t i = 0; i < VIEW_COUNT; i++) {
                    lv_obj_set_hidden(views[i].led_canvases[LED_RED], !led_red);
                    lv_obj_set_hidden(views[i].led_canvases[LED_GREEN], !led_green);
                    lv_obj_set_hidden(views[i].torch_canvas, !torch);
                }
                if (connected != shown_connected) {
                    connect_screen_show(!connected);
                    shown_connected = connected;
                }
                bsp_display_unlock();
            }
            redraw = false;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void palette_event_callback(lv_event_t *event)
{
    (void)event;

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    viewer.palette = (uint8_t)((viewer.palette + 1u) % PALETTE_COUNT);
    viewer.frame_dirty = true;
    xSemaphoreGive(state_mutex);
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
        xQueueReset(key_queue);

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
        usb_send(device, keepalive, sizeof(keepalive));

        /* Only this task writes to the K1: touch keys arrive through the queue,
         * and a keepalive goes out whenever no key was sent for a while. */
        while (xSemaphoreTake(disconnected_sem, 0) != pdTRUE) {
            radio_key_t key;
            if (xQueueReceive(key_queue, &key, pdMS_TO_TICKS(K1VIEWER_KEEPALIVE_MS)) == pdTRUE) {
                const uint8_t packet[] = {
                    0xAA, 0x55,
                    key.long_press ? K1VIEWER_TYPE_KEY_LONG : K1VIEWER_TYPE_KEY,
                    key.code,
                };
                usb_send(device, packet, sizeof(packet));
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

/* Black screen with the "connect your UV-K1" icon and message, shown while no
 * radio is connected. It covers the radio and swallows its touches. */
static lv_obj_t *create_connect_screen(const view_t *view)
{
    const skin_layout_t *layout = view->layout;
    lv_obj_t *screen = lv_obj_create(view->screen);

    lv_obj_remove_style_all(screen);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_size(screen, layout->width, layout->height);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_click_focusable(screen, false);

    if (connect_icon_image.data != NULL) {
        lv_obj_t *icon = lv_image_create(screen);
        lv_image_set_src(icon, &connect_icon_image);
        lv_obj_set_pos(icon, layout->connect_icon_x, layout->connect_icon_y);

        /* Slow breathing while waiting for the radio. */
        lv_anim_t pulse;
        lv_anim_init(&pulse);
        lv_anim_set_var(&pulse, icon);
        lv_anim_set_exec_cb(&pulse, connect_icon_pulse_cb);
        lv_anim_set_values(&pulse, LV_OPA_COVER, CONNECT_PULSE_MIN_OPA);
        lv_anim_set_duration(&pulse, CONNECT_PULSE_MS);
        lv_anim_set_reverse_duration(&pulse, CONNECT_PULSE_MS);
        lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&pulse, lv_anim_path_ease_in_out);
        lv_anim_start(&pulse);
    }

    if (connect_text_image.data != NULL) {
        lv_obj_t *text = lv_image_create(screen);
        lv_image_set_src(text, &connect_text_image);
        lv_obj_set_pos(text, layout->connect_text_x, layout->connect_text_y);
    }
    return screen;
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

    /* The radio LCD replaces the glass of the skin; tap it to change palette. */
    view->lcd_canvas = lv_canvas_create(view->screen);
    lv_canvas_set_buffer(view->lcd_canvas, view->lcd_pixels, glass->w, glass->h,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(view->lcd_canvas, glass->x, glass->y);
    lv_obj_set_clickable(view->lcd_canvas, true);
    lv_obj_add_event_cb(view->lcd_canvas, palette_event_callback, LV_EVENT_CLICKED, NULL);

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
    view->connect_screen = create_connect_screen(view);
}

static void display_init(lv_display_rotation_t rotation)
{
    display = bsp_display_start();
    assert(display != NULL);
    ESP_ERROR_CHECK(bsp_display_backlight_on());

    rgb565_image_init(&connect_icon_image, "connect icon",
                      k1_connect_icon_rgb565_start, k1_connect_icon_rgb565_end,
                      SKIN_CONNECT_ICON_W, SKIN_CONNECT_ICON_H);
    rgb565_image_init(&connect_text_image, "connect text",
                      k1_connect_text_rgb565_start, k1_connect_text_rgb565_end,
                      SKIN_CONNECT_TEXT_W, SKIN_CONNECT_TEXT_H);

    if (bsp_display_lock(0)) {
        for (uint8_t i = 0; i < VIEW_COUNT; i++) {
            create_view(&views[i]);
        }

        lv_indev_t *touch = bsp_display_get_input_dev();
        if (touch != NULL) {
            lv_indev_set_long_press_time(touch, KEY_LONG_PRESS_MS);
            lv_indev_set_long_press_repeat_time(touch, KEY_REPEAT_MS);
        }

        bsp_display_rotate(display, rotation);
        lv_screen_load(view_for_rotation(rotation)->screen);
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

/* Follow the orientation of the Tab5 with its BMI270 accelerometer. A new
 * orientation must hold for IMU_SETTLE_SAMPLES readings before the render
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

    /* The BMI270 shares the BSP I2C bus with the touch controller. */
    const esp_err_t bus_result = bsp_i2c_init();
    const bmi270_driver_config_t driver_config = {
        .addr = BMI270_I2C_ADDRESS_L,
        .interface = BMI270_USE_I2C,
        .i2c_bus = bsp_i2c_get_handle(),
    };

    if (bus_result != ESP_OK ||
        bmi270_create(&driver_config, &imu) != ESP_OK ||
        bmi270_start(imu, &imu_config) != ESP_OK) {
        ESP_LOGW(TAG, "IMU not available, the orientation stays fixed");
        vTaskDelete(NULL);
        return;
    }

    lv_display_rotation_t candidate = FIXED_ROTATION;
    uint32_t stable = 0;

    while (true) {
        float accel[3];
        lv_display_rotation_t rotation;

        if (bmi270_get_acce_data(imu, &accel[0], &accel[1], &accel[2]) == ESP_OK &&
            imu_orientation(accel, &rotation)) {
            if (rotation != candidate) {
                candidate = rotation;
                stable = 0;
            }
            if (++stable == IMU_SETTLE_SAMPLES) {
                xSemaphoreTake(state_mutex, portMAX_DELAY);
                if (viewer.rotation != candidate) {
                    viewer.rotation = candidate;
                    viewer.frame_dirty = true;
                    ESP_LOGI(TAG, "Orientation %d (accel x=%.2f y=%.2f z=%.2f)",
                             (int)candidate, (double)accel[0], (double)accel[1], (double)accel[2]);
                }
                xSemaphoreGive(state_mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(IMU_PERIOD_MS));
    }
}
#endif

void app_main(void)
{
    state_mutex = xSemaphoreCreateMutex();
    disconnected_sem = xSemaphoreCreateBinary();
    key_queue = xQueueCreate(KEY_QUEUE_LENGTH, sizeof(radio_key_t));
    assert(state_mutex != NULL);
    assert(disconnected_sem != NULL);
    assert(key_queue != NULL);

    parser_reset();
    viewer.rotation = FIXED_ROTATION;
    display_init(viewer.rotation);

    assert(xTaskCreate(render_task, "viewer-render", 8192, NULL, 5, NULL) == pdPASS);
    assert(xTaskCreate(usb_task, "viewer-usb", 6144, NULL, 8, NULL) == pdPASS);
#if AUTO_ROTATE
    assert(xTaskCreate(imu_task, "viewer-imu", 4096, NULL, 4, NULL) == pdPASS);
#endif
}
