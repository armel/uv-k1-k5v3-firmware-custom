/* Copyright 2026 F4HWN
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
 * Multiboot flash programmer.
 *
 * M1 (validated): brick-critical core - reprogram the internal application flash
 * from an image held in the external SPI flash, running from RAM.
 *
 * M2 (this file): slot format + integrity validation. Each slot starts with a
 * header (magic, size, CRC32, name, version); a restore validates the header and
 * the image CRC32 *before* erasing anything, so an incompatible or corrupt image
 * can never brick the radio. There is a single firmware for both the K1 and the
 * K5v3 (the keypad difference is handled at runtime by the hidden SetNav menu),
 * so no per-model guard is needed.
 *
 * See docs/multiboot-design.md for the overall design.
 */

#ifndef DRIVER_MB_FLASH_H
#define DRIVER_MB_FLASH_H

#include <stdint.h>
#include <stdbool.h>

/* Internal flash application region (see Core/py32f071xb.ld:
 * FLASH origin 0x08002800, length 118 KiB). 0x08002800 is 256-byte aligned, so
 * the whole region can be page-erased (256 B granularity) without touching the
 * factory bootloader that lives just below it. */
#define MB_INT_APP_BASE     0x08002800u
#define MB_INT_APP_SIZE     0x0001D800u   /* 118 KiB */

/* External SPI flash slot layout (see docs/multiboot-design.md).
 * Each 128 KiB slot = one header sector (4 KiB) followed by the image. */
#define MB_SLOT_STRIDE      0x00020000u   /* 128 KiB per slot            */
#define MB_SLOT_IMG_OFFSET  0x00001000u   /* image starts after header sector */
#define MB_SLOT0_EXT_BASE   0x00020000u   /* slot 0 header base           */
#define MB_SLOT_COUNT       4u

/* Slot header (stored at the slot base, first 4 KiB sector). 64 bytes. */
#define MB_SLOT_MAGIC       0x31424D46u   /* "FMB1" */
#define MB_HDR_VERSION      1u
#define MB_FLAG_COMMITTED   (1u << 0)     /* image written and verified */
#define MB_NAME_LEN         16
#define MB_VERSION_LEN      16

typedef struct __attribute__((packed)) {
    uint32_t magic;                    /* MB_SLOT_MAGIC                    */
    uint16_t hdr_version;              /* MB_HDR_VERSION                   */
    uint16_t flags;                    /* MB_FLAG_COMMITTED, ...           */
    uint32_t image_size;              /* bytes, <= MB_INT_APP_SIZE        */
    uint32_t image_crc32;             /* CRC-32 (zlib) over image_size B  */
    char     name[MB_NAME_LEN];       /* human-readable, NUL-terminated   */
    char     fw_version[MB_VERSION_LEN]; /* firmware version string        */
    uint8_t  reserved[16];            /* pad to 64 bytes, future use      */
} mb_slot_header_t;

/* Restore validation result (MB_OK never returns - the radio resets). */
enum {
    MB_OK = 0,
    MB_ERR_MAGIC,        /* no/invalid slot header            */
    MB_ERR_VERSION,      /* header format too new             */
    MB_ERR_NOT_COMMITTED,/* image not marked complete         */
    MB_ERR_SIZE,         /* image_size out of range           */
    MB_ERR_CRC,          /* image CRC32 mismatch              */
    MB_ERR_SPI,          /* external flash read/write timed out*/
    MB_ERR_SLOT,         /* slot index out of range           */
    MB_ERR_AUTH          /* write refused: timestamp mismatch */
};

/*
 * Copy the live internal application image into external flash slot 0, writing
 * the image first and then a valid header (COMMITTED) last. Runs entirely from
 * flash and only writes the external SPI flash, so it is safe (no brick risk).
 * Reports the stored image size and CRC-32 through the (optional) out params.
 */
void MB_BackupToSlot0(uint32_t *out_size, uint32_t *out_crc32);

