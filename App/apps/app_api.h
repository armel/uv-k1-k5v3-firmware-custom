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
 * Overlay-app ABI (POC).
 *
 * A leaf, modal "overlay app" is a self-contained code blob stored in the
 * external SPI flash and copied into the 4 KiB overlay RAM (the PY25Q16 sector
 * cache) to run, then discarded. It is linked with NOCROSSREFS and must not
 * reference resident firmware symbols: every service it needs is reached through
 * this table, which the loader fills and passes to the entry point.
 *
 *   void app_main(const app_api_t *api);   // entry, at blob offset 0
 *
 * Both the firmware loader and the app include THIS header, so the struct layout
 * can never disagree. Bump APP_ABI_VERSION on any incompatible change; the loader
 * refuses a blob whose header abi_version does not match.
 */

#ifndef APPS_APP_API_H
#define APPS_APP_API_H

#include <stdint.h>
#include <stdbool.h>

/* ABI 3: the table below (v1 core + the fields once labelled "v2 additions") is a
 * single versioned layout. Any change to app_api_t - a reorder, a removal, or an
 * append - MUST bump this. Keep in sync with ABI_VERSION in pack_app.py, which
 * stamps the blob the loader checks against. */
#define APP_ABI_VERSION   3u

/* KEY codes mirrored from driver/keyboard.h (enum KEY_Code_e). Kept in sync by
 * value so the app stays independent of the firmware headers. */
enum {
    APP_KEY_0       = 0,
    APP_KEY_1       = 1,
    APP_KEY_2       = 2,
    APP_KEY_3       = 3,
    APP_KEY_4       = 4,
    APP_KEY_5       = 5,
    APP_KEY_6       = 6,
    APP_KEY_7       = 7,
    APP_KEY_8       = 8,
    APP_KEY_9       = 9,
    APP_KEY_MENU    = 10,
    APP_KEY_UP      = 11,
    APP_KEY_DOWN    = 12,
    APP_KEY_EXIT    = 13,
    APP_KEY_STAR    = 14,
    APP_KEY_F       = 15,
    APP_KEY_INVALID = 19,
};

/* The framebuffer is the resident gFrameBuffer[FRAME_LINES][128]; the app draws
 * into it and calls a blit_* to push it to the LCD. */
typedef uint8_t (*app_fb_t)[128];

/* Broadcast FM shared state (mirrors gEeprom.FM_*), read/written via fm_state. */
#define APP_FM_CH_MAX 48
typedef struct {
    uint16_t freq_playing;   /* current tuned frequency (0.1 MHz) */
    uint16_t sel_freq;       /* last VFO frequency                */
    uint8_t  band;           /* 0..3                              */
    uint8_t  is_mr;          /* memory mode                       */
    uint8_t  sel_ch;         /* selected memory channel 0..47     */
} app_fm_state_t;

