/*
 * Chinese font (GB2312) storage in the external SPI flash.
 *
 * The font binary is written by the k5web tool over the UART/USB command
 * channel (App/app/uart.c commands 0x05E6/0x05E7). Rendering is done by
 * the UI layer reading glyph bitmaps straight from SPI flash.
 *
 * Glyph format (matches the ST7565 page layout, same convention as
 * gFontBig): 12x12 pixels = 24 bytes = two half-pages of 12 columns.
 * First 12 bytes: rows 0..7 (bit n = row n, LSB = top), next 12 bytes:
 * rows 8..11 in the low nibble. Glyph index = (zone - 0xA1) * 94 + (pos - 0xA1).
 */

#ifndef APP_CNFONT_H
#define APP_CNFONT_H

#include <stdint.h>
#include <stdbool.h>

// External SPI flash layout: the font lives in the multiboot-compatible
// shared font area at 0xA0000 ("标准GB2312大字体", 0xA0000..0xE0000) so the
// same flashed font serves this firmware, the 白头佬 multiboot systems AND
// single-system radios (where the region is free space). Format: 16x16
// glyphs, 32 bytes each (column bytes, upper half rows 0..7, lower half
// rows 8..15, LSB = top), linear GB2312 index truncated to 8192 entries --
// that covers every defined character (last one, 0xF7FE, is index 8177).
// The 12x12 / 8x8 rendering is produced at draw time by downsampling.
#define CN_FONT_FLASH_BASE      0x0A0000u
#define CN_FONT_GLYPH_SIZE      32u                       // 16x16, 2 bytes per column
#define CN_FONT_GLYPH_COUNT     8192u                     // truncated shared font
#define CN_FONT_FLASH_SIZE      (CN_FONT_GLYPH_COUNT * CN_FONT_GLYPH_SIZE)   // 262,144 B
#define CN_FONT_SECTOR_COUNT    ((CN_FONT_FLASH_SIZE + 0xFFFu) / 0x1000u)    // 64 sectors

// Erase one 4 KiB sector of the font area. Returns false when out of range.
bool CN_FONT_EraseSector(uint16_t SectorIndex);

// Write Len bytes at Offset within the font area. Returns false when out of range.
bool CN_FONT_Write(uint32_t Offset, const uint8_t *pData, uint16_t Len);

// Read Len (<= 128) bytes at Offset within the font area into pData.
// Returns false when out of range.
bool CN_FONT_Read(uint32_t Offset, uint8_t *pData, uint16_t Len);

// Reads the 32-byte glyph of the GB2312 character (Zone/Pos in 0xA1..0xFE)
// into pOut. Returns false when the character is beyond the truncated
// 8192-glyph shared font or the arguments are out of range (pOut must hold
// at least 32 bytes).
bool CN_FONT_GetGlyph(uint8_t Zone, uint8_t Pos, uint8_t *pOut);

// True when a font has actually been programmed: an erased area reads all
// 0xFF, which no real glyph is made of, so the first glyph is a cheap probe.
bool CN_FONT_Present(void);

#endif // APP_CNFONT_H
