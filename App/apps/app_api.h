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

#include <stddef.h>   /* offsetof */
#include <stdint.h>
#include <stdbool.h>

/* API levels within ABI major 1, one per published release (what a release
 * ships is frozen; services added before the next release join its level):
 *   1  v6.0.0 baseline: every service up to and including beam_draw
 *   2  ticks_ms, rand32, asset_read (+ app_header_t asset_size / asset_crc),
 *      idivmod, uidivmod (the resident division helpers), Labs system info,
 *      current and minimum-since-boot free stack/RAM margin
 *   3  append_i32 / append_u32 (resident decimal formatters), af_capture_open /
 *      sample / close (RX audio on PA4), cycles_48mhz, draw_frequency,
 *      handoff / handoff_read, tx_prepare; the Labs system information slots
 *      become part of the layout in every build, so level 3 services have one
 *      offset */
#define APP_ABI_MAJOR  1u
#define APP_API_LEVEL  3u

/* App name length in app_header_t, the name handoff() takes (API level 3). */
#define APP_NAME_LEN    16

/* Largest message handoff() carries from one app to the next (API level 3):
 * raw bytes, not NUL-terminated.  32 covers a position, a symbol and a short
 * ID.  It can be raised in a later release (one byte of resident RAM, i.e. of
 * stack margin, per byte; len stays uint8_t, 255 at most); an older firmware
 * then clamps, so receivers must check the length handoff_read() returns.  It
 * must never be lowered. */
#define APP_HANDOFF_MAX 32u

