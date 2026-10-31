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
#define DISPLAY_ROTATION       LV_DISPLAY_ROTATION_0    /* native portrait; _180 flips it */
#define KEY_LONG_PRESS_MS      500u
#define KEY_REPEAT_MS          120u
#define KEY_QUEUE_LENGTH       16u
#define LCD_INACTIVE_MIX       0.05f
#define LCD_SHADOW_MIX         0.18f
#define LCD_SHADOW_DX          1u
#define LCD_SHADOW_DY          2u
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
} viewer_state_t;

typedef struct {
    uint8_t code;
    bool long_press;
} radio_key_t;

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
extern const uint8_t k1_connect_icon_rgb565_start[];
extern const uint8_t k1_connect_icon_rgb565_end[];
extern const uint8_t k1_connect_text_rgb565_start[];
extern const uint8_t k1_connect_text_rgb565_end[];

static SemaphoreHandle_t state_mutex;
static SemaphoreHandle_t disconnected_sem;
static QueueHandle_t key_queue;
static viewer_parser_t parser;
static viewer_state_t viewer;
static lv_image_dsc_t skin_image;
static lv_image_dsc_t connect_icon_image;
static lv_image_dsc_t connect_text_image;
static uint16_t *lcd_pixels;
static uint32_t lcd_stride;
static lv_obj_t *lcd_canvas;
static lv_obj_t *led_canvases[LED_COUNT];
static lv_obj_t *torch_canvas;
static lv_obj_t *connect_screen;

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

/* Draw the radio LCD into the glass: 3 px per column, 4.5 px per row (the
 * K1 pixels are 1.5 times taller than wide), with a soft drop shadow. In
 * deep sleep the glass darkens, as when the radio turns its backlight off. */
static void render_radio_frame(const uint8_t *framebuffer, uint8_t palette_id, bool asleep)
{
    if (palette_id >= PALETTE_COUNT) {
        palette_id = PALETTE_K1;
    }
    const viewer_palette_t *palette = &palettes[palette_id];
    const uint32_t background = asleep
        ? mix_hex(palette->background, LCD_SLEEP_COLOR, LCD_SLEEP_MIX)
        : palette->background;
    const uint32_t inactive_hex =
        mix_hex(background, palette->foreground, LCD_INACTIVE_MIX);
    const uint16_t glass = rgb565_hex(background);
    const uint16_t inactive = rgb565_hex(inactive_hex);
    const uint16_t shadow =
        rgb565_hex(mix_hex(inactive_hex, palette->foreground, LCD_SHADOW_MIX));
    const uint16_t foreground = rgb565_hex(palette->foreground);

    for (uint32_t y = 0; y < SKIN_GLASS_H; y++) {
        uint16_t *row = &lcd_pixels[y * lcd_stride];
        for (uint32_t x = 0; x < SKIN_GLASS_W; x++) {
            row[x] = glass;
        }
    }

    for (uint32_t sy = 0; sy < SKIN_LCD_H; sy++) {
        const uint32_t ly = sy * SKIN_LCD_ROW_NUM / SKIN_LCD_ROW_DEN;
        const bool shadow_row = sy >= LCD_SHADOW_DY;
        const uint32_t shadow_ly =
            shadow_row ? (sy - LCD_SHADOW_DY) * SKIN_LCD_ROW_NUM / SKIN_LCD_ROW_DEN : 0;
        uint16_t *row = &lcd_pixels[(SKIN_LCD_Y + sy) * lcd_stride + SKIN_LCD_X];

        for (uint32_t sx = 0; sx < SKIN_LCD_W; sx++) {
            uint16_t color = inactive;
            if (radio_pixel(framebuffer, sx / SKIN_LCD_CELL_W, ly)) {
                color = foreground;
            } else if (shadow_row && sx >= LCD_SHADOW_DX &&
                       radio_pixel(framebuffer,
                                   (sx - LCD_SHADOW_DX) / SKIN_LCD_CELL_W,
                                   shadow_ly)) {
                color = shadow;
            }
            row[sx] = color;
        }
    }
}

