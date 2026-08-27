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

#include "apps/app_overlay.h"

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS

#include <string.h>
#include "py32f0xx.h"

#include "driver/bk4819.h"
#include "driver/bk4819-regs.h"
#ifdef ENABLE_FMRADIO
#include "driver/bk1080.h"
#include "app/fm.h"
#endif
#include "driver/keyboard.h"
#include "driver/py25q16.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "driver/backlight.h"
#include "ui/helper.h"
#include "ui/status.h"
#include "board.h"
#include "audio.h"
#include "radio.h"
#include "helper/battery.h"
#include "settings.h"
#include "misc.h"   /* dBmCorrTable */

_Static_assert(sizeof(app_header_t) == 64, "app_header_t must be 64 bytes");

/* CRC-32 (zlib) over RAM bytes - matches App/apps/pack_app.py and the firmware's
 * mb_ext_image_crc32 (init 0xFFFFFFFF, poly 0xEDB88320, final XOR). */
static uint32_t app_crc32(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ---- ABI wrappers: the few resident calls that are not a direct signature match ---- */
static void    app_display_clear(void) { UI_DisplayClear(); }
static void    app_status_clear(void)  { UI_StatusClear(); }
static uint8_t app_get_key(void)       { return (uint8_t)KEYBOARD_GetKey(); }
static void    app_led(bool on)        { BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, on); }

static void app_play_tone(uint16_t tone, uint16_t ms)
{
    BK4819_PrepareToPlayTone(true);
    AUDIO_AudioPathOn();
    BK4819_PlayToneRaw(tone, ms);
    AUDIO_AudioPathOff();
}

static uint32_t app_make_seed(void)
{
    /* Reproduces the original Breakout seed source. */
    return BK4819_ReadRegister(BK4819_REG_67) & 0x01FF
           * gBatteryVoltageAverage
           * gEeprom.VfoInfo[0].pRX->Frequency;
}

/* ---- v2 radio wrappers ---- */
static int16_t  app_rssi_dbm(void)     { return BK4819_GetRSSI_dBm() + dBmCorrTable[gRxVfo->Band]; }
static uint16_t app_bk_read(uint8_t r) { return BK4819_ReadRegister((BK4819_REGISTER_t)r); }
static void     app_bk_write(uint8_t r, uint16_t v) { BK4819_WriteRegister((BK4819_REGISTER_t)r, v); }
static void     app_set_af(uint8_t m)  { BK4819_SetAF((BK4819_AF_Type_t)m); }
static void     app_audio_path(bool on){ if (on) AUDIO_AudioPathOn(); else AUDIO_AudioPathOff(); }
static void     app_prepare_tone(void) { BK4819_PrepareToPlayTone(true); }
static void     app_play_tone_raw(uint16_t hz, uint16_t ms) { BK4819_PlayToneRaw(hz, ms); }
static void     app_tones_off_rx(void) { BK4819_TurnsOffTones_TurnsOnRX(); }
static uint32_t app_rx_freq(void)      { return gRxVfo->pRX->Frequency; }

/* ---- v2 config (deferred, flash-backed) ----
 * Stored per app slot in the header sector, just after the 64-byte header. cfg_load
 * reads flash at launch (ReadBuffer bypasses the overlay cache). cfg_save only stages
 * into RAM - the app runs from the sector cache, so it cannot write flash itself; the
 * loader commits the staged bytes to flash after the app returns (RMW preserves the
 * slot header). Erasing/reinstalling a slot resets its config, which is intended. */
#define APP_CFG_OFFSET  0x40u    /* config area within the header sector */
static uint8_t app_cfg_buf[16];
static uint8_t app_cfg_len;      /* staged length; 0 = nothing to commit */
static uint8_t app_run_slot;     /* slot of the app currently running */

static void app_cfg_load(uint8_t *buf, uint8_t len)
{
    if (len > sizeof(app_cfg_buf)) len = sizeof(app_cfg_buf);
    PY25Q16_ReadBuffer(APP_SLOT_BASE(app_run_slot) + APP_CFG_OFFSET, buf, len);
}
static void app_cfg_save(const uint8_t *buf, uint8_t len)
{
    if (len > sizeof(app_cfg_buf)) len = sizeof(app_cfg_buf);
    memcpy(app_cfg_buf, buf, len);
    app_cfg_len = len;   /* mark dirty; the loader commits after the app returns */
}

