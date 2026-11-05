// SPDX-License-Identifier: Apache-2.0
#ifndef UI_GUI_STYLE_H
#define UI_GUI_STYLE_H

#include <stdint.h>

#ifdef ENABLE_FEAT_F4HWN_UI_PLUS
#define UI_PLUS_ENABLED 1
#else
#define UI_PLUS_ENABLED 0
#endif

enum {
    GUI_STYLE_TINY = 0,
    GUI_STYLE_CLASSIC,
    GUI_STYLE_PLUS,
    GUI_STYLE_COUNT = 2 + UI_PLUS_ENABLED
};

// Display byte at 0x00A15D: contrast 0..3, inversion 4, extension 5,
// meter 6, legacy CLASSIC flag 7. Earlier firmware saves bit 5 as zero.
// Keep bit 7 set for UI+ so older firmware interprets it as CLASSIC.
static inline uint8_t GUI_STYLE_Decode(uint8_t displayByte)
{
    if (!(displayByte & 0x80u))
        return GUI_STYLE_TINY;
    // Every bit pattern must round-trip: 0xFF is also a valid UI+ setting
    // with contrast 15, inversion and CLASSIC meter enabled.
    return UI_PLUS_ENABLED && (displayByte & 0x20u) ? GUI_STYLE_PLUS : GUI_STYLE_CLASSIC;
}

static inline uint8_t GUI_STYLE_Encode(uint8_t style)
{
    return UI_PLUS_ENABLED && style == GUI_STYLE_PLUS ? 0xA0u :
           style == GUI_STYLE_CLASSIC ? 0x80u : 0;
}

#endif