/* Minimum API level of an app that ships read-only assets (pack_app.py). */
#define APP_API_ASSETS 2u

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
    void     (*tx_carrier)(bool on); /* gate the PA on/off (beacon CARR carrier keying) */
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

    /* ---- BEAM channel transfer ----
     * APP_CAP_BEAM (v6.0.0, retired): every service below, FSK included.
     * APP_CAP_BEAM2: only beam_prepare (tunes the channel, no FSK setup),
     * beam_get, beam_save and beam_draw; the app drives the FSK modem through
     * bk_read/bk_write, and beam_leave/send/rx/rx_poll are NULL. */
    void     (*beam_prepare)(void); /* tune the fixed narrow-band FSK channel */
    void     (*beam_leave)(void); /* defensively stop FSK before app return */
    void     (*beam_get)(app_beam_channel_t *channel); /* export selected VFO */
    uint16_t (*beam_save)(const app_beam_channel_t *channel); /* first free MR, or 0xffff */
    void     (*beam_send)(uint16_t *packet); /* transmit one 36-word FSK packet */
    void     (*beam_rx)(bool start);         /* arm or stop FSK reception */
    uint8_t  (*beam_rx_poll)(uint16_t *packet); /* APP_BEAM_RX_* */
    void     (*beam_draw)(const char *status); /* MAIN display with one BEAM center line */

    /* ---- API level 2: time, randomness, read-only assets ---- */
    /* Free-running millisecond clock with 10 ms resolution (SysTick).  Compare
     * with unsigned subtraction: (api->ticks_ms() - start) >= period. */
    uint32_t (*ticks_ms)(void);
    /* xorshift32 PRNG, never 0.  The resident state persists across launches
     * and is re-mixed with RSSI noise and SysTick jitter at each launch, so apps
     * need no seed of their own. */
    uint32_t (*rand32)(void);
    /* Copy len bytes of this app's assets, starting at offset, into buf.  The
     * read is clamped to the packed asset size; returns the byte count copied
     * (0 past the end or when the app has no assets).  The loader verified the
     * assets' CRC before launch.  buf may live in the overlay (.bss) or on the
     * stack. */
    uint16_t (*asset_read)(uint16_t offset, void *buf, uint16_t len);

    /* ---- API level 2: integer division ---- */
    /* The resident run-time helpers (the Cortex-M0+ has no divide
     * instruction), so an app needs no libgcc division of its own: its
     * __aeabi_idivmod / __aeabi_uidivmod (and the __aeabi_idiv / __aeabi_uidiv
     * aliases) just forward here.  C division (truncated toward zero), the
     * quotient in the low word (r0) and the remainder in the high word (r1), as
     * the AEABI helpers return them; x / 0 gives 0, remainder x. */
    uint64_t (*idivmod)(int32_t n, int32_t d);
    uint64_t (*uidivmod)(uint32_t n, uint32_t d);

    /* ---- API level 2: zero-code Labs system information ----
     * Valid only with APP_CAP_SYSINFO (the loader sets them only with
     * ENABLE_FEAT_F4HWN_OVERLAY_INFO, NULL otherwise).  The fields themselves
     * are in the layout of every build, not under that #ifdef: apps are built
     * apart from the firmware and mostly without the define, so an #ifdef here
     * would put every later service at one offset in the app (276) and another
     * in a Labs firmware (328), and the app would call the wrong address.
     * Costs nothing in Labs, the only edition with overlay apps. */
    const char *sys_edition;
    const char *sys_version;
    const char *sys_build_date;
    const char *sys_build_time;
    const char *sys_build_commit;
    const void *sys_flash_end;
    const void *sys_ram_end;
    const uint16_t *sys_battery_voltage;
    const void *sys_battery_type;
    unsigned int (*sys_battery_percent)(unsigned int voltage_10mV);
    void (*sys_storage_read)(uint32_t address, void *buffer, uint32_t size);

    /* ---- API level 2: stack watermark diagnostics ---- */
    uint32_t (*sys_stack_free_now)(void);
    uint32_t (*sys_stack_free_min)(void);

    /* ---- API level 3: number formatting ---- */
    /* Signed decimal of v at dst, no leading zeros, '-' when negative, over the
     * whole int32 range. Not NUL-terminated: returns the end, so calls chain
     * as o = append_i32(o, v). Writes at most 11 characters. */
    char *(*append_i32)(char *dst, int32_t v);

    /* ---- API level 3: RX audio capture on PA4 ----
     * The RX audio reaches PA4 (the voice DAC pin), AC-coupled: open snapshots
     * GPIOA_MODER, RCC_APBENR1, DAC_CR, DAC_DHR12R1, ADC_SQR3 and ADC_SMPR3,
     * holds PA4 at mid-scale with the MCU DAC (channel 1, output buffer off)
     * and selects ADC channel 4 with the battery channel's sample time. While
     * the capture is open, battery_sample() switches the ADC to the battery
     * and back by itself. close restores all six registers; the loader also
     * closes a capture the app left open. open and close may be repeated. */
    void     (*af_capture_open)(void);
    uint16_t (*af_capture_sample)(void);   /* one bounded 12-bit conversion  */
    void     (*af_capture_close)(void);

    /* Per-application-run count of 48 MHz core cycles: the 10 ms tick and the
     * SysTick phase, read race-free. It wraps every ~89.5 s; its origin is
     * deliberately rebased before entry so the first wrap occurs ~10 s after
     * launch and exposes unsafe direct comparisons early. Never order readings
     * with < or >. Use app_cycles_before() / app_cycles_reached() for deadlines
     * less than 2^31 cycles (~44.7 s) away, or unsigned subtraction to measure
     * an elapsed interval shorter than one full wrap. */
    uint32_t (*cycles_48mhz)(void);

    /* ---- API level 3: the frequency as the main screen draws it ----
     * f in 10 Hz units, from column x. Below 1 GHz: the big digits up to the
     * kHz ("144.800") on lines line and line + 1, the last two digits in the
     * small font on line + 1 right after them (big digits 13 px, the point
     * 3 px: 95 px in all). From 1 GHz: the whole string in the normal font on
     * line (80 px). Nothing clips, so x is held at 33 at most. */
    void (*draw_frequency)(uint32_t f_10hz, uint8_t x, uint8_t line);

    /* Unsigned decimal of u at dst, no leading zeros, over the whole uint32
     * range. Not NUL-terminated: returns the end. Writes at most 10 chars. */
    char *(*append_u32)(char *dst, uint32_t u);

    /* ---- API level 3: hand-off to another app ----
     * Only one app runs at a time, so an app asks the loader to launch the
     * next one after it returns.  handoff() names the installed app to launch
     * and stages up to APP_HANDOFF_MAX bytes for it, in RAM only.  name is a
     * C string, e.g. handoff("APRS TX", ...), compared with the header name
     * over APP_NAME_LEN bytes at most.  The calling app then returns as usual:
     * the loader commits its config, waits for every key to be released, then
     * launches the target instead of going back to the menu, and the target's
     * APP_FLAG_EXIT_TO_MAIN decides where the chain ends.  true means a
     * compatible target (valid header, this firmware's overlay VMA) was found
     * and the launch is scheduled, not that it will succeed: a CRC failure is
     * reported by the menu under the target's name.  The last call wins: no
     * compatible app with that name returns false and leaves nothing pending,
     * which also cancels an earlier call.  Copies min(len, APP_HANDOFF_MAX)
     * bytes from msg; a longer len is clamped.  len 0 (msg may then be NULL)
     * is a hand-off without payload: the target still learns its sender.
     * A chain that never ends is cut back to the menu: a long EXIT press
     * between two apps, or 8 hand-offs in a row from apps that each ran less
     * than 300 ms. */
    bool    (*handoff)(const char *name, const void *msg, uint8_t len);
    /* The message handed to this app and its sender's name.  Returns -1 when
     * the app was launched from the menu or a shortcut (buf and from are then
     * untouched), else the message length (0 to APP_HANDOFF_MAX), of which
     * min(length, max) bytes are copied into buf: a return above max means
     * the message was cut.  buf may be NULL when max is 0.  from, unless NULL,
     * receives the sender's name as a C string: give it char[APP_NAME_LEN + 1].
     * Read once, -1 afterwards: call it at launch, before handoff(), which
     * replaces the message. */
    int16_t (*handoff_read)(void *buf, uint8_t max, char *from);

    /* tx_set_params without the FM waits and with PA_ENABLE left off, for a
     * caller that keys the PA itself (tx_set_params leaves an unshaped
     * carrier burst). On an older level 3 table, check api_size first. */
    void    (*tx_prepare)(void);
} app_api_t;

