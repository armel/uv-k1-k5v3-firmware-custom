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
 * Both the firmware loader and the app include THIS header.  Within one ABI
 * major, app_api_t is append-only: existing fields may never move, disappear or
 * change signature.  Append a service and bump APP_API_LEVEL; only apps using
 * that service need the new level.  A breaking layout change bumps
 * APP_ABI_MAJOR and resets APP_API_LEVEL to 1.
 */

#ifndef APPS_APP_API_H
#define APPS_APP_API_H

#include <stdint.h>
#include <stdbool.h>

/* First unpublished/public baseline: all services currently present below are
 * ABI major 1, API level 1. */
#define APP_ABI_MAJOR  1u
#define APP_API_LEVEL  1u

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
    APP_KEY_PTT     = 16,
    APP_KEY_INVALID = 19,
    APP_KEY_SAVER   = 0xFD, /* virtual: saver active, skip this app frame       */
    APP_KEY_WAKE    = 0xFE, /* virtual: saver dismissed, redraw without action */
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

/* Compact, pointer-free description of one receiver in the resident triple-VFO
 * service.  Overlay apps must never see VFO_Info_t directly: its layout varies
 * with firmware features and contains resident pointers. */
typedef struct {
    uint32_t frequency;            /* RX frequency, x10 Hz                    */
    uint16_t channel;              /* memory channel, zero based              */
    uint16_t step;                 /* step, 10 Hz units                        */
    uint16_t code_value;           /* CTCSS x0.1 Hz or DCS octal source value  */
    int16_t  rssi_dbm;             /* last/current corrected RSSI              */
    uint8_t  modulation;
    uint8_t  power;
    uint8_t  bandwidth;
    uint8_t  code_type;
    uint8_t  code;
    uint8_t  offset_direction;
    uint8_t  reverse;
    uint8_t  squelch;
    uint8_t  flags;                /* APP_TRIVFO_* below                       */
    char     name[11];             /* channel name, trimmed and NUL terminated */
} app_trivfo_info_t;

enum {
    APP_TRIVFO_SELECTED  = 1u << 0,
    APP_TRIVFO_TUNED     = 1u << 1,
    APP_TRIVFO_RECEIVING = 1u << 2,
    APP_TRIVFO_TX        = 1u << 3,
    APP_TRIVFO_USER_POWER = 1u << 4,
    APP_TRIVFO_AUDIO_BAR = 1u << 5,
    APP_TRIVFO_GUI_CLASSIC = 1u << 6,
    APP_TRIVFO_PTT_ONEPUSH = 1u << 7,
};

enum {
    APP_TRIVFO_SCAN = 0,
    APP_TRIVFO_RX   = 1,
    APP_TRIVFO_HOLD = 2,
    APP_TRIVFO_TX_STATE = 3,
};

/* Pointer-free BEAM channel description.  The resident bridge translates this
 * stable ABI type to/from feature-dependent VFO_Info_t. */
typedef struct {
    uint32_t rx_frequency;
    uint32_t tx_offset_frequency;
    uint8_t  rx_code;
    uint8_t  tx_code;
    uint8_t  rx_codetype;
    uint8_t  tx_codetype;
    uint8_t  modulation;
    uint8_t  tx_offset_direction;
    uint8_t  tx_lock;
    uint8_t  busy_channel_lock;
    uint8_t  output_power;
    uint8_t  channel_bandwidth;
    uint8_t  frequency_reverse;
    uint8_t  dtmf_ptt_id_mode;
    uint8_t  dtmf_decoding_enable;
    uint8_t  step_setting;
    uint8_t  scrambling_type;
    uint8_t  band;
    uint8_t  scanlist;
    uint8_t  compander;
    char     name[16]; /* fixed-width wire field; NUL termination is not required */
} app_beam_channel_t;

_Static_assert(sizeof(app_beam_channel_t) == 44u,
               "BEAM ABI/wire channel layout changed");

enum {
    APP_BEAM_RX_WAIT  = 0,
    APP_BEAM_RX_READY = 1,
    APP_BEAM_RX_ERROR = 2,
};

