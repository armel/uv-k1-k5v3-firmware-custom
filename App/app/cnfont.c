#include "cnfont.h"

#include <stddef.h>

#include "driver/py25q16.h"

#ifdef ENABLE_FEAT_F4HWN_CN_FONT

bool CN_FONT_EraseSector(uint16_t SectorIndex)
{
    if (SectorIndex >= CN_FONT_SECTOR_COUNT)
    {
        return false;
    }

    PY25Q16_SectorErase(CN_FONT_FLASH_BASE + (uint32_t)SectorIndex * 0x1000u);
    return true;
}

bool CN_FONT_Write(uint32_t Offset, const uint8_t *pData, uint16_t Len)
{
    if (pData == NULL || Len == 0 || Offset >= CN_FONT_FLASH_SIZE ||
        Len > CN_FONT_FLASH_SIZE - Offset)
    {
        return false;
    }

    PY25Q16_WriteBuffer(CN_FONT_FLASH_BASE + Offset, pData, Len, false);
    return true;
}

bool CN_FONT_GetGlyph(uint8_t Zone, uint8_t Pos, uint8_t *pOut)
{
    if (pOut == NULL || Zone < 0xA1u || Zone > 0xFEu || Pos < 0xA1u || Pos > 0xFEu)
    {
        return false;
    }

    const uint32_t Offset = ((uint32_t)(Zone - 0xA1u) * 94u + (uint32_t)(Pos - 0xA1u))
                            * CN_FONT_GLYPH_SIZE;
    PY25Q16_ReadBuffer(CN_FONT_FLASH_BASE + Offset, pOut, CN_FONT_GLYPH_SIZE);
    return true;
}

bool CN_FONT_Present(void)
{
    uint8_t Glyph[CN_FONT_GLYPH_SIZE];
    CN_FONT_GetGlyph(0xA1u, 0xA1u, Glyph);   // 啊: first glyph of the font
    for (uint8_t i = 0; i < CN_FONT_GLYPH_SIZE; i++)
    {
        if (Glyph[i] != 0xFFu)
        {
            return true;
        }
    }
    return false;   // whole glyph erased -> no font programmed
}

#endif // ENABLE_FEAT_F4HWN_CN_FONT
