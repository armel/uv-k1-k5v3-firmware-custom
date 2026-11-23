/*
 * Tab5 speaker path for the K1Viewer key clicks.
 *
 * M5Unified's speaker is left off on purpose. Its mixer task keeps the I2S DMA
 * queue full of silence for a while after each sound, so a click requested
 * shortly after another one waits behind up to 43 ms of zeros; it also sets the
 * ES8388 up with its own register values. The clicks therefore keep the path of
 * the former Tab5 BSP: an I2S channel written directly through esp_codec_dev,
 * with the same ES8388 settings, where a new click starts within one 5 ms DMA
 * buffer. Only the ES8388 register accesses and the amplifier enable go
 * through M5Unified, which owns the internal I2C bus and its I/O expanders.
 *
 * Copyright 2026 Armel F4HWN
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tab5_audio.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "tab5_hardware.h"

#define AUDIO_I2S_PORT    I2S_NUM_1      /* the former BSP default (CONFIG_BSP_I2S_NUM) */
#define AUDIO_SAMPLE_RATE 48000u
#define AUDIO_I2S_MCLK    GPIO_NUM_30
#define AUDIO_I2S_BCLK    GPIO_NUM_27
#define AUDIO_I2S_WS      GPIO_NUM_29
#define AUDIO_I2S_DOUT    GPIO_NUM_26
#define ES8388_ADDRESS    0x10u          /* 7-bit address on the internal I2C bus */

static const char *TAG = "tab5-audio";

/* esp_codec_dev reaches the ES8388 through M5Unified's internal I2C bus, so the
 * codec shares its bus lock with the touch panel, the IMU and the expanders. */
static int codec_ctrl_open(const audio_codec_ctrl_if_t *ctrl, void *cfg, int cfg_size)
{
    (void)ctrl;
    (void)cfg;
    (void)cfg_size;
    return ESP_CODEC_DEV_OK;
}

static bool codec_ctrl_is_open(const audio_codec_ctrl_if_t *ctrl)
{
    (void)ctrl;
    return true;
}

static int codec_ctrl_read(const audio_codec_ctrl_if_t *ctrl, int reg, int reg_len,
                           void *data, int data_len)
{
    (void)ctrl;
    if (reg_len != 1 || data_len <= 0) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    return tab5_internal_i2c_read(ES8388_ADDRESS, (uint8_t)reg, data, (size_t)data_len)
               ? ESP_CODEC_DEV_OK
               : ESP_CODEC_DEV_READ_FAIL;
}

static int codec_ctrl_write(const audio_codec_ctrl_if_t *ctrl, int reg, int reg_len,
                            void *data, int data_len)
{
    (void)ctrl;
    if (reg_len != 1 || data_len <= 0) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    return tab5_internal_i2c_write(ES8388_ADDRESS, (uint8_t)reg, data, (size_t)data_len)
               ? ESP_CODEC_DEV_OK
               : ESP_CODEC_DEV_WRITE_FAIL;
}

static int codec_ctrl_close(const audio_codec_ctrl_if_t *ctrl)
{
    (void)ctrl;
    return ESP_CODEC_DEV_OK;
}

static const audio_codec_ctrl_if_t codec_ctrl = {
    .open = codec_ctrl_open,
    .is_open = codec_ctrl_is_open,
    .read_reg = codec_ctrl_read,
    .write_reg = codec_ctrl_write,
    .close = codec_ctrl_close,
};

esp_codec_dev_handle_t tab5_audio_speaker_init(void)
{
    i2s_chan_handle_t tx = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; /* the DMA plays silence between two clicks */

    if (i2s_new_channel(&chan_cfg, &tx, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "Could not create the I2S channel");
        return NULL;
    }

    /* Same settings as the former BSP: 48 kHz, 16-bit Philips, MCLK at 256 fs.
     * esp_codec_dev switches the slots to stereo when the device is opened. */
    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_MCLK,
            .bclk = AUDIO_I2S_BCLK,
            .ws = AUDIO_I2S_WS,
            .dout = AUDIO_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    if (i2s_channel_init_std_mode(tx, &std_cfg) != ESP_OK ||
        i2s_channel_enable(tx) != ESP_OK) {
        ESP_LOGW(TAG, "Could not start the I2S channel");
        i2s_del_channel(tx);
        return NULL;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = AUDIO_I2S_PORT,
        .rx_handle = NULL,
        .tx_handle = tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    const audio_codec_if_t *codec_if = NULL;
    esp_codec_dev_handle_t speaker = NULL;

    if (data_if == NULL) {
        ESP_LOGW(TAG, "Could not create the codec data interface");
    } else {
        /* The amplifier enable is on the I/O expander, not on a GPIO: switch it
         * on before the codec starts, as the BSP did, and give esp_codec_dev no
         * pin to drive. */
        if (!tab5_speaker_amp_enable()) {
            ESP_LOGW(TAG, "Could not enable the speaker amplifier");
        }

        es8388_codec_cfg_t codec_cfg = {
            .ctrl_if = &codec_ctrl,
            .gpio_if = NULL,
            .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
            .master_mode = false,
            .pa_pin = -1,
            .pa_reverted = false,
            /* Same hardware gain as the BSP, so KEY_CLICK_VOLUME keeps its level. */
            .hw_gain = {
                .pa_voltage = 5.0f,
                .codec_dac_voltage = 3.3f,
            },
        };
        codec_if = es8388_codec_new(&codec_cfg);
        if (codec_if == NULL) {
            ESP_LOGW(TAG, "Could not set up the ES8388");
        } else {
            esp_codec_dev_cfg_t dev_cfg = {
                .dev_type = ESP_CODEC_DEV_TYPE_OUT,
                .codec_if = codec_if,
                .data_if = data_if,
            };
            speaker = esp_codec_dev_new(&dev_cfg);
            if (speaker == NULL) {
                ESP_LOGW(TAG, "Could not create the speaker device");
            }
        }
    }

    if (speaker != NULL) {
        return speaker;
    }
    if (codec_if != NULL) {
        audio_codec_delete_codec_if(codec_if);
    }
    if (data_if != NULL) {
        audio_codec_delete_data_if(data_if);
    }
    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    return NULL;
}
