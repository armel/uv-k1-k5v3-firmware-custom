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
 * Overlay-app loader (POC).
 *
 * A leaf, modal app is a self-contained blob held in the external SPI flash. To
 * run it the loader validates the header + code CRC, copies the code into the
 * 4 KiB overlay (the PY25Q16 sector cache, shared VMA with the multiboot RAM
 * stub), and calls its entry point. The blob runs from RAM and reaches every
 * resident service through app_api_t. The loader never touches the internal
 * application flash, so a corrupt or incompatible blob is only refused (header/CRC
 * mismatch) or crashes into a watchdog reset.
 *
 * This is NOT a security sandbox. The CRC is an integrity check, not authentication,
 * and a launched app is TRUSTED NATIVE CODE: it runs privileged with full access to
 * memory and peripherals (no MPU, no crypto auth), so a malicious or buggy app CAN
 * brick the radio - e.g. by driving the internal-flash controller itself. Only
 * install apps you trust.
 *
 * External-flash "Apps" region (carved from the free space in the PY25Q16 map,
 * after the multiboot markers, before the RX/TX log):
 *
 *   0x102000  slot 0   ]  16 slots x 8 KiB = 128 KiB
 *   0x104000  slot 1   ]  each slot: 4 KiB header sector + 4 KiB code
 *     ...              ]
 *   0x120000  slot 15  ]
 *
 * Host tooling (APP_SlotErase/Write/Info) touches the external flash only and is
 * never brick-critical; a bad slot is simply refused at launch by the CRC check.
 */

#ifndef APPS_APP_OVERLAY_H
#define APPS_APP_OVERLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "app_api.h"

/* ---- external-flash Apps region ---- */
#define APP_REGION_BASE   0x00102000u   /* first app slot                        */
#define APP_SLOT_STRIDE   0x00002000u   /* 8 KiB per slot                        */
#define APP_CODE_OFFSET   0x00001000u   /* code starts after the 4 KiB header sector */
#define APP_SECTOR_SIZE   0x00001000u   /* external NOR erase granularity        */
#define APP_SLOT_COUNT    16u
#define APP_SLOT_BASE(s)  (APP_REGION_BASE + (uint32_t)(s) * APP_SLOT_STRIDE)

/* ---- overlay RAM budget: the PY25Q16 4 KiB sector cache reused as workspace.
 * The link VMA itself is pinned in Core/py32f071xb.ld (ORIGIN+0x280) and passed
 * via compile-app.sh; the loader checks each blob's link_vma against it. ---- */
#define APP_OVERLAY_MAX   0x00001000u   /* 4 KiB                                 */

/* ---- blob header (64 bytes, little-endian; see App/apps/pack_app.py) ---- */
#define APP_MAGIC         0x31504146u   /* "FAP1"                                */
#define APP_HDR_VERSION   1u
#define APP_FLAG_COMMITTED (1u << 0)
#define APP_NAME_LEN      16
#define APP_VERSION_LEN   16

typedef struct __attribute__((packed)) {
    uint32_t magic;                    /* APP_MAGIC                              */
    uint16_t hdr_version;              /* APP_HDR_VERSION                        */
    uint16_t abi_version;             /* ABI the app was built against          */
    uint32_t code_size;               /* bytes of code, <= APP_OVERLAY_MAX      */
    uint32_t code_crc32;              /* CRC-32 (zlib) over code_size bytes     */
    uint16_t entry_off;               /* entry offset within the code (0)       */
    uint16_t flags;                   /* APP_FLAG_COMMITTED, ...                */
    char     name[APP_NAME_LEN];      /* human-readable, NUL-terminated         */
    char     version[APP_VERSION_LEN];/* app version string                     */
    uint32_t link_vma;                /* RAM VMA the code was linked at         */
    uint8_t  reserved[8];             /* pad to 64 bytes                        */
} app_header_t;

enum {
    APP_OK = 0,
    APP_ERR_SLOT,           /* slot index out of range                */
    APP_ERR_MAGIC,          /* no/invalid header                      */
    APP_ERR_ABI,            /* ABI version mismatch                   */
    APP_ERR_NOT_COMMITTED,  /* image not marked complete              */
    APP_ERR_SIZE,           /* code_size out of range                 */
    APP_ERR_CRC,            /* code CRC-32 mismatch                   */
    APP_ERR_VMA,            /* overlay buffer not at the link VMA     */
    APP_ERR_AUTH,           /* host write refused: timestamp mismatch */
};

/* Read + validate a slot header (no CRC of the code). */
uint8_t APP_ValidateSlot(uint8_t slot, app_header_t *out_header);

/* Validate, load into the overlay, verify the code CRC, and run the app.
 * Returns when the app exits; the internal flash is never touched. */
uint8_t APP_LaunchOverlay(uint8_t slot);

/* Find the first valid app with this exact header name and launch it. */
uint8_t APP_LaunchOverlayByName(const char *name);

/* Host-tool slot management (external flash only, never brick-critical). */
uint8_t APP_SlotInfo(uint8_t slot, app_header_t *out_header);
uint8_t APP_SlotErase(uint8_t slot);
uint8_t APP_SlotWrite(uint8_t slot, uint32_t offset, const uint8_t *data, uint32_t len);


#endif /* APPS_APP_OVERLAY_H */
