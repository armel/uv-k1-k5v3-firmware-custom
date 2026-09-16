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

#ifndef FONT_H
#define FONT_H

#include <stdint.h>


void FONT_DrawBigGlyph(uint8_t glyph, uint8_t *top, uint8_t *bottom);
extern const uint8_t gFontBigDigits[11][26 - 6];
extern const uint8_t gFont3x5[96][3];

#define FONT_SMALL_WIDTH       6u
#define FONT_SMALL_GLYPH_COUNT (95u - 1u)
#define FONT_SMALL_PACKED_SIZE ((FONT_SMALL_GLYPH_COUNT * FONT_SMALL_WIDTH * 7u + 7u) / 8u)

extern const uint8_t gFontSmallPacked[FONT_SMALL_PACKED_SIZE];
#ifdef ENABLE_SMALL_BOLD
    extern const uint8_t gFontSmallBoldPacked[FONT_SMALL_PACKED_SIZE];
#endif

#endif
