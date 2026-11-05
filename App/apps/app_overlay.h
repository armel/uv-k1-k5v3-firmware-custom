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
 * after the multiboot markers and before the Sonic 4K reserve):
 *
 *   0x102000  slot 0   ]  48 slots x 8 KiB = 384 KiB
 *   0x104000  slot 1   ]  each slot: 4 KiB header sector + 4 KiB code
 *     ...              ]
 *   0x160000  slot 47  ]
 *
 * Header sector layout (slot-relative):
 *
 *   0x000  app_header_t (64 B)
 *   0x040  per-app config staged by cfg_save (16 B)
 *   0x050  name of the app that config belongs to (16 B, reserved up to 0x0FF);
 *          kept with the config when the slot is erased for an update
 *   0x100  packaged assets (API level 2, up to 3840 B, CRC-checked at launch);
 *          normally read-only, except for a validated config-proxy target block
 *          and for the screen saved at exit (APP_FLAG_SAVE_SCREEN)
 *   0x1000 code (<= 4 KiB, copied into the overlay RAM)
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
#define APP_SLOT_COUNT    48u
#define APP_SLOT_BASE(s)  (APP_REGION_BASE + (uint32_t)(s) * APP_SLOT_STRIDE)

/* ---- overlay RAM budget: the PY25Q16 4 KiB sector cache reused as workspace.
 * The link VMA itself is pinned in Core/py32f071xb.ld (ORIGIN+0x280) and passed
 * via compile-app.sh; the loader checks each blob's link_vma against it. ---- */
#define APP_OVERLAY_MAX   0x00001000u   /* 4 KiB                                 */

/* ---- packaged assets, served read-only to apps by asset_read (API level 2) ----
 * Literal values so pack_app.py can parse them; checked against the slot
 * geometry by static asserts in app_overlay.c. A config-proxy editor may stage
 * one bounded target block; only the resident loader writes it after exit. */
#define APP_ASSET_OFFSET  0x00000100u   /* assets start in the header sector     */
#define APP_ASSET_MAX     0x00000F00u   /* up to the end of the header sector    */

/* ---- blob header (64 bytes, little-endian; see App/apps/pack_app.py) ---- */
#define APP_MAGIC         0x31504146u   /* "FAP1"                                */
#define APP_HDR_VERSION   1u
#define APP_FLAG_COMMITTED   0x0001u
#define APP_FLAG_SCREEN_SAVER 0x0002u
#define APP_FLAG_EXIT_TO_MAIN 0x0004u   /* leave the Apps menu when the app returns */
/* Keep the screen the app leaves behind: when it returns, the loader copies the
 * display buffers (status line, then the 7 frame lines: one 128x64 screen)
 * over the last APP_SCREEN_SAVE_LEN bytes of its assets and updates asset_crc.
 * The app reserves that block at the end of its assets, reads it back with
 * asset_read at the next launch and fills the buffers before it returns (no
 * blit needed). Firmware older than this flag ignores it: the block then keeps
 * its packaged content. An app update erases the slot, the saved screen too. */
#define APP_FLAG_SAVE_SCREEN 0x0008u
#define APP_SCREEN_SAVE_LEN  1024u
#define APP_FLAG_SHORTCUT_SHIFT 8u
#define APP_FLAG_SHORTCUT_MASK  0x0F00u
#define APP_VERSION_LEN   16

/* Shortcut IDs: the value of the header's shortcut field, one per app (the
 * first four predate the IDs, hence their single-bit values).  A firmware that
 * does not know an ID ignores the app's shortcut. */
#define APP_SHORTCUT_FM       0x01u
#define APP_SHORTCUT_FOXHUNT  0x02u
#define APP_SHORTCUT_BEACON   0x04u
#define APP_SHORTCUT_BEAM     0x08u
#define APP_SHORTCUT_TONECALL 0x03u

/* Optional resident facilities an app may require.  Requirements live in the
 * previously reserved header bytes, so app_header_t remains 64 bytes. */
#define APP_CAP_FM            0x00000001u
/* Retired: the v6.0.0 BEAM bridge (resident FSK send/receive).  No longer
 * advertised, so a v6.0.0 Beam.app is refused with APP_ERR_CAP. */
#define APP_CAP_BEAM          0x00000004u
#define APP_CAP_SYSINFO       0x00000008u
/* BEAM channel bridge only (beam_prepare/get/save/draw); the app drives the
 * FSK modem itself through bk_read/bk_write. */
#define APP_CAP_BEAM2         0x00000010u
/* Configuration proxy (app_config_proxy.h), introduced by firmware v6.2.0:
 * an editor app rewrites one bounded asset block of another app. Required by
 * the editor and by its target, whose settings cannot be changed without it;
 * earlier firmware refuses both with APP_ERR_CAP. */
