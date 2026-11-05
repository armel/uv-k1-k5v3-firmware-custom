/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
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

#ifndef DRIVER_ST7565_H
#define DRIVER_ST7565_H

#include <stdbool.h>
#include <stdint.h>

#define LCD_WIDTH       128
#define LCD_HEIGHT       64
#define FRAME_LINES 7

extern uint8_t gStatusLine[LCD_WIDTH];
extern uint8_t gFrameBuffer[FRAME_LINES][LCD_WIDTH];

void ST7565_DrawLine(const unsigned int Column, const unsigned int Line, const uint8_t *pBitmap, const unsigned int Size);
void ST7565_BlitFullScreen(void);
void ST7565_BlitLine(unsigned line);
void ST7565_BlitStatusLine(void);
void ST7565_FillScreen(uint8_t Value);
void ST7565_Init(void);
#ifdef ENABLE_FEAT_F4HWN_SLEEP
    void ST7565_ShutDown(void);
#endif
void ST7565_FixInterfGlitch(void);
void ST7565_HardwareReset(void);
void ST7565_SelectColumnAndLine(uint8_t Column, uint8_t Line);
void ST7565_WriteByte(uint8_t Value);

#ifdef ENABLE_FEAT_F4HWN
    #ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
    void ST7565_BlitScreen(uint8_t line);    // 0 status line, 1-7 a frame line, 8 all frame lines
    bool ST7565_Feed(unsigned count);
    #endif
    #if defined(ENABLE_FEAT_F4HWN_CTR) || defined(ENABLE_FEAT_F4HWN_INV)
    void ST7565_ContrastAndInv(void);
    #endif
    //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
    void ST7565_Gauge(uint8_t line, uint8_t min, uint8_t max, uint8_t value);
    //#endif
    int16_t map(int16_t x, int16_t in_min, int16_t in_max, int16_t out_min, int16_t out_max);
#endif

#endif