/* CRITICAL PERSISTENCE RULE
 *
 * The app executes from the RAM buffer also used as PY25Q16's sector cache.
 * Therefore no API callback may erase or write external flash while app_main()
 * is running: a read-modify-write would overwrite the executing app.  Services
 * such as cfg_save, fm_commit and beam_save must only stage resident RAM state;
 * APP_LaunchOverlay commits it after app_main() returns. */

typedef struct app_api {
    /* Fixed four-byte prefix; services remain naturally pointer-aligned. */
    uint8_t   abi_major;            /* == APP_ABI_MAJOR                         */
    uint8_t   api_level;            /* == APP_API_LEVEL                         */
    uint16_t  api_size;             /* sizeof(app_api_t), for optional probing  */

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
    /* ---- audio / indicator ---- */
    void (*play_tone)(uint16_t tone, uint16_t ms);  /* full BK4819 tone burst + AF path */
    void (*led)(bool on);                           /* green GPIO indicator             */

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
    void (*backlight_update)(void);  /* resident fade + BLTime service            */
    void (*audio_scope)(uint8_t line, bool active); /* shared MAIN microphone scope */

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
    uint16_t (*fm_lo)(uint8_t band);                    /* band low  limit (0.1 MHz) */
    uint16_t (*fm_hi)(uint8_t band);                    /* band high limit (0.1 MHz) */
    void     (*fm_mute)(bool mute);                     /* BK1080_Mute                */
    int8_t   (*fm_valid)(uint16_t freq, uint16_t lo);   /* FM_CheckFrequencyLock: 0 = station */
    uint16_t *fm_channels;                              /* -> gFM_Channels[APP_FM_CH_MAX], shared RAM r/w */
    void     (*fm_state)(app_fm_state_t *s, bool write);/* read/write the resident gEeprom.FM_* */
    void     (*fm_commit)(void);                        /* deferred SETTINGS_SaveFM (config + channels) */

    /* ---- navigation (API level 1 baseline) ----
     * Convert a raw APP_KEY_UP/DOWN into a semantic value direction:
     *   UV-K5 UP/DOWN    -> +1/-1
     *   UV-K1 LEFT/RIGHT -> -1/+1
     * Returns 0 for any other key. Keep get_key() raw for spatial controls. */
    int8_t (*nav_dir)(uint8_t key);

    /* ---- triple VFO (optional resident capability APP_CAP_TRIVFO) ----
     * A and B are the live Main Display VFOs. C is a resident temporary VFO
     * loaded from c_channel (or the first valid memory after B when invalid).
     * tick is called every 20 ms by the app and returns APP_TRIVFO_* state. */
    uint16_t (*trivfo_enter)(uint16_t c_channel);
    void     (*trivfo_leave)(void);
    void     (*trivfo_get)(uint8_t vfo, app_trivfo_info_t *info);
    void     (*trivfo_select)(uint8_t vfo);
    uint16_t (*trivfo_step)(uint8_t vfo, int8_t direction);
    uint8_t  (*trivfo_tick)(void);
    uint8_t  (*trivfo_ptt)(bool pressed); /* physical PTT edge; resident applies SetPTT */

    /* ---- BEAM channel transfer (optional resident capability APP_CAP_BEAM) ---- */
    void     (*beam_prepare)(void); /* tune the fixed narrow-band FSK channel */
    void     (*beam_leave)(void); /* defensively stop FSK before app return */
    void     (*beam_get)(app_beam_channel_t *channel); /* export selected VFO */
    uint16_t (*beam_save)(const app_beam_channel_t *channel); /* first free MR, or 0xffff */
    void     (*beam_send)(uint16_t *packet); /* transmit one 36-word FSK packet */
    void     (*beam_rx)(bool start);         /* arm or stop FSK reception */
    uint8_t  (*beam_rx_poll)(uint16_t *packet); /* APP_BEAM_RX_* */
    void     (*beam_draw)(const char *status); /* MAIN display with one BEAM center line */
} app_api_t;

/* BK4819 AF modes for set_af (mirror driver/bk4819.h values). */
enum { APP_AF_MUTE = 0, APP_AF_FM = 1, APP_AF_AM = 7 };

/* Register ids the apps use (mirror driver/bk4819-regs.h). */
enum { APP_BK_REG_13 = 0x13 };

/* Entry point every app blob exports, placed at blob offset 0. */
typedef void (*app_entry_t)(const app_api_t *api);

#endif /* APPS_APP_API_H */
