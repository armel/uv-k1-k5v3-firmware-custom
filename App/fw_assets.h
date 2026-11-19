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

/*
 * Firmware assets: read-only data (fonts, menu texts, QR codes) kept out of
 * the internal flash.
 *
 * A table tagged FW_ASSET is linked into the .fw_assets section, at a virtual
 * address that does not exist on the MCU (FW_ASSETS region of the linker
 * script). The build extracts that section into an asset pack appended to the
 * release .bin; UV Studio writes the pack into the asset zone of the firmware
 * slot (external flash, see mb_flash.h). At run time FW_ASSETS_Read() turns a
 * pointer into such a table into an external-flash address and reads it.
 *
 * Asset tables must NEVER be dereferenced directly: only pointer arithmetic
 * (indexing, decay to a pointer) and sizeof are allowed, every read goes
 * through FW_ASSETS_Read() / FW_ASSETS_ReadStr(). When the feature is
 * disabled, FW_ASSET is empty and the readers become plain memory copies.
 */

#ifndef FW_ASSETS_H
#define FW_ASSETS_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef ENABLE_FEAT_F4HWN_FW_ASSETS

#define FW_ASSET __attribute__((section(".fw_assets")))

/* Asset pack header, at the start of the asset zone of a firmware slot. The
 * payload (the .fw_assets section, byte for byte) follows it. */
#define FW_ASSETS_MAGIC       0x31415746u   /* "FWA1" */
#define FW_ASSETS_VERSION     1u
#define FW_ASSETS_BUILD_ID_LEN 16u

typedef struct {
    uint32_t magic;                             /* FW_ASSETS_MAGIC          */
    uint16_t version;                           /* FW_ASSETS_VERSION        */
    uint16_t header_size;                       /* sizeof(fw_assets_header_t) */
    uint32_t payload_size;                      /* bytes after the header   */
    uint32_t payload_crc32;                     /* CRC-32 (zlib) of payload */
    uint8_t  build_id[FW_ASSETS_BUILD_ID_LEN];  /* GNU build-id (MD5) of the firmware */
} fw_assets_header_t;

/* Validate the asset zone of the running firmware slot and enable reads.
 * Until it succeeds every read returns zeros (blank glyphs, empty strings). */
void FW_ASSETS_Init(uint8_t slot);
/* Drop the assets (the zone of the running slot is being rewritten). */
void FW_ASSETS_Invalidate(void);
bool FW_ASSETS_IsValid(void);

/* Build identity and payload size of this firmware, as stored in its pack. */
const uint8_t *FW_ASSETS_BuildId(void);
uint32_t FW_ASSETS_PayloadSize(void);

void FW_ASSETS_Read(const void *src, void *dst, uint32_t len);
/* Copy a fixed-width string table entry of `size` bytes; the result is always
 * terminated, even if an entry was cut by its table width. */
void FW_ASSETS_ReadStr(char *dst, const char *src, uint32_t size);

#else

#define FW_ASSET

static inline void FW_ASSETS_Read(const void *src, void *dst, uint32_t len)
{
    memcpy(dst, src, len);
}

static inline void FW_ASSETS_ReadStr(char *dst, const char *src, uint32_t size)
{
    memcpy(dst, src, size);
    dst[size - 1u] = '\0';
}

#endif

/* Copy entry `index` of a fixed-width string table: dst needs sizeof(table[0]). */
#define FW_ASSETS_STRCPY(dst, table, index) \
    FW_ASSETS_ReadStr((dst), (table)[index], sizeof((table)[0]))

/* Copy a complete asset object into resident memory; src must not be a pointer. */
#define FW_ASSETS_COPY(dst, src) \
    FW_ASSETS_Read((src), (dst), sizeof(src))

#endif