#define APP_CAP_CFG_PROXY     0x00000020u
/* Serial ports (serial_read / serial_write / serial_ctl, API level 3): the
 * USB-C port; serial_ctl tells which ports this firmware has (APP_SERIAL_OK). */
#define APP_CAP_SERIAL        0x00000040u

/* Aligned RAM objects; app_overlay.c pins every serialized field offset. */
typedef struct {
    uint32_t magic;                    /* APP_MAGIC                              */
    uint16_t hdr_version;              /* APP_HDR_VERSION                        */
    uint8_t  abi_major;                /* required ABI family                    */
    uint8_t  api_min;                  /* minimum append-only API level          */
    uint32_t code_size;               /* bytes of code, <= APP_OVERLAY_MAX      */
    uint32_t code_crc32;              /* CRC-32 (zlib) over code_size bytes     */
    uint16_t entry_off;               /* entry offset within the code (0)       */
    uint16_t flags;                   /* APP_FLAG_COMMITTED, ...                */
    char     name[APP_NAME_LEN];      /* human-readable, NUL-terminated         */
    char     version[APP_VERSION_LEN];/* app version string                     */
    uint32_t link_vma;                /* RAM VMA the code was linked at         */
    uint32_t required_caps;           /* APP_CAP_* required by this app         */
    uint16_t asset_size;              /* bytes at APP_ASSET_OFFSET, 0 = none    */
    uint16_t asset_crc;               /* low 16 bits of the assets' CRC-32      */
} app_header_t;

enum {
    APP_OK = 0,
    APP_ERR_SLOT,           /* slot index out of range                */
    APP_ERR_MAGIC,          /* no/invalid header                      */
    APP_ERR_ABI,            /* ABI family/API level mismatch          */
    APP_ERR_NOT_COMMITTED,  /* image not marked complete              */
    APP_ERR_SIZE,           /* code_size or asset_size out of range   */
    APP_ERR_CRC,            /* code or asset CRC mismatch             */
    APP_ERR_VMA,            /* overlay buffer not at the link VMA     */
    APP_ERR_AUTH,           /* host write refused: timestamp mismatch */
    APP_ERR_CAP,            /* required firmware capability missing   */
    APP_CHAIN_ABORTED,      /* runaway hand-off chain stopped safely  */
};

/* Read + validate a slot header (no CRC of the code). */
uint8_t APP_ValidateSlot(uint8_t slot, app_header_t *out_header);

/* Validate, load into the overlay, verify the code CRC, and run the app, then
 * each app it hands off to.  Returns when the last one exits, with its result;
 * APP_CHAIN_ABORTED means a runaway-chain guard stopped the sequence.  The
 * internal flash is never touched while an overlay is executing. */
uint8_t APP_LaunchOverlay(uint8_t slot);

/* Set when an app staged a VFO (vfo_stage): the Apps menu then hands back to
 * the main screen, and clears it. */
extern bool gAppVfoExit;

/* An open RX audio capture (af_capture_open) holds the ADC on PA4: Yield puts
 * the battery channel back for one conversion and returns whether it did, Resume
 * takes PA4 back. Used by BOARD_ADC_GetBatteryInfo(). */
bool    APP_AfCaptureYieldAdc(void);
void    APP_AfCaptureResumeAdc(void);

/* The slot of the last app APP_LaunchOverlay ran or tried to launch: the app
 * that ended a hand-off chain, or the target that failed.  Its header names
 * the app in an error and holds the APP_FLAG_EXIT_TO_MAIN that applies. */
extern uint8_t gAppLastSlot;

/* Block until no key and no PTT has been down for 100 ms.  Returns true on a
 * long EXIT press meanwhile (the loader then ends a hand-off chain). */
bool    APP_WaitKeyRelease(void);

/* Launch the first installed app advertising this shortcut bit. */
uint8_t APP_LaunchOverlayShortcut(uint8_t shortcut);

/* Cached availability of apps exposed as resident quick actions. */
bool    APP_OverlayShortcutInstalled(uint8_t shortcut);  /* an app holds this shortcut ID */

/* Host-tool slot management (external flash only, never brick-critical). */
uint8_t APP_SlotInfo(uint8_t slot, app_header_t *out_header);
uint8_t APP_SlotErase(uint8_t slot);
uint8_t APP_SlotWrite(uint8_t slot, uint32_t offset, const uint8_t *data, uint32_t len);

/* Change token used by the modal Apps selector.  A completed host install is
 * published after validation; erasing a slot publishes directly. */
uint8_t APP_SlotRevision(void);
void APP_NotifySlotChanged(void);


#endif /* APPS_APP_OVERLAY_H */
