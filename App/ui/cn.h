/*
 * Chinese (GB2312) text rendering.
 */

#ifndef UI_CN_H
#define UI_CN_H

#include <stdint.h>

#ifdef ENABLE_FEAT_F4HWN_CN_FONT

// Pixel width of pStr (8 px per ASCII char, 12 px per GB2312 char).
uint8_t UI_PrintStringCNWidth(const char *pStr);

// Draws pStr at frame-buffer line Line (and Line+1), starting at column X.
// Returns the drawn width in pixels.
uint8_t UI_PrintStringCN(const char *pStr, uint8_t Line, uint8_t X);

// Pixel width of pStr in the small style (7 px per ASCII char, 12 px per
// GB2312 char).
uint8_t UI_PrintStringCNSmallWidth(const char *pStr);

// Small variant for the channel-name line: ASCII uses gFontSmall (one line,
// shifted 2 px down), Chinese glyphs are 12x12 and drawn at Line and the
// upper half of Line+1. Returns the drawn width in pixels.
uint8_t UI_PrintStringCNSmall(const char *pStr, uint8_t Line, uint8_t X);

// Tight single-line variant for the dual-VFO name+frequency row: Chinese
// glyphs are squashed to 12x8 and share one line with the small ASCII.
// Returns the drawn width in pixels.
uint8_t UI_PrintStringCNTight(const char *pStr, uint8_t Line, uint8_t X);

#endif // ENABLE_FEAT_F4HWN_CN_FONT

#endif // UI_CN_H