/* Draw the lit RX/TX light pipe (trapezoid) with its glow, as ARGB8888. */
static void led_render(uint32_t *pixels, uint32_t stride, const led_colors_t *colors)
{
    const float height = SKIN_LED_BOTTOM_Y - SKIN_LED_TOP_Y;
    const float half_width = (SKIN_LED_BOTTOM_X1 - SKIN_LED_BOTTOM_X0) / 2.0f;
    const float center_x = (SKIN_LED_TOP_X0 + SKIN_LED_TOP_X1 +
                            SKIN_LED_BOTTOM_X0 + SKIN_LED_BOTTOM_X1) / 4.0f;
    const float center_y = SKIN_LED_TOP_Y + height * 0.4f;

    for (uint32_t y = 0; y < SKIN_LED_BOX_H; y++) {
        const float py = (float)(SKIN_LED_BOX_Y + (int32_t)y) + 0.5f;
        const float t = clampf((py - SKIN_LED_TOP_Y) / height, 0.0f, 1.0f);
        const float left = SKIN_LED_TOP_X0 + (SKIN_LED_BOTTOM_X0 - SKIN_LED_TOP_X0) * t;
        const float right = SKIN_LED_TOP_X1 + (SKIN_LED_BOTTOM_X1 - SKIN_LED_TOP_X1) * t;

        for (uint32_t x = 0; x < SKIN_LED_BOX_W; x++) {
            const float px = (float)(SKIN_LED_BOX_X + (int32_t)x) + 0.5f;
            const float dx = fmaxf(fmaxf(left - px, px - right), 0.0f);
            const float dy = fmaxf(fmaxf(SKIN_LED_TOP_Y - py, py - SKIN_LED_BOTTOM_Y), 0.0f);
            const float inside = fminf(fminf(px - left, right - px),
                                       fminf(py - SKIN_LED_TOP_Y, SKIN_LED_BOTTOM_Y - py));
            const float coverage = clampf(inside + 0.5f, 0.0f, 1.0f);
            const float fade = clampf(1.0f - sqrtf(dx * dx + dy * dy) / SKIN_LED_GLOW,
                                      0.0f, 1.0f);
            const float glow = LED_GLOW_ALPHA * fade * fade;
            const float alpha = coverage + (1.0f - coverage) * glow;
            const float distance = hypotf((px - center_x) / half_width, (py - center_y) / height);
            uint32_t core = mix_hex(colors->center, colors->core, clampf(distance, 0.0f, 1.0f));

            if (py - SKIN_LED_TOP_Y > 1.5f && py - SKIN_LED_TOP_Y < 3.5f &&
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

/* Draw the lit torch as ARGB8888: the visible top of the dome, a halo, and a
 * soft beam fanning out up to the top of the screen, between the antenna and
 * the knob. */
static void torch_render(uint32_t *pixels, uint32_t stride)
{
    const float halo_rx = SKIN_TORCH_RX + SKIN_TORCH_HALO;
    const float halo_ry = SKIN_TORCH_RY + SKIN_TORCH_HALO;

    for (uint32_t y = 0; y < SKIN_TORCH_BOX_H; y++) {
        const float py = (float)(SKIN_TORCH_BOX_Y + (int32_t)y) + 0.5f;
        const float dy = py - SKIN_TORCH_Y;

        for (uint32_t x = 0; x < SKIN_TORCH_BOX_W; x++) {
            const float px = (float)(SKIN_TORCH_BOX_X + (int32_t)x) + 0.5f;
            const float dx = px - SKIN_TORCH_X;

            /* Dome, cut by the housing edge like on the skin. */
            const float dome = sqrtf((dx * dx) / (SKIN_TORCH_RX * SKIN_TORCH_RX) +
                                     (dy * dy) / (SKIN_TORCH_RY * SKIN_TORCH_RY));
            const float core = clampf((1.0f - dome) * SKIN_TORCH_RY + 0.5f, 0.0f, 1.0f) *
                               clampf(SKIN_TORCH_TOP - py + 0.5f, 0.0f, 1.0f);

            const float halo_fade = clampf(1.0f - sqrtf((dx * dx) / (halo_rx * halo_rx) +
                                                        (dy * dy) / (halo_ry * halo_ry)),
                                           0.0f, 1.0f);
            const float halo = TORCH_HALO_ALPHA * halo_fade * halo_fade;

            float beam = 0.0f;
            if (dy < 0.0f) {
                const float rise = -dy;
                const float across = clampf(1.0f - fabsf(dx) / (SKIN_TORCH_RX + rise * SKIN_TORCH_BEAM_SPREAD),
                                            0.0f, 1.0f);
                beam = TORCH_BEAM_ALPHA * (1.0f - rise / SKIN_TORCH_Y) * across * across;
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
    lv_obj_set_hidden(connect_screen, true);
}

static void connect_icon_pulse_cb(void *obj, int32_t value)
{
    lv_obj_set_style_image_opa(obj, (lv_opa_t)value, 0);
}

/* Fade the black "connect the radio" screen in or out over the radio. Once
 * faded out it is hidden, so it no longer catches the touches. */
static void connect_screen_show(bool show)
{
    lv_anim_t anim;

    lv_anim_delete(connect_screen, connect_screen_opa_cb);
    lv_obj_set_hidden(connect_screen, false);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, connect_screen);
    lv_anim_set_exec_cb(&anim, connect_screen_opa_cb);
    lv_anim_set_values(&anim, lv_obj_get_style_opa(connect_screen, 0),
                       show ? LV_OPA_COVER : LV_OPA_TRANSP);
    lv_anim_set_duration(&anim, CONNECT_FADE_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    if (!show) {
        lv_anim_set_completed_cb(&anim, connect_screen_faded_out);
    }
    lv_anim_start(&anim);
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

    while (true) {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        if (viewer.frame_dirty) {
            memcpy(framebuffer, viewer.framebuffer, sizeof(framebuffer));
            flags = viewer.flags;
            palette = viewer.palette;
            connected = viewer.usb_connected;
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
                render_radio_frame(framebuffer, palette, asleep);
                lv_obj_invalidate(lcd_canvas);
                lv_obj_set_hidden(led_canvases[LED_RED], !led_red);
                lv_obj_set_hidden(led_canvases[LED_GREEN], !led_green);
                lv_obj_set_hidden(torch_canvas, !torch);
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
static lv_obj_t *create_connect_screen(lv_obj_t *root)
{
    lv_obj_t *screen = lv_obj_create(root);

    lv_obj_remove_style_all(screen);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_size(screen, SKIN_WIDTH, SKIN_HEIGHT);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_click_focusable(screen, false);

    if (rgb565_image_init(&connect_icon_image, "connect icon",
                          k1_connect_icon_rgb565_start, k1_connect_icon_rgb565_end,
                          SKIN_CONNECT_ICON_W, SKIN_CONNECT_ICON_H)) {
        lv_obj_t *icon = lv_image_create(screen);
        lv_image_set_src(icon, &connect_icon_image);
        lv_obj_set_pos(icon, SKIN_CONNECT_ICON_X, SKIN_CONNECT_ICON_Y);

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

    if (rgb565_image_init(&connect_text_image, "connect text",
                          k1_connect_text_rgb565_start, k1_connect_text_rgb565_end,
                          SKIN_CONNECT_TEXT_W, SKIN_CONNECT_TEXT_H)) {
        lv_obj_t *text = lv_image_create(screen);
        lv_image_set_src(text, &connect_text_image);
        lv_obj_set_pos(text, SKIN_CONNECT_TEXT_X, SKIN_CONNECT_TEXT_Y);
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

static void create_key_targets(lv_obj_t *root)
{
    for (size_t i = 0; i < SKIN_KEY_COUNT; i++) {
        const skin_key_t *key = &skin_keys[i];
        lv_obj_t *target = create_key_overlay(root, key->x, key->y, key->w, key->h,
                                              SKIN_KEY_RADIUS);
        lv_obj_add_event_cb(target, key_event_callback, LV_EVENT_ALL,
                            (void *)(uintptr_t)key->code);
    }

    /* F1 and F2 are narrow on the rail: a wider transparent touch area takes
     * the press and passes its states to the cover of the drawn key, which
     * also glows so the press shows around the finger. */
    for (size_t i = 0; i < SKIN_SIDE_KEY_COUNT; i++) {
        const skin_side_key_t *key = &skin_side_keys[i];
        lv_obj_t *area = lv_obj_create(root);

        lv_obj_remove_style_all(area);
        lv_obj_set_pos(area, key->hit_x, key->hit_y);
        lv_obj_set_size(area, key->hit_w, key->hit_h);
        lv_obj_set_scrollable(area, false);
        lv_obj_set_scroll_on_focus(area, false);
        lv_obj_set_click_focusable(area, false);
        lv_obj_set_state_trickle(area, true);
        lv_obj_set_overflow_visible(area, true);
        lv_obj_add_event_cb(area, key_event_callback, LV_EVENT_ALL,
                            (void *)(uintptr_t)key->code);

        lv_obj_t *overlay = create_key_overlay(area, key->x - key->hit_x, key->y - key->hit_y,
                                               key->w, key->h, SKIN_SIDE_KEY_RADIUS);
        lv_obj_set_clickable(overlay, false);
        lv_obj_set_style_shadow_color(overlay, lv_color_hex(KEY_LONG_COLOR), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(overlay, SIDE_KEY_GLOW, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_opa(overlay, LV_OPA_60, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_opa(overlay, LV_OPA_COVER, LV_STATE_PRESSED | LV_STATE_USER_1);
    }
}

static void display_init(void)
{
    lv_display_t *display = bsp_display_start();
    assert(display != NULL);

    bsp_display_rotate(display, DISPLAY_ROTATION);
    ESP_ERROR_CHECK(bsp_display_backlight_on());

    const bool skin_ready =
        rgb565_image_init(&skin_image, "skin", uv_k1_skin_rgb565_start,
                          uv_k1_skin_rgb565_end, SKIN_WIDTH, SKIN_HEIGHT);

    if (bsp_display_lock(0)) {
        lv_obj_t *root = lv_screen_active();
        lv_obj_set_style_bg_color(root, lv_color_hex(BACKGROUND_COLOR), 0);
        lv_obj_set_scrollable(root, false);

        if (skin_ready) {
            lv_obj_t *skin = lv_image_create(root);
            lv_image_set_src(skin, &skin_image);
            lv_obj_set_pos(skin, 0, 0);
        }

        const uint32_t stride =
            lv_draw_buf_width_to_stride(SKIN_GLASS_W, LV_COLOR_FORMAT_RGB565);
        lcd_stride = stride / sizeof(uint16_t);
        lcd_pixels = heap_caps_aligned_calloc(
            64, stride, SKIN_GLASS_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        assert(lcd_pixels != NULL);

        /* The radio LCD replaces the glass of the skin; tap it to change palette. */
        lcd_canvas = lv_canvas_create(root);
        lv_canvas_set_buffer(lcd_canvas, lcd_pixels, SKIN_GLASS_W, SKIN_GLASS_H,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(lcd_canvas, SKIN_GLASS_X, SKIN_GLASS_Y);
        lv_obj_set_clickable(lcd_canvas, true);
        lv_obj_add_event_cb(lcd_canvas, palette_event_callback, LV_EVENT_CLICKED, NULL);

        uint32_t *pixels;
        uint32_t stride_px;
        for (uint8_t i = 0; i < LED_COUNT; i++) {
            led_canvases[i] = create_light_canvas(root, SKIN_LED_BOX_X, SKIN_LED_BOX_Y,
                                                  SKIN_LED_BOX_W, SKIN_LED_BOX_H,
                                                  &pixels, &stride_px);
            led_render(pixels, stride_px, &led_colors[i]);
        }
        torch_canvas = create_light_canvas(root, SKIN_TORCH_BOX_X, SKIN_TORCH_BOX_Y,
                                           SKIN_TORCH_BOX_W, SKIN_TORCH_BOX_H,
                                           &pixels, &stride_px);
        torch_render(pixels, stride_px);

        create_key_targets(root);
        connect_screen = create_connect_screen(root);

        lv_indev_t *touch = bsp_display_get_input_dev();
        if (touch != NULL) {
            lv_indev_set_long_press_time(touch, KEY_LONG_PRESS_MS);
            lv_indev_set_long_press_repeat_time(touch, KEY_REPEAT_MS);
        }

        bsp_display_unlock();
    }
}

void app_main(void)
{
    state_mutex = xSemaphoreCreateMutex();
    disconnected_sem = xSemaphoreCreateBinary();
    key_queue = xQueueCreate(KEY_QUEUE_LENGTH, sizeof(radio_key_t));
    assert(state_mutex != NULL);
    assert(disconnected_sem != NULL);
    assert(key_queue != NULL);

    parser_reset();
    display_init();

    assert(xTaskCreate(render_task, "viewer-render", 8192, NULL, 5, NULL) == pdPASS);
    assert(xTaskCreate(usb_task, "viewer-usb", 6144, NULL, 8, NULL) == pdPASS);
}
