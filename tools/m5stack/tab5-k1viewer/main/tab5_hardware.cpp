/*
 * M5Unified hardware adapter for the Tab5 K1Viewer.
 *
 * Copyright 2026 Armel F4HWN
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tab5_hardware.h"

#include <M5Unified.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

constexpr uint32_t kDisplayWidth = 720;
constexpr uint32_t kDisplayHeight = 1280;
constexpr uint32_t kDisplayBufferLines = 32;
constexpr size_t kDisplayBufferAlign = 128; /* cache line, for the DMA2D reads */
constexpr TickType_t kPanelCopyTimeout = pdMS_TO_TICKS(100);
constexpr uint32_t kLvglTaskStackBytes = 8192;
constexpr uint32_t kTouchReadPeriodMs = 10;
constexpr uint32_t kInternalI2cFrequency = 400000;
constexpr uint8_t kSpeakerEnablePin = 1; /* SPK_EN on the PI4IOE5V6408 at 0x43 */
/* Keep touch processing ahead of the frame renderer so a busy radio screen
 * cannot hide the release between two taps. */
constexpr UBaseType_t kLvglTaskPriority = 6;

#define TAB5_STRINGIFY_VALUE_(value) #value
#define TAB5_STRINGIFY_VALUE(value) TAB5_STRINGIFY_VALUE_(value)
constexpr char kM5UnifiedVersion[] =
    TAB5_STRINGIFY_VALUE(M5UNIFIED_VERSION_MAJOR) "."
    TAB5_STRINGIFY_VALUE(M5UNIFIED_VERSION_MINOR) "."
    TAB5_STRINGIFY_VALUE(M5UNIFIED_VERSION_PATCH);
#undef TAB5_STRINGIFY_VALUE
#undef TAB5_STRINGIFY_VALUE_

const char *const kTag = "tab5-hardware";
SemaphoreHandle_t display_mutex;
lv_display_t *display;
lv_indev_t *touch;

/*
 * Display output. M5GFX writes the DPI frame buffer (PSRAM) with the CPU: a
 * plain memcpy in portrait, but a per-pixel copy when rotated, followed by a
 * cache write-back of the address range it touched. A landscape strip is a
 * column of the portrait panel, so that range covers all 1280 panel lines for
 * every LVGL strip. Strips therefore follow the path of the former BSP
 * instead: rotated in internal RAM, then copied into the frame buffer by the
 * DMA2D through esp_lcd, with no CPU write to PSRAM and no cache write-back.
 * M5GFX keeps the panel set-up and its rotation, which also maps the touch.
 */
esp_lcd_panel_handle_t panel_handle;  /* null: every strip goes through M5GFX */
SemaphoreHandle_t panel_copy_done;
uint16_t *rotate_buffer;
uint8_t display_rotation;             /* M5GFX rotation, 0 to 3 */
bool panel_copy_late;                 /* a copy outlived its timeout */
bool panel_copy_warned;

/* M5GFX keeps the DPI panel handle protected. A pointer to member named
 * through a derived class reads it without modifying M5GFX. */
struct DsiPanelAccess : lgfx::Panel_DSI {
    static esp_lcd_panel_handle_t handle(lgfx::Panel_DSI &panel)
    {
        return panel.*(&DsiPanelAccess::_disp_panel_handle);
    }
};

esp_lcd_panel_handle_t dsi_panel_handle(void)
{
    lgfx::Panel_Device *panel = M5.Display.getPanel();
    if (panel == nullptr || panel->getBus() == nullptr ||
        panel->getBus()->busType() != lgfx::bus_type_t::bus_dsi) {
        return nullptr;
    }
    return DsiPanelAccess::handle(*static_cast<lgfx::Panel_DSI *>(panel));
}

/* Called by esp_lcd once the DMA2D has copied the strip: from its interrupt,
 * or from the caller for a CPU copy. */
bool IRAM_ATTR panel_copy_done_cb(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *,
                                  void *)
{
    if (!xPortInIsrContext()) {
        xSemaphoreGive(panel_copy_done);
        return false;
    }
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(panel_copy_done, &woken);
    return woken == pdTRUE;
}

