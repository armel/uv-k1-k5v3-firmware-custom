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

#include <stdbool.h>
#include <stdint.h>


void FONT_DrawBigGlyph(uint8_t glyph, uint8_t *top, uint8_t *bottom);
#define FONT_BIG_WIDTH          7u
#define FONT_BIG_DIGITS_DEFAULT 0u

#ifdef ENABLE_FEAT_F4HWN_SET_FNT
    // The compact digit sets (10 columns) come first and share one table; the
    // original 13-column set is last.
    typedef enum {
        FONT_BIG_DIGITS_TERMINUS = FONT_BIG_DIGITS_DEFAULT,
        FONT_BIG_DIGITS_ALT,
        FONT_BIG_DIGITS_SEVEN_SEGMENT,
        FONT_BIG_DIGITS_ORIGINAL,
        FONT_BIG_DIGITS_COUNT
    } FontBigDigits_t;

    #define FONT_BIG_DIGITS_COMPACT_COUNT FONT_BIG_DIGITS_ORIGINAL

    // One SetFnt value selects both families: digit = value % FONT_BIG_DIGITS_COUNT,
    // text = value / FONT_BIG_DIGITS_COUNT.
    typedef enum {
        FONT_TEXT_NORMAL,
        FONT_TEXT_SMALL_CAPS,
        FONT_TEXT_BLOCK,
        FONT_TEXT_VARIANT_COUNT
    } FontTextVariant_t;

    #define FONT_VARIANT_COUNT       (FONT_BIG_DIGITS_COUNT * FONT_TEXT_VARIANT_COUNT)
    #define FONT_VARIANT_DEFAULT     FONT_BIG_DIGITS_DEFAULT

    void FONT_DrawBigGlyphWithVariant(uint8_t variant, uint8_t glyph,
                                      uint8_t *top, uint8_t *bottom);
#endif

void FONT_DrawBigDigit(uint8_t font, uint8_t glyph, uint8_t *top, uint8_t *bottom);
extern const uint8_t gFont3x5[96][3];

#define FONT_SMALL_WIDTH       6u
#define FONT_SMALL_GLYPH_COUNT (95u - 1u)

// The small fonts handed to UI_PrintStringBuffer(): uncompressed asset tables
// (FONT_SMALL_WIDTH bytes per glyph), or 7-bit packed tables in internal flash.
#ifdef ENABLE_FEAT_F4HWN_FW_ASSETS
    extern const uint8_t gFontSmall[FONT_SMALL_GLYPH_COUNT][FONT_SMALL_WIDTH];
    #define FONT_SMALL_NORMAL ((const uint8_t *)gFontSmall)
    #ifdef ENABLE_SMALL_BOLD
        extern const uint8_t gFontSmallBold[FONT_SMALL_GLYPH_COUNT][FONT_SMALL_WIDTH];
        #define FONT_SMALL_BOLD ((const uint8_t *)gFontSmallBold)
    #endif
    #ifdef ENABLE_FEAT_F4HWN_SET_FNT
        extern const uint8_t gFontSmallSmallCaps[FONT_SMALL_GLYPH_COUNT][FONT_SMALL_WIDTH];
        extern const uint8_t gFontSmallBlock[FONT_SMALL_GLYPH_COUNT][FONT_SMALL_WIDTH];
        #ifdef ENABLE_SMALL_BOLD
            extern const uint8_t gFontSmallBoldSmallCaps[FONT_SMALL_GLYPH_COUNT][FONT_SMALL_WIDTH];
        #endif
        const uint8_t *FONT_GetSmallFont(uint8_t variant, bool bold);
    #else
        #define FONT_GetSmallFont(variant, bold) ((void)(variant), (bold) ? FONT_SMALL_BOLD : FONT_SMALL_NORMAL)
    #endif
#else
    #define FONT_SMALL_PACKED_SIZE ((FONT_SMALL_GLYPH_COUNT * FONT_SMALL_WIDTH * 7u + 7u) / 8u)

    extern const uint8_t gFontSmallPacked[FONT_SMALL_PACKED_SIZE];
    #define FONT_SMALL_NORMAL gFontSmallPacked
    #ifdef ENABLE_SMALL_BOLD
        extern const uint8_t gFontSmallBoldPacked[FONT_SMALL_PACKED_SIZE];
        #define FONT_SMALL_BOLD gFontSmallBoldPacked
    #endif
    // Without the firmware assets there is a single text family.
    #define FONT_GetSmallFont(variant, bold) ((void)(variant), (bold) ? FONT_SMALL_BOLD : FONT_SMALL_NORMAL)
#endif
#ifndef FONT_SMALL_BOLD
    #define FONT_SMALL_BOLD FONT_SMALL_NORMAL
#endif

#endif
