/*
 * Tab5 speaker path for the K1Viewer key clicks.
 *
 * Copyright 2026 Armel F4HWN
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TAB5_AUDIO_H
#define TAB5_AUDIO_H

#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open the ES8388 speaker output as the former Tab5 BSP did: a 48 kHz I2S
 * channel driven by esp_codec_dev, written synchronously. Returns NULL when the
 * speaker cannot be used. Call it once, after tab5_hardware_init(). */
esp_codec_dev_handle_t tab5_audio_speaker_init(void);

#ifdef __cplusplus
}
#endif

#endif