/* ---- v2 battery / backlight ---- */
static void app_draw_battery(void)
{
    char t[8];
    UI_DrawStatusBattery(gStatusLine, t);
}
static void app_battery_sample(void)
{
    BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[gBatteryVoltageIndex++], &gBatteryCurrent);
    if (gBatteryVoltageIndex > 3)
        gBatteryVoltageIndex = 0;
    BATTERY_GetReadings(false);
}

/* ---- v2 TX (beacon) ---- */
static uint8_t app_tx_state(void)
{
    if (TX_freq_check(gTxVfo->pTX->Frequency) != 0 && gTxVfo->TX_LOCK) return 1; /* TX disable */
    if (gBatteryDisplayLevel == 0) return 2;  /* battery low */
    if (gBatteryDisplayLevel > 6)  return 3;  /* voltage high */
    if (gTxVfo->Modulation != MODULATION_FM) return 1;
    return 0;
}
static void     app_tx_set_params(void)  { RADIO_SetTxParameters(); }
static void     app_tx_tone(uint16_t hz) { BK4819_TransmitTone(false, hz); }
static void     app_tx_mute(bool on)     { if (on) BK4819_EnterTxMute(); else BK4819_ExitTxMute(); }
static void     app_tx_end(void)         { BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false); RADIO_SetupRegisters(true); }
static uint32_t app_tx_freq(void)        { return gTxVfo->pTX->Frequency; }
static void app_boot_callsign(char *buf, uint8_t len)
{
    char raw[12]; uint8_t n = 0;
    PY25Q16_ReadBuffer(0x00A0C8u, raw, sizeof(raw));   /* boot message line 1 */
    for (uint8_t i = 0; i < sizeof(raw) && (uint8_t)(n + 1) < len; i++) {
        char c = raw[i];
        if (c == '\0' || (uint8_t)c == 0xFFu) break;
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/') buf[n++] = c;
    }
    buf[n] = '\0';
}

#ifdef ENABLE_FMRADIO
/* ---- v2 broadcast FM (BK1080), sovereign (no BK4819 dual-watch) ---- */
static void app_fm_enter(uint16_t f, uint8_t b)
{
    BK1080_Init(f, b);
    BK4819_PickRXFilterPathBasedOnFrequency(10320000);   /* FM band antenna filter */
    AUDIO_AudioPathOn();
    gEnableSpeaker = true;
}
static void app_fm_exit(void)
{
    AUDIO_AudioPathOff();
    gEnableSpeaker = false;
    BK1080_Init0();
    BK4819_PickRXFilterPathBasedOnFrequency(gRxVfo->pRX->Frequency);   /* restore RX filter */
}
static void     app_fm_set_freq(uint16_t f, uint8_t b) { BK1080_SetFrequency(f, b); }
static uint16_t app_fm_read(uint8_t r) { return BK1080_ReadRegister((BK1080_Register_t)r); }
static uint16_t app_fm_lo(uint8_t b)   { return BK1080_GetFreqLoLimit(b); }
static uint16_t app_fm_hi(uint8_t b)   { return BK1080_GetFreqHiLimit(b); }
static void     app_fm_mute(bool m)    { BK1080_Mute(m); }
static int8_t   app_fm_valid(uint16_t f, uint16_t lo) { return (int8_t)FM_CheckFrequencyLock(f, lo); }

static bool app_fm_dirty;   /* deferred: SETTINGS_SaveFM committed after the app returns */
static void app_fm_state(app_fm_state_t *s, bool write)
{
    if (write) {
        gEeprom.FM_FrequencyPlaying  = s->freq_playing;
        gEeprom.FM_SelectedFrequency = s->sel_freq;
        gEeprom.FM_Band              = s->band & 3u;
        gEeprom.FM_IsMrMode          = s->is_mr ? true : false;
        gEeprom.FM_SelectedChannel   = s->sel_ch;
    } else {
        s->freq_playing = gEeprom.FM_FrequencyPlaying;
        s->sel_freq     = gEeprom.FM_SelectedFrequency;
        s->band         = gEeprom.FM_Band;
        s->is_mr        = gEeprom.FM_IsMrMode;
        s->sel_ch       = gEeprom.FM_SelectedChannel;
    }
}
static void app_fm_commit(void) { app_fm_dirty = true; }
#endif