/* Unconditional, for the same reason: these offsets are what the apps and
 * the firmware must agree on, whatever the build options. */
_Static_assert(offsetof(app_api_t, sys_edition) == 276u &&
               offsetof(app_api_t, append_i32) == 328u,
               "the level 2 API prefix must stay 328 bytes");
_Static_assert(offsetof(app_api_t, handoff) == 356u &&
               offsetof(app_api_t, tx_prepare) == 364u &&
               sizeof(app_api_t) == 368u,
               "app_api_t is append-only: every edition shares this layout");

/* Wrap-safe modular ordering for deadlines less than half a cycle-counter
 * period away (~44.7 s at 48 MHz). Unused helpers emit no application code. */
static inline bool app_cycles_before(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) < 0;
}

static inline bool app_cycles_reached(uint32_t now, uint32_t deadline)
{
    return !app_cycles_before(now, deadline);
}

/* BK4819 AF modes for set_af (mirror driver/bk4819.h values). */
enum { APP_AF_MUTE = 0, APP_AF_FM = 1, APP_AF_AM = 7 };

/* Register ids the apps use (mirror driver/bk4819-regs.h). */
enum { APP_BK_REG_13 = 0x13 };

/* Entry point every app blob exports, placed at blob offset 0. */
typedef void (*app_entry_t)(const app_api_t *api);

#endif /* APPS_APP_API_H */