typedef struct app_api {
    uint8_t   abi_version;          /* == APP_ABI_VERSION                       */

    app_fb_t  fb;                   /* -> gFrameBuffer                          */

    /* ---- display ---- */
    void (*display_clear)(void);                                   /* UI_DisplayClear      */
    void (*status_clear)(void);                                    /* UI_StatusClear       */
    void (*draw_line)(app_fb_t fb, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, bool black);         /* UI_DrawLineBuffer    */
    void (*draw_rect)(app_fb_t fb, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, bool black);         /* UI_DrawRectangleBuffer */
    void (*print_bold)(const char *s, uint8_t start,
                       uint8_t end, uint8_t line);                 /* UI_PrintStringSmallBold */
    void (*print_tiny)(const char *s, uint8_t x, uint8_t y,
                       bool statusbar, bool fill);                 /* GUI_DisplaySmallest  */
    void (*blit_full)(void);                                       /* ST7565_BlitFullScreen */
    void (*blit_line)(unsigned line);                              /* ST7565_BlitLine      */
    void (*blit_status)(void);                                     /* ST7565_BlitStatusLine */

    /* ---- input / system ---- */
    uint8_t (*get_key)(void);       /* KEYBOARD_GetKey, returns an APP_KEY_* code */
    void    (*delay_ms)(uint32_t ms);                              /* SYSTEM_DelayMs        */
    void    (*backlight_tick)(void);                               /* BACKLIGHT_UpdateTickless */

    /* ---- audio / indicator ---- */
    void (*play_tone)(uint16_t tone, uint16_t ms);  /* full BK4819 tone burst + AF path */
    void (*led)(bool on);                           /* green GPIO indicator             */

    /* ---- data provided at launch ---- */
    uint32_t seed;                  /* resident-computed PRNG seed              */

    /* ==== appended when the ABI moved 1 -> 2; now an integral part of ABI 2.
     * A firmware and an app that agree on abi_version agree on this whole layout,
     * so these must never be reached through a table that does not include them. == */

    /* ---- extra text drawing ---- */
    void (*print_normal)(const char *s, uint8_t start, uint8_t end, uint8_t line); /* UI_PrintStringSmallNormal */
    void (*print_inverse)(const char *s, uint8_t x, uint8_t line,
                          bool statusbar, bool fill, uint8_t endX);                /* GUI_DisplaySmallestInverse */
    void (*display_freq)(const char *s, uint8_t x, uint8_t y, bool statusbar);     /* UI_DisplayFrequency (big font) */

    /* ---- BK4819 radio access ---- */
    int16_t  (*rssi_dbm)(void);                  /* corrected RSSI of the RX VFO, dBm      */
    uint16_t (*bk_read)(uint8_t reg);            /* BK4819_ReadRegister                    */
    void     (*bk_write)(uint8_t reg, uint16_t v);/* BK4819_WriteRegister                  */
    void     (*set_agc)(bool on);                /* BK4819_SetAGC                          */
    void     (*set_af)(uint8_t mode);            /* BK4819_SetAF (APP_AF_* below)          */
    void     (*audio_path)(bool on);             /* AUDIO_AudioPathOn/Off                  */
    void     (*prepare_tone)(void);              /* BK4819_PrepareToPlayTone(true)         */
    void     (*play_tone_raw)(uint16_t hz, uint16_t ms); /* BK4819_PlayToneRaw             */
    void     (*tones_off_rx)(void);              /* BK4819_TurnsOffTones_TurnsOnRX         */
    uint32_t (*rx_freq)(void);                   /* current RX VFO frequency (x10 Hz)      */

    /* ---- config persistence (deferred: staged now, committed on exit) ---- */
    void (*cfg_load)(uint8_t *buf, uint8_t len); /* read the app's saved config bytes      */
    void (*cfg_save)(const uint8_t *buf, uint8_t len); /* stage bytes; resident commits after the app returns */

    /* ---- battery + backlight ---- */
    void (*draw_battery)(void);      /* UI_DrawStatusBattery into the status line */
    void (*battery_sample)(void);    /* periodic ADC sample so the level stays live */
    void (*backlight_on)(void);      /* BACKLIGHT_TurnOn                          */
    void (*backlight_update)(void);  /* BACKLIGHT_Update (fade step)              */

    uint8_t *status_line;            /* -> gStatusLine (for status-bar icons)     */

    /* ---- TX (beacon) ---- */
    uint8_t  (*tx_state)(void);      /* 0 = OK to transmit, else a denial code    */
    void     (*tx_set_params)(void); /* RADIO_SetTxParameters (key up: carrier+PA) */
    void     (*tx_tone)(uint16_t hz);/* BK4819_TransmitTone prime (MCW tone)       */
    void     (*tx_mute)(bool on);    /* key the tone on(false)/off(true) via TxMute */
    void     (*tx_end)(void);        /* PA off + RADIO_SetupRegisters (back to RX) */
    uint32_t (*tx_freq)(void);       /* current TX VFO frequency (x10 Hz)          */
    void     (*boot_callsign)(char *buf, uint8_t len); /* sanitised callsign from the boot message */
    void     (*print_string)(const char *s, uint8_t start, uint8_t end,
                             uint8_t line, uint8_t width);   /* UI_PrintString (big font) */

    /* ---- broadcast FM (BK1080), sovereign: no BK4819 dual-watch ---- */
    void     (*fm_enter)(uint16_t freq, uint8_t band);  /* BK1080_Init + antenna filter + audio on */
    void     (*fm_exit)(void);                          /* audio off + BK1080_Init0 + restore filter */
    void     (*fm_set_freq)(uint16_t freq, uint8_t band);/* BK1080_SetFrequency (freq in 0.1 MHz) */
    uint16_t (*fm_read)(uint8_t reg);                   /* BK1080_ReadRegister (RSSI / valid) */
    uint16_t (*fm_lo)(uint8_t band);                    /* band low  limit (0.1 MHz) */
    uint16_t (*fm_hi)(uint8_t band);                    /* band high limit (0.1 MHz) */
    void     (*fm_mute)(bool mute);                     /* BK1080_Mute                */
    int8_t   (*fm_valid)(uint16_t freq, uint16_t lo);   /* FM_CheckFrequencyLock: 0 = station */
    uint16_t *fm_channels;                              /* -> gFM_Channels[APP_FM_CH_MAX], shared RAM r/w */
    void     (*fm_state)(app_fm_state_t *s, bool write);/* read/write the resident gEeprom.FM_* */
    void     (*fm_commit)(void);                        /* deferred SETTINGS_SaveFM (config + channels) */

    /* ---- navigation (ABI 3) ----
     * Convert a raw APP_KEY_UP/DOWN into a semantic value direction:
     *   UV-K5 UP/DOWN    -> +1/-1
     *   UV-K1 LEFT/RIGHT -> -1/+1
     * Returns 0 for any other key. Keep get_key() raw for spatial controls. */
    int8_t (*nav_dir)(uint8_t key);
} app_api_t;

/* BK4819 AF modes for set_af (mirror driver/bk4819.h values). */
enum { APP_AF_MUTE = 0, APP_AF_FM = 1, APP_AF_AM = 7 };

/* Register ids the apps use (mirror driver/bk4819-regs.h). */
enum { APP_BK_REG_13 = 0x13 };

/* Entry point every app blob exports, placed at blob offset 0. */
typedef void (*app_entry_t)(const app_api_t *api);

#endif /* APPS_APP_API_H */