void *display_buffer_alloc(size_t size)
{
    void *buffer = heap_caps_aligned_alloc(kDisplayBufferAlign, size,
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA |
                                           MALLOC_CAP_8BIT);
    if (buffer == nullptr) {
        buffer = heap_caps_aligned_alloc(kDisplayBufferAlign, size,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return buffer;
}

void panel_copy_warn(const char *message)
{
    if (!panel_copy_warned) {
        panel_copy_warned = true;
        ESP_LOGW(kTag, "%s", message);
    }
}

/* Rotate an LVGL strip exactly as M5GFX's Panel_FrameBufferBase maps the same
 * rotation, then let the DMA2D copy it into the frame buffer. Once this path
 * is in use, M5GFX must not write the frame buffer any more: its CPU cache
 * still holds lines from the boot clear, and a partial write would put them
 * back over the DMA data. A strip that cannot be copied is dropped instead
 * and stays stale until LVGL draws that area again. */
void panel_flush(lv_display_t *lv_display, const lv_area_t *area, const uint16_t *pixels)
{
    /* The driver accepts one copy at a time, and a late one may still read
     * the rotation buffer: let it end first. */
    if (panel_copy_late) {
        if (xSemaphoreTake(panel_copy_done, kPanelCopyTimeout) != pdTRUE) {
            panel_copy_warn("DMA2D copy still running, strip dropped");
            return;
        }
        panel_copy_late = false;
    }

    const uint8_t rotation = display_rotation;
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    const int32_t lw = lv_display_get_horizontal_resolution(lv_display);
    const int32_t lh = lv_display_get_vertical_resolution(lv_display);
    const uint16_t *out = rotate_buffer;
    int32_t px = area->x1;
    int32_t py = area->y1;
    int32_t pw = w;
    int32_t ph = h;

    switch (rotation) {
    case 1: /* logical (x, y) is panel (lh - 1 - y, x) */
        px = lh - 1 - area->y2;
        py = area->x1;
        pw = h;
        ph = w;
        for (int32_t j = 0; j < h; ++j) {
            const uint16_t *src = &pixels[j * w];
            uint16_t *dst = &rotate_buffer[h - 1 - j];
            for (int32_t i = 0; i < w; ++i) {
                dst[i * h] = src[i];
            }
        }
        break;
    case 2: /* logical (x, y) is panel (lw - 1 - x, lh - 1 - y) */
        px = lw - 1 - area->x2;
        py = lh - 1 - area->y2;
        for (int32_t j = 0; j < h; ++j) {
            const uint16_t *src = &pixels[j * w];
            uint16_t *dst = &rotate_buffer[(h - 1 - j) * w + (w - 1)];
            for (int32_t i = 0; i < w; ++i) {
                dst[-i] = src[i];
            }
        }
        break;
    case 3: /* logical (x, y) is panel (y, lw - 1 - x) */
        px = area->y1;
        py = lw - 1 - area->x2;
        pw = h;
        ph = w;
        for (int32_t j = 0; j < h; ++j) {
            const uint16_t *src = &pixels[j * w];
            uint16_t *dst = &rotate_buffer[(w - 1) * h + j];
            for (int32_t i = 0; i < w; ++i) {
                dst[-i * h] = src[i];
            }
        }
        break;
    default:
        out = pixels;
        break;
    }

    if (esp_lcd_panel_draw_bitmap(panel_handle, px, py, px + pw, py + ph, out) != ESP_OK) {
        panel_copy_warn("DMA2D copy refused, strip dropped");
        return;
    }
    /* LVGL reuses its buffer, and the next strip the rotation buffer, as soon
     * as this one is flushed. */
    if (xSemaphoreTake(panel_copy_done, kPanelCopyTimeout) != pdTRUE) {
        panel_copy_late = true;
        panel_copy_warn("DMA2D copy took longer than 100 ms");
    }
}

uint32_t lvgl_tick_get(void)
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000u);
}

