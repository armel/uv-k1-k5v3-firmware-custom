/*
 * Chinese (GB2312) text rendering.
 */

#ifndef UI_CN_H
#define UI_CN_H

#include <stdint.h>

#ifdef ENABLE_FEAT_F4HWN_CN_FONT

// Pixel width of pStr (8 px per ASCII char, 16 px per GB2312 char).
uint8_t UI_PrintStringCNWidth(const char *pStr);

// Draws pStr at frame-buffer line Line (and Line+1), starting at column X.
// Returns the drawn width in pixels.
uint8_t UI_PrintStringCN(const char *pStr, uint8_t Line, uint8_t X);

#endif // ENABLE_FEAT_F4HWN_CN_FONT

#endif // UI_CN_H
