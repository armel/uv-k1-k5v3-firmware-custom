// SPDX-License-Identifier: Apache-2.0
#ifndef UI_UI_PLUS_H
#define UI_UI_PLUS_H

#include <stdbool.h>
#include <stdint.h>

// Coordinates are relative to the 56-pixel main framebuffer, below the
// separate status bar. Render into each original 24-pixel block first; A may
// then be lowered into an unused middle line without moving B.
enum {
    UI_PLUS_LOCK_X = 8,
    UI_PLUS_RX_LABEL_X = 8,
    UI_PLUS_ACTIVITY_X = 20,
    UI_PLUS_NAME_X = 28,
    UI_PLUS_SCAN_NAME_X = 32,
    UI_PLUS_FREQUENCY_X = 36,
    UI_PLUS_FREQUENCY_Y = 11,
    UI_PLUS_STATUS_Y = 19,
    UI_PLUS_A_SHIFT = 3,
    UI_PLUS_SINGLE_LAYOUT = 2,
    UI_PLUS_SINGLE_ACTIVITY_Y = 35,
    UI_PLUS_SINGLE_STATUS_Y = 43,
    UI_PLUS_SINGLE_FOOTER_Y = 49,
};

void UI_PLUS_DrawName(const char *text, uint8_t x, uint8_t y, bool bold);
void UI_PLUS_DrawFrequency(const char *text, uint8_t x, uint8_t y);
// Single-line readouts inside the existing dual-channel block.
void UI_PLUS_DrawDualFrequency(const char *text, uint8_t line);
void UI_PLUS_DrawDualText(const char *text, uint8_t line);
void UI_PLUS_DrawChannelNumber(const char *text, uint8_t line);
void UI_PLUS_PrintStatus(const char *text, uint8_t x, uint8_t line, uint8_t layout);
void UI_PLUS_DrawPowerBars(unsigned int level, uint8_t line);
bool UI_PLUS_LowerChannelA(void);

void UI_PLUS_DrawSingleName(const char *text);
void UI_PLUS_DrawSingleFrequency(const char *text);
// Complete 3/4-digit MHz frequency, all five decimals, using the large digits.
void UI_PLUS_DrawLargeFrequency(const char *text);
// Centered CH + three-digit number; retains all four digits for 1000..1024.
void UI_PLUS_DrawLargeChannel(const char *text);
// Centered full channel name, up to ten characters, at the same 22px height.
void UI_PLUS_DrawLargeName(const char *text);
void UI_PLUS_DrawIcon(const uint8_t *glyph, uint8_t width, uint8_t x, uint8_t y);
void UI_PLUS_DrawSingleList(const char *text, uint8_t x);
void UI_PLUS_DrawSinglePowerBars(unsigned int level);
// Legacy meter/message renderers draw into page 5. Save that page first;
// this copies their output into y35..40 while restoring the status row.
void UI_PLUS_FinishSingleActivity(const uint8_t *savedPage, bool compress);

#endif