uint8_t APP_ValidateSlot(uint8_t slot, app_header_t *out_header)
{
    if (slot >= APP_SLOT_COUNT)
        return APP_ERR_SLOT;

    app_header_t h;
    PY25Q16_ReadBuffer(APP_SLOT_BASE(slot), &h, sizeof(h));

    if (h.magic != APP_MAGIC)               return APP_ERR_MAGIC;
    if (h.hdr_version != APP_HDR_VERSION)    return APP_ERR_MAGIC;
    if (h.abi_version != APP_ABI_VERSION)    return APP_ERR_ABI;
    if (!(h.flags & APP_FLAG_COMMITTED))     return APP_ERR_NOT_COMMITTED;
    if (h.code_size < 2u || h.code_size > APP_OVERLAY_MAX ||
        (uint32_t)h.entry_off > h.code_size - 2u ||   /* leave room for a 2-byte Thumb insn */
        (h.entry_off & 1u) != 0u)                     /* entry must be Thumb-aligned (even) */
        return APP_ERR_SIZE;

    if (out_header)
        *out_header = h;
    return APP_OK;
}

uint8_t APP_SlotInfo(uint8_t slot, app_header_t *out_header)
{
    if (slot >= APP_SLOT_COUNT)
        return APP_ERR_SLOT;
    app_header_t h;
    PY25Q16_ReadBuffer(APP_SLOT_BASE(slot), &h, sizeof(h));
    if (out_header)
        *out_header = h;
    return (h.magic == APP_MAGIC) ? APP_OK : APP_ERR_MAGIC;
}