/*
 * Validate slot 0 and, if valid, reflash the internal application from it and
 * reset. Validation (magic, version, COMMITTED flag, size, image CRC-32) runs
 * from flash *before* any erase: on failure it returns an MB_ERR_* code and the
 * internal flash is left untouched. On success it NEVER RETURNS (the RAM-resident
 * copier reflashes the internal application and triggers a system reset). The
 * factory bootloader is never touched, so an interrupted copy is recoverable
 * over USB/DFU.
 */
uint8_t MB_RestoreSlot0(void);

/* Multi-slot API used by the boot selector. Validation always covers the full
 * image CRC before restore. progress_line may point to a 128-byte LCD page; the
 * RAM copier then fills it while reflashing. Pass NULL to disable LCD updates. */
uint8_t MB_ValidateSlot(uint8_t slot, mb_slot_header_t *out_header, uint32_t *out_crc);
uint8_t MB_RestoreSlot(uint8_t slot, uint8_t *progress_line);

/*
 * Host-tool slot management (M4, "Firmware Slots" in UV Studio). Everything is
 * bounds-checked (slot < MB_SLOT_COUNT, offset+len <= MB_SLOT_STRIDE) and writes
 * touch the EXTERNAL flash only, so none of this is brick-critical: a bad slot is
 * simply refused at restore by the CRC validation above.
 *
 *  - MB_SlotInfo   reads the 64-byte header only (fast, no CRC recompute) and
 *                  returns MB_OK / MB_ERR_* describing the header state.
 *  - MB_SlotErase  erases the whole 128 KiB slot region (header + image).
 *  - MB_SlotWrite  programs `len` bytes at slot_base+offset. The slot MUST have
 *                  been erased first (NOR flash only clears 1->0 bits); the host
 *                  writes the image, then the COMMITTED header last.
 * Use MB_ValidateSlot afterwards to confirm the full image CRC.
 */
uint8_t MB_SlotInfo(uint8_t slot, mb_slot_header_t *out_header);
uint8_t MB_SlotErase(uint8_t slot);
uint8_t MB_SlotWrite(uint8_t slot, uint32_t offset, const uint8_t *data, uint32_t len);


/*
 * Test helper: overwrite up to `len` (capped) bytes in the MIDDLE of slot 0's
 * image with the supplied data, so the next MB_RestoreSlot0() fails its CRC-32
 * check and refuses. It writes ONLY inside slot 0's image body - never the
 * header, and never anything outside the slot (calibration, EEPROM, RF log...).
 * Developer aid to exercise the safe-refusal path without external tooling.
 */
void MB_CorruptSlot0(const uint8_t *data, uint32_t len);

/*
 * Validate slot 0 (header + image CRC-32) WITHOUT reflashing. Same checks as the
 * restore path, entirely via polled reads (cannot hang). Returns MB_OK or an
 * MB_ERR_* code, and reports the computed image CRC-32 through out_crc.
 * Useful as a safe diagnostic from the host tool.
 */
uint8_t MB_ValidateSlot0(uint32_t *out_crc);

/*
 * Read `len` bytes of external flash at `addr` into `buf`, via the DMA driver
 * (PY25Q16_ReadBuffer) - the same proven read path the backup uses internally.
 * Ground-truth diagnostic to check what is actually stored in a slot.
 */
void MB_DumpExt(uint32_t addr, uint8_t *buf, uint32_t len);

/*
 * Diagnostic: snapshot the SPI2 / DMA hardware state WITHOUT any flash access
 * (so it cannot hang). out[0]=SPI2->CR1, [1]=SPI2->SR, [2]=DMA RD chan CCR,
 * [3]=DMA WR chan CCR. Reveals whether SPI2 is disabled/busy or a DMA channel
 * is left armed when a read command runs.
 */
void MB_SpiState(uint32_t out[4]);

/*
 * On-screen trace marker (debug). Draws `s` on the LCD (SPI1, independent of the
 * flash SPI2) so that when a flash read freezes the CPU, the frozen screen shows
 * the last step reached. Only draws while mb_mark_on is set (i.e. during a Dump),
 * so it does not flash the screen during normal writes.
 */
extern volatile uint8_t mb_mark_on;
void MB_Mark(const char *s);

#endif /* DRIVER_MB_FLASH_H */