void display_flush(lv_display_t *lv_display, const lv_area_t *area, uint8_t *pixels)
{
    if (panel_handle != nullptr) {
        panel_flush(lv_display, area, reinterpret_cast<const uint16_t *>(pixels));
    } else {
        const int32_t width = area->x2 - area->x1 + 1;
        const int32_t height = area->y2 - area->y1 + 1;

        M5.Display.waitDMA();
        M5.Display.pushImage(area->x1, area->y1, width, height,
                             reinterpret_cast<lgfx::rgb565_t *>(pixels));
        M5.Display.waitDMA();
    }
    lv_display_flush_ready(lv_display);
}

void touch_read(lv_indev_t *, lv_indev_data_t *data)
{
    M5.update();
    if (M5.Touch.getCount() == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    const auto point = M5.Touch.getDetail(0);
    if (!point.isPressed()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    data->point.x = point.x;
    data->point.y = point.y;
    data->state = LV_INDEV_STATE_PRESSED;
}

void lvgl_task(void *)
{
    while (true) {
        uint32_t delay_ms = 10;
        if (xSemaphoreTakeRecursive(display_mutex, portMAX_DELAY) == pdTRUE) {
            delay_ms = lv_timer_handler();
            xSemaphoreGiveRecursive(display_mutex);
        }
        if (delay_ms < 2) {
            delay_ms = 2;
        } else if (delay_ms > 20) {
            delay_ms = 20;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

} // namespace

extern "C" esp_err_t tab5_hardware_init(void)
{
    auto config = M5.config();
    config.external_display_value = 0;
    config.clear_display = true;
    config.output_power = true;
    config.internal_imu = true;
    config.internal_rtc = false;
    config.internal_mic = false;
    /* The key clicks drive the ES8388 themselves (tab5_audio.c): M5Unified must
     * not claim the I2S port or set the codec up with its own registers. */
    config.internal_spk = false;
    config.fallback_board = m5::board_t::board_M5Tab5;
    /* M5.begin() saves the backlight level, sets up the panel with the backlight
     * off, then restores the saved level: 127 by default, which would light a
     * blank panel until the renderer has its first picture. Saving 0 keeps the
     * backlight off until the renderer turns it on, as the former BSP did. */
    M5.Display.setBrightness(0);
    M5.begin(config);

    if ((M5.getBoard() != m5::board_t::board_M5Tab5 &&
         M5.getBoard() != m5::board_t::board_M5Tab5X) ||
        M5.Display.width() != kDisplayWidth ||
        M5.Display.height() != kDisplayHeight) {
        ESP_LOGE(kTag, "Unexpected board/display: board=%d size=%dx%d",
                 static_cast<int>(M5.getBoard()), M5.Display.width(), M5.Display.height());
        return ESP_ERR_NOT_SUPPORTED;
    }

    display_mutex = xSemaphoreCreateRecursiveMutex();
    if (display_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    lv_init();
    lv_tick_set_cb(lvgl_tick_get);

    display = lv_display_create(kDisplayWidth, kDisplayHeight);
    if (display == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    const size_t buffer_size =
        kDisplayWidth * kDisplayBufferLines * sizeof(lv_color16_t);
    void *buffer = display_buffer_alloc(buffer_size);
    if (buffer == nullptr) {
        ESP_LOGE(kTag, "No memory for the LVGL display buffer");
        return ESP_ERR_NO_MEM;
    }
    /* Same size: a rotated strip has as many pixels as the LVGL one. The DMA2D
     * path needs all of these, or the display stays on M5GFX for good. */
    rotate_buffer = static_cast<uint16_t *>(display_buffer_alloc(buffer_size));
    panel_copy_done = xSemaphoreCreateBinary();
    panel_handle = rotate_buffer != nullptr && panel_copy_done != nullptr
                       ? dsi_panel_handle()
                       : nullptr;
    if (panel_handle != nullptr) {
        esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
        callbacks.on_color_trans_done = panel_copy_done_cb;
        if (esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &callbacks, nullptr) !=
            ESP_OK) {
            panel_handle = nullptr;
        }
    }
    ESP_LOGI(kTag, "Display output: %s, LVGL buffer in %s",
             panel_handle != nullptr ? "DMA2D" : "M5GFX",
             esp_ptr_internal(buffer) ? "internal RAM" : "PSRAM");
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, nullptr, buffer_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, display_flush);

    touch = lv_indev_create();
    if (touch == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch, display);
    lv_indev_set_read_cb(touch, touch_read);
    lv_timer_set_period(lv_indev_get_read_timer(touch), kTouchReadPeriodMs);

    /* Use the lower Tab5 charger step while the display and USB host are on. */
    if (!M5.Power.setChargeCurrent(500)) {
        ESP_LOGW(kTag, "Could not select the 500 mA charge current");
    }
    if (!M5.Power.setBatteryCharge(true)) {
        ESP_LOGW(kTag, "Could not enable battery charging");
    }

    if (xTaskCreate(lvgl_task, "viewer-lvgl", kLvglTaskStackBytes, nullptr,
                    kLvglTaskPriority, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(kTag, "M5Unified ready: board=%d display=%dx%d touch=%s imu=%s",
             static_cast<int>(M5.getBoard()), M5.Display.width(), M5.Display.height(),
             M5.Touch.isEnabled() ? "yes" : "no",
             M5.Imu.isEnabled() ? "yes" : "no");
    return ESP_OK;
}

extern "C" lv_display_t *tab5_display_get(void)
{
    return display;
}

extern "C" lv_indev_t *tab5_touch_get(void)
{
    return touch;
}

extern "C" bool tab5_display_lock(uint32_t timeout_ms)
{
    return display_mutex != nullptr &&
           xSemaphoreTakeRecursive(display_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

extern "C" void tab5_display_unlock(void)
{
    xSemaphoreGiveRecursive(display_mutex);
}

extern "C" void tab5_display_rotate(lv_display_rotation_t rotation)
{
    M5.Display.waitDMA();
    /* M5GFX still rotates the touch points; panel_flush() rotates the pixels
     * the same way. */
    M5.Display.setRotation(static_cast<uint8_t>(rotation));
    display_rotation = static_cast<uint8_t>(rotation) & 3u;
    lv_display_set_resolution(display, M5.Display.width(), M5.Display.height());
}

extern "C" void tab5_display_brightness_set(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    M5.Display.setBrightness(static_cast<uint8_t>((percent * 255u + 50u) / 100u));
}

extern "C" const char *tab5_m5unified_version(void)
{
    return kM5UnifiedVersion;
}

extern "C" bool tab5_imu_available(void)
{
    return M5.Imu.isEnabled();
}

extern "C" bool tab5_imu_read_accel(float accel[3])
{
    return M5.Imu.isEnabled() &&
           M5.Imu.getAccelData(&accel[0], &accel[1], &accel[2]);
}

extern "C" bool tab5_internal_i2c_read(uint8_t address, uint8_t reg, uint8_t *data,
                                       size_t length)
{
    return M5.In_I2C.readRegister(address, reg, data, length, kInternalI2cFrequency);
}

extern "C" bool tab5_internal_i2c_write(uint8_t address, uint8_t reg, const uint8_t *data,
                                        size_t length)
{
    return M5.In_I2C.writeRegister(address, reg, data, length, kInternalI2cFrequency);
}

extern "C" bool tab5_speaker_amp_enable(void)
{
    auto &expander = M5.getIOExpander(0);
    return expander.setDirection(kSpeakerEnablePin, true) &&
           expander.setHighImpedance(kSpeakerEnablePin, false) &&
           expander.digitalWrite(kSpeakerEnablePin, true);
}

extern "C" esp_err_t tab5_usb_power_enable(void)
{
    return M5.Power.setExtOutput(true, m5::ext_port_mask_t::ext_USB)
               ? ESP_OK
               : ESP_FAIL;
}

extern "C" esp_err_t tab5_external_power_enable(void)
{
    return M5.Power.setExtOutput(true, m5::ext_port_mask_t::ext_PA)
               ? ESP_OK
               : ESP_FAIL;
}