uint8_t APP_LaunchOverlay(uint8_t slot)
{
    app_header_t h;
    uint8_t rc = APP_ValidateSlot(slot, &h);
    if (rc != APP_OK)
        return rc;

    /* The app's absolute data references only resolve if it runs at the exact
     * VMA it was linked for. The overlay VMA varies with the firmware's RAM
     * layout (per preset/features), so the app records its link VMA and we
     * refuse a mismatch cleanly instead of jumping into misaddressed code. */
    uint8_t *ws = PY25Q16_OverlayBuffer();
    if (h.link_vma != (uint32_t)ws)
        return APP_ERR_VMA;

    /* Repurpose the sector cache: drop any cached config sector, load the code
     * straight in (ReadBuffer bypasses the cache), and verify it in RAM before
     * trusting it. Zeroing first leaves the app's .bss clean. */
    PY25Q16_InvalidateCache();
    memset(ws, 0, APP_OVERLAY_MAX);
    PY25Q16_ReadBuffer(APP_SLOT_BASE(slot) + APP_CODE_OFFSET, ws, h.code_size);

    if (app_crc32(ws, h.code_size) != h.code_crc32) {
        PY25Q16_InvalidateCache();
        return APP_ERR_CRC;
    }

    /* Ensure every store to the overlay is visible before we branch into it. */
    __DSB();
    __ISB();

    const app_api_t api = {
        .abi_version    = APP_ABI_VERSION,
        .fb             = gFrameBuffer,
        .display_clear  = app_display_clear,
        .status_clear   = app_status_clear,
        .draw_line      = UI_DrawLineBuffer,
        .draw_rect      = UI_DrawRectangleBuffer,
        .print_bold     = UI_PrintStringSmallBold,
        .print_tiny     = GUI_DisplaySmallest,
        .blit_full      = ST7565_BlitFullScreen,
        .blit_line      = ST7565_BlitLine,
        .blit_status    = ST7565_BlitStatusLine,
        .get_key        = app_get_key,
        .delay_ms       = SYSTEM_DelayMs,
        .backlight_tick = BACKLIGHT_UpdateTickless,
        .play_tone      = app_play_tone,
        .led            = app_led,
        .seed           = app_make_seed(),
        /* v2 */
        .print_normal   = UI_PrintStringSmallNormal,
        .print_inverse  = GUI_DisplaySmallestInverse,
        .display_freq   = UI_DisplayFrequency,
        .rssi_dbm       = app_rssi_dbm,
        .bk_read        = app_bk_read,
        .bk_write       = app_bk_write,
        .set_agc        = BK4819_SetAGC,
        .set_af         = app_set_af,
        .audio_path     = app_audio_path,
        .prepare_tone   = app_prepare_tone,
        .play_tone_raw  = app_play_tone_raw,
        .tones_off_rx   = app_tones_off_rx,
        .rx_freq        = app_rx_freq,
        .cfg_load       = app_cfg_load,
        .cfg_save       = app_cfg_save,
        .draw_battery   = app_draw_battery,
        .battery_sample = app_battery_sample,
        .backlight_on   = BACKLIGHT_TurnOn,
        .backlight_update = BACKLIGHT_Update,
        .status_line    = gStatusLine,
        .tx_state       = app_tx_state,
        .tx_set_params  = app_tx_set_params,
        .tx_tone        = app_tx_tone,
        .tx_mute        = app_tx_mute,
        .tx_end         = app_tx_end,
        .tx_freq        = app_tx_freq,
        .boot_callsign  = app_boot_callsign,
        .print_string   = UI_PrintString,
#ifdef ENABLE_FMRADIO
        .fm_enter       = app_fm_enter,
        .fm_exit        = app_fm_exit,
        .fm_set_freq    = app_fm_set_freq,
        .fm_read        = app_fm_read,
        .fm_lo          = app_fm_lo,
        .fm_hi          = app_fm_hi,
        .fm_mute        = app_fm_mute,
        .fm_valid       = app_fm_valid,
        .fm_channels    = gFM_Channels,
        .fm_state       = app_fm_state,
        .fm_commit      = app_fm_commit,
#endif
    };

    app_run_slot = slot;   /* for cfg_load / cfg_save */
    app_cfg_len  = 0;
#ifdef ENABLE_FMRADIO
    app_fm_dirty = false;
#endif

    /* Pin RX to the user-selected VFO before the app runs. Under dual watch
     * gRxVfo is whichever VFO the receiver was parked on when F+7 was pressed,
     * so an RF app (FoxHunt, a future S-meter, ...) would measure and display a
     * VFO the user did not pick - sometimes A, sometimes B. Point RX at the
     * selected (TX) VFO and retune so rx_freq(), rssi_dbm() and the tuned
     * hardware all agree on the selected channel. gTxVfo is left untouched, so
     * Beacon's tx_freq() stays correct too. State is saved and restored on
     * return so the resident dual watch resumes cleanly. */
    const uint8_t     saved_rx_vfo = gEeprom.RX_VFO;
    VFO_Info_t *const saved_rx     = gRxVfo;
    gEeprom.RX_VFO = gEeprom.TX_VFO;
    gRxVfo         = gTxVfo;
    RADIO_SetupRegisters(true);

    app_entry_t entry = (app_entry_t)(((uint32_t)ws + h.entry_off) | 1u);
    entry(&api);

    /* Restore the resident RX/dual-watch tuning the app ran on top of. */
    gEeprom.RX_VFO = saved_rx_vfo;
    gRxVfo         = saved_rx;
    RADIO_SetupRegisters(true);

    /* The overlay held app code, not a valid config sector. */
    PY25Q16_InvalidateCache();

    /* Commit any deferred config the app staged (RMW keeps the slot header). */
    if (app_cfg_len) {
        PY25Q16_WriteBuffer(APP_SLOT_BASE(slot) + APP_CFG_OFFSET, app_cfg_buf, app_cfg_len, false);
        PY25Q16_InvalidateCache();
    }
#ifdef ENABLE_FMRADIO
    /* Commit the FM config + 48 channels the app edited (shared with resident FM). */
    if (app_fm_dirty) {
        app_fm_dirty = false;
        SETTINGS_SaveFM();
        PY25Q16_InvalidateCache();
    }
#endif
    return APP_OK;
}

uint8_t APP_SlotErase(uint8_t slot)
{
    if (slot >= APP_SLOT_COUNT)
        return APP_ERR_SLOT;
    uint32_t base = APP_SLOT_BASE(slot);
    for (uint32_t off = 0; off < APP_SLOT_STRIDE; off += APP_SECTOR_SIZE)
        PY25Q16_SectorErase(base + off);
    PY25Q16_InvalidateCache();
    return APP_OK;
}

uint8_t APP_SlotWrite(uint8_t slot, uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (slot >= APP_SLOT_COUNT)
        return APP_ERR_SLOT;
    if (offset > APP_SLOT_STRIDE || len > APP_SLOT_STRIDE - offset)
        return APP_ERR_SIZE;
    PY25Q16_WriteBuffer(APP_SLOT_BASE(slot) + offset, data, len, false);
    PY25Q16_InvalidateCache();
    return APP_OK;
}

#endif /* ENABLE_FEAT_F4HWN_OVERLAY_APPS */
