/* Copyright 2026 Armel F4HWN
 * https://github.com/armel
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

#include <string.h>

#include "driver/mb_flash.h"
#include "driver/py25q16.h"
#include "fw_assets.h"

/* Linker-provided bounds of the .fw_assets payload and of the GNU build-id
 * note (py32f071xb.ld). The note is a 12-byte header and the "GNU" name before
 * the 16-byte MD5 descriptor. */
extern const uint8_t __fw_assets_start[];
extern const uint8_t __fw_assets_end[];
extern const uint8_t __fw_build_id[];

#define FW_BUILD_ID_DESC_OFFSET 16u

static bool     gAssetsValid;
static uint32_t gAssetsDelta;   /* external-flash address minus link address */

const uint8_t *FW_ASSETS_BuildId(void)
{
    return __fw_build_id + FW_BUILD_ID_DESC_OFFSET;
}

uint32_t FW_ASSETS_PayloadSize(void)
{
    return (uint32_t)(__fw_assets_end - __fw_assets_start);
}

void FW_ASSETS_Init(uint8_t slot)
{
    gAssetsValid = false;

    if (MB_AssetValidate(slot, FW_ASSETS_BuildId(), FW_ASSETS_PayloadSize(), NULL) != MB_OK)
        return;

    gAssetsDelta = MB_ASSET_REGION_BASE + (uint32_t)slot * MB_ASSET_SLOT_SIZE +
                   MB_ASSET_HEADER_SIZE - (uint32_t)__fw_assets_start;
    gAssetsValid = true;
}

void FW_ASSETS_Invalidate(void)
{
    gAssetsValid = false;
}

bool FW_ASSETS_IsValid(void)
{
    return gAssetsValid;
}

void FW_ASSETS_Read(const void *src, void *dst, uint32_t len)
{
    if (!gAssetsValid)
    {
        memset(dst, 0, len);
        return;
    }

    /* The Safe variant waits for a pending erase/program (RX/TX log, settings)
     * to finish: a read issued while the chip is busy returns garbage. */
    PY25Q16_ReadBufferSafe((uint32_t)src + gAssetsDelta, dst, len);
}

void FW_ASSETS_ReadStr(char *dst, const char *src, uint32_t size)
{
    FW_ASSETS_Read(src, dst, size);
    dst[size - 1u] = '\0';
}
