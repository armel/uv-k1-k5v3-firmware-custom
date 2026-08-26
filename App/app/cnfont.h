/*
 * Chinese font (GB2312) storage in the external SPI flash.
 *
 * The font binary is written by the k5web tool over the UART/USB command
 * channel (App/app/uart.c commands 0x05E6/0x05E7). Rendering is done by
 * the UI layer reading glyph bitmaps straight from SPI flash.
 *
 * Glyph format (matches the ST7565 page layout, same convention as
 * gFontBig): 16x16 pixels = 32 bytes. First 16 bytes are the upper half,
 * next 16 the lower half; each byte is one column, bit n = pixel row n of
 * that half (LSB = top). Glyph index = (zone - 0xA1) * 94 + (pos - 0xA1).
 */

#ifndef APP_CNFONT_H
#define APP_CNFONT_H

#include <stdint.h>
#include <stdbool.h>

// External SPI flash layout: font sits in the free gap between the boot
// logo area (~0x012000) and the voice prompts (0x14C000).
#define CN_FONT_FLASH_BASE      0x020000u
#define CN_FONT_GLYPH_SIZE      32u                       // 16x16, 2 bytes per column
#define CN_FONT_GLYPH_COUNT     (94u * 94u)               // GB2312 zones A1..FE
#define CN_FONT_FLASH_SIZE      (CN_FONT_GLYPH_COUNT * CN_FONT_GLYPH_SIZE)   // 282,752 B
#define CN_FONT_SECTOR_COUNT    ((CN_FONT_FLASH_SIZE + 0xFFFu) / 0x1000u)    // 70 sectors

// Erase one 4 KiB sector of the font area. Returns false when out of range.
bool CN_FONT_EraseSector(uint16_t SectorIndex);

// Write Len bytes at Offset within the font area. Returns false when out of range.
bool CN_FONT_Write(uint32_t Offset, const uint8_t *pData, uint16_t Len);

// Reads the 32-byte glyph (upper half 16 bytes + lower half 16 bytes) of the
// GB2312 character (Zone/Pos in 0xA1..0xFE) into pOut. Returns false when the
// arguments are out of range (pOut must hold at least 32 bytes).
bool CN_FONT_GetGlyph(uint8_t Zone, uint8_t Pos, uint8_t *pOut);

// True when a font has actually been programmed: an erased area reads all
// 0xFF, which no real glyph is made of, so the first glyph is a cheap probe.
bool CN_FONT_Present(void);

#endif // APP_CNFONT_H
