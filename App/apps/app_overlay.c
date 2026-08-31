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
#include "driver/mb_flash.h"
#include "driver/py25q16.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "driver/backlight.h"
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
#include "k5viewer.h"
#endif
#include "app/app.h"
#include "ui/helper.h"
#include "ui/main.h"
#include "ui/status.h"
#include "board.h"
#include "audio.h"
#include "dcs.h"
#include "functions.h"
#include "frequencies.h"
#include "radio.h"
#include "helper/battery.h"
#include "settings.h"
#include "misc.h"   /* dBmCorrTable */

_Static_assert(sizeof(app_header_t) == 64, "app_header_t must be 64 bytes");

/* ---- ABI wrappers: the few resident calls that are not a direct signature match ---- */
static bool app_allow_screen_saver;
static bool app_screen_saver_wake;

static void app_backlight_on(void)
{
    APP_ModalScreenSaverExit();
    BACKLIGHT_TurnOn();
}

static void app_backlight_update(void)
{
    APP_ModalBacklightTick(app_allow_screen_saver);
}

static uint8_t app_get_key(void)
{
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    /* Overlay apps run synchronously outside APP_Update(). Keep serial key
     * injection alive while an app owns the foreground loop. */
    K5VIEWER_ParseInput();
#endif
    const KEY_Code_t key = KEYBOARD_GetKey();

    if (app_screen_saver_wake) {
        if (key == KEY_INVALID)
            app_screen_saver_wake = false;
        return APP_KEY_INVALID;
    }

    if (!APP_IsScreenSaverDisplayed())
        return (uint8_t)key;

    if (key == KEY_INVALID)
        return APP_KEY_SAVER;

    app_backlight_on();
    if (key == KEY_PTT)
        return APP_KEY_PTT;

    app_screen_saver_wake = true;
    return APP_KEY_WAKE;
}

#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
static void app_blit_full(void)
{
    ST7565_BlitFullScreen();
    /* The normal loop mirrors completed frames after drawing. Overlay apps
     * bypass that loop, so publish the frame from this ABI wrapper. */
    K5VIEWER_Update(false);
}
#endif

static int8_t  app_nav_dir(uint8_t key)
{
    int8_t direction;

    if (key == KEY_UP)
        direction = 1;
    else if (key == KEY_DOWN)
        direction = -1;
    else
        return 0;

    return gEeprom.SET_NAV ? direction : -direction;
}
static void    app_led(bool on)        { BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, on); }

static void app_play_tone(uint16_t tone, uint16_t ms)
{
    BK4819_PrepareToPlayTone(true);
    AUDIO_AudioPathOn();
    BK4819_PlayToneRaw(tone, ms);
    AUDIO_AudioPathOff();
}

/* ---- ABI 4: resident triple-VFO engine ---------------------------------
 * The overlay owns the UI and key timing, while this resident engine owns all
 * radio details.  Keeping VFO_Info_t and BK4819 sequencing on this side makes
 * the app independent of feature-dependent firmware layouts. */
#define APP_TRIVFO_COUNT          3u
#define APP_TRIVFO_TUNE_TICKS     5u    /* 100 ms at one tick / 20 ms */
#define APP_TRIVFO_TX_HOLD_TICKS 125u   /* keep the existing 2.5 s TX return */

static VFO_Info_t  app_trivfo_c;
static VFO_Info_t *app_trivfo_saved_rx;
static VFO_Info_t *app_trivfo_saved_tx;
static VFO_Info_t *app_trivfo_saved_current;
static uint8_t     app_trivfo_selected;
static uint8_t     app_trivfo_pre_rx_selected;
static uint8_t     app_trivfo_current;
static uint8_t     app_trivfo_settle;
static uint16_t    app_trivfo_hold;
static uint8_t     app_trivfo_candidate_wait;
static uint32_t    app_trivfo_tx_ticks;
static bool        app_trivfo_running;
static bool        app_trivfo_receiving;
static bool        app_trivfo_transmitting;
static bool        app_trivfo_sql_open;
static bool        app_trivfo_ctcss_ok;
static bool        app_trivfo_cdcss_ok;
static bool        app_trivfo_ab_dirty;
static bool        app_trivfo_restore_selection;
static bool        app_trivfo_onepush_stop_armed;
static uint8_t     app_trivfo_freq_dirty;

static void app_trivfo_end_tx(void);

static VFO_Info_t *app_trivfo_vfo(uint8_t vfo)
{
    return vfo < 2u ? &gEeprom.VfoInfo[vfo] : &app_trivfo_c;
}

static bool app_trivfo_load_memory(VFO_Info_t *vfo, uint16_t channel)
{
    ChannelScanDisplayInfo_t info;
    if (!IS_MR_CHANNEL(channel) ||
        !SETTINGS_FetchChannelScanDisplayInfo(channel, &info))
        return false;

    RADIO_InitInfo(vfo, channel, info.rx.Frequency);
    vfo->freq_config_RX              = info.rx;
    vfo->freq_config_TX              = info.tx;
    vfo->TX_OFFSET_FREQUENCY         = info.offset;
    vfo->StepFrequency               = info.stepFrequency;
    vfo->STEP_SETTING                = info.stepSetting;
    vfo->Modulation                  = info.modulation;
    vfo->TX_OFFSET_FREQUENCY_DIRECTION = info.txOffsetFrequencyDirection;
    vfo->OUTPUT_POWER                = info.outputPower;
    vfo->FrequencyReverse            = info.frequencyReverse;
    vfo->CHANNEL_BANDWIDTH           = info.channelBandwidth;
    vfo->BUSY_CHANNEL_LOCK           = info.busyChannelLock;
    vfo->TX_LOCK                     = info.txLock;
#ifdef ENABLE_DTMF_CALLING
    vfo->DTMF_DECODING_ENABLE        = info.dtmfDecodingEnable;
#endif
    vfo->DTMF_PTT_ID_TX_MODE         = info.dtmfPttIdTxMode;
    vfo->Band                        = FREQUENCY_GetBand(vfo->freq_config_RX.Frequency);
    vfo->Compander                   = MR_GetChannelAttributes(channel)->compander;
    SETTINGS_FetchChannelName(vfo->Name, channel);
    vfo->pRX = vfo->FrequencyReverse ? &vfo->freq_config_TX : &vfo->freq_config_RX;
    vfo->pTX = vfo->FrequencyReverse ? &vfo->freq_config_RX : &vfo->freq_config_TX;
    RADIO_ConfigureSquelchAndOutputPower(vfo);
    return true;
}

static uint16_t app_trivfo_next_channel(uint16_t channel, int8_t direction, uint8_t vfo)
{
    if (direction == 0)
        direction = 1;
    channel = RADIO_FindNextChannel((uint16_t)(channel + direction), direction,
                                    false, vfo < 2u ? vfo : 0u);
    return channel;
}

static void app_trivfo_tune(uint8_t vfo)
{
    app_trivfo_current = vfo % APP_TRIVFO_COUNT;
    gRxVfo = app_trivfo_vfo(app_trivfo_current);
    gCurrentVfo = gRxVfo;
    app_trivfo_receiving = false;
    app_trivfo_sql_open = false;
    app_trivfo_ctcss_ok = false;
    app_trivfo_cdcss_ok = false;
    app_trivfo_candidate_wait = 0;
    app_trivfo_settle = APP_TRIVFO_TUNE_TICKS;
    AUDIO_AudioPathOff();
    gEnableSpeaker = false;
    RADIO_SetupRegisters(false);
    FUNCTION_Init();
}

static void app_trivfo_poll_irq(void)
{
    while (BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
        BK4819_WriteRegister(BK4819_REG_02, 0);
        const uint16_t irq = BK4819_ReadRegister(BK4819_REG_02);
        if (irq & BK4819_REG_02_SQUELCH_LOST)  app_trivfo_sql_open = true;
        if (irq & BK4819_REG_02_SQUELCH_FOUND) {
            app_trivfo_sql_open = false;
            app_trivfo_ctcss_ok = false;
            app_trivfo_cdcss_ok = false;
        }

        /* A BK4819 CSS interrupt is a transition, not a persistent level.
         * Depending on the silicon revision and the configured polarity, the
         * first transition can be reported as FOUND or LOST.  MAIN keeps the
         * decoder state across both transitions; do the same here and use any
         * CSS transition as proof that the configured decoder has acquired the
         * signal.  A wrong CTCSS/DCS does not generate either transition. */
        if (irq & (BK4819_REG_02_CTCSS_LOST | BK4819_REG_02_CTCSS_FOUND))
            app_trivfo_ctcss_ok = true;
        if (irq & (BK4819_REG_02_CDCSS_LOST | BK4819_REG_02_CDCSS_FOUND))
            app_trivfo_cdcss_ok = true;
    }
}

static bool app_trivfo_qualified(void)
{
    const VFO_Info_t *vfo = app_trivfo_vfo(app_trivfo_current);
    if (!app_trivfo_sql_open)
        return false;
    if (vfo->Modulation != MODULATION_FM || vfo->pRX->CodeType == CODE_TYPE_OFF)
        return true;
    if (vfo->pRX->CodeType == CODE_TYPE_CONTINUOUS_TONE)
        return app_trivfo_ctcss_ok;
    return app_trivfo_cdcss_ok;
}

static uint16_t app_trivfo_enter(uint16_t c_channel)
{
    app_trivfo_saved_rx      = gRxVfo;
    app_trivfo_saved_tx      = gTxVfo;
    app_trivfo_saved_current = gCurrentVfo;

    if (!IS_MR_CHANNEL(c_channel) ||
        !SETTINGS_FetchChannelScanInfo(c_channel, NULL, NULL)) {
        uint16_t start = IS_MR_CHANNEL(gEeprom.ScreenChannel[1])
                       ? gEeprom.ScreenChannel[1] : gEeprom.MrChannel[1];
        c_channel = app_trivfo_next_channel(start, 1, 2);
    }
    if (c_channel == 0xFFFFu || !app_trivfo_load_memory(&app_trivfo_c, c_channel)) {
        c_channel = RADIO_FindNextChannel(MR_CHANNEL_FIRST, RADIO_CHANNEL_UP, false, 0);
        if (c_channel != 0xFFFFu)
            app_trivfo_load_memory(&app_trivfo_c, c_channel);
    }

    const uint8_t initial_vfo = gEeprom.TX_VFO < 2u ? gEeprom.TX_VFO : 0u;
    app_trivfo_selected = initial_vfo;
    app_trivfo_pre_rx_selected = initial_vfo;
    app_trivfo_restore_selection = false;
    app_trivfo_hold = 0;
    app_trivfo_running = true;
    app_trivfo_transmitting = false;
    app_trivfo_onepush_stop_armed = false;
    app_trivfo_ab_dirty = false;
    app_trivfo_freq_dirty = 0;
    app_trivfo_tune(initial_vfo);
    return c_channel;
}

static void app_trivfo_leave(void)
{
    if (!app_trivfo_running)
        return;
    if (app_trivfo_transmitting)
        app_trivfo_end_tx();
    AUDIO_AudioPathOff();
    gEnableSpeaker = false;
    app_trivfo_running = false;
    app_trivfo_transmitting = false;
    gRxVfo = app_trivfo_saved_rx;
    gTxVfo = app_trivfo_saved_tx;
    gCurrentVfo = app_trivfo_saved_current;
}

static void app_trivfo_get(uint8_t index, app_trivfo_info_t *info)
{
    if (info == NULL || index >= APP_TRIVFO_COUNT)
        return;
    const VFO_Info_t *vfo = app_trivfo_vfo(index);
    memset(info, 0, sizeof(*info));
    info->frequency  = vfo->pRX->Frequency;
    info->channel    = vfo->CHANNEL_SAVE;
    info->step       = vfo->StepFrequency;
    if (vfo->pRX->CodeType == CODE_TYPE_CONTINUOUS_TONE)
        info->code_value = CTCSS_Options[vfo->pRX->Code];
    else if (vfo->pRX->CodeType == CODE_TYPE_DIGITAL ||
             vfo->pRX->CodeType == CODE_TYPE_REVERSE_DIGITAL)
        info->code_value = DCS_Options[vfo->pRX->Code];
    info->rssi_dbm   = (index == app_trivfo_current)
                     ? BK4819_GetRSSI_dBm() + dBmCorrTable[vfo->Band] : -160;
    info->modulation = vfo->Modulation;
    info->power      = vfo->OUTPUT_POWER == OUTPUT_POWER_USER
                     ? (uint8_t)(gSetting_set_pwr + 1u) : vfo->OUTPUT_POWER;
    info->bandwidth  = vfo->CHANNEL_BANDWIDTH;
#ifdef ENABLE_FEAT_F4HWN_NARROWER
    if (info->bandwidth == BANDWIDTH_NARROW && gSetting_set_nfm == 1)
        info->bandwidth++;
#endif
    info->code_type  = vfo->pRX->CodeType;
    info->code       = vfo->pRX->Code;
    info->offset_direction = (vfo->freq_config_RX.Frequency != vfo->freq_config_TX.Frequency)
                           ? vfo->TX_OFFSET_FREQUENCY_DIRECTION : 0u;
    info->reverse    = vfo->FrequencyReverse;
    info->squelch    = gEeprom.SQUELCH_LEVEL;
    if (index == app_trivfo_selected) info->flags |= APP_TRIVFO_SELECTED;
    if (index == app_trivfo_current)  info->flags |= APP_TRIVFO_TUNED;
    if (index == app_trivfo_current && app_trivfo_receiving) info->flags |= APP_TRIVFO_RECEIVING;
    if (index == app_trivfo_selected && app_trivfo_transmitting) info->flags |= APP_TRIVFO_TX;
    if (vfo->OUTPUT_POWER == OUTPUT_POWER_USER) info->flags |= APP_TRIVFO_USER_POWER;
#ifdef ENABLE_AUDIO_BAR
    if (gSetting_mic_bar) info->flags |= APP_TRIVFO_AUDIO_BAR;
#endif
    if (gSetting_set_gui) info->flags |= APP_TRIVFO_GUI_CLASSIC;
    if (gSetting_set_ptt_session) info->flags |= APP_TRIVFO_PTT_ONEPUSH;
    if (IS_MR_CHANNEL(vfo->CHANNEL_SAVE))
        memcpy(info->name, vfo->Name, sizeof(info->name) - 1u);
}

static void app_trivfo_select(uint8_t vfo)
{
    if (vfo < APP_TRIVFO_COUNT)
        app_trivfo_selected = vfo;
}

static uint16_t app_trivfo_step(uint8_t index, int8_t direction)
{
    if (index >= APP_TRIVFO_COUNT || app_trivfo_transmitting)
        return 0xFFFFu;
    VFO_Info_t *vfo = app_trivfo_vfo(index);

    if (IS_FREQ_CHANNEL(vfo->CHANNEL_SAVE)) {
        if (direction == 0)
            direction = 1;
        const uint32_t frequency = APP_SetFrequencyByStep(vfo, direction);
        if (RX_freq_check(frequency) < 0)
            return 0xFFFFu;

        vfo->freq_config_RX.Frequency = frequency;
        RADIO_ApplyOffset(vfo);
        RADIO_ConfigureSquelchAndOutputPower(vfo);
        if (index < 2u)
            app_trivfo_freq_dirty |= (uint8_t)(1u << index);
        app_trivfo_hold = 0;
        app_trivfo_tune(index);
        return vfo->CHANNEL_SAVE;
    }

    uint16_t base = IS_MR_CHANNEL(vfo->CHANNEL_SAVE) ? vfo->CHANNEL_SAVE
                  : (index < 2u ? gEeprom.MrChannel[index] : gEeprom.MrChannel[1]);
    const uint16_t channel = app_trivfo_next_channel(base, direction, index);
    if (channel == 0xFFFFu)
        return channel;
    if (index < 2u) {
        gEeprom.ScreenChannel[index] = channel;
        gEeprom.MrChannel[index] = channel;
        RADIO_ConfigureChannel(index, VFO_CONFIGURE_RELOAD);
        app_trivfo_ab_dirty = true;
    } else {
        app_trivfo_load_memory(&app_trivfo_c, channel);
    }
    app_trivfo_hold = 0;
    app_trivfo_tune(index);
    return channel;
}

static void app_trivfo_end_tx(void)
{
    if (!app_trivfo_transmitting)
        return;
    RADIO_SendEndOfTransmission();
    app_trivfo_transmitting = false;
    app_trivfo_onepush_stop_armed = false;
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    app_trivfo_hold = APP_TRIVFO_TX_HOLD_TICKS;
    app_trivfo_tune(app_trivfo_selected);
}

static uint8_t app_trivfo_tick(void)
{
    if (!app_trivfo_running)
        return APP_TRIVFO_SCAN;
    if (app_trivfo_transmitting) {
        const uint32_t timeout = ((uint32_t)gEeprom.TX_TIMEOUT_TIMER + 1u) * 250u;
        if (++app_trivfo_tx_ticks >= timeout) {
            app_trivfo_end_tx();
            return APP_TRIVFO_HOLD;
        }
        return APP_TRIVFO_TX_STATE;
    }

    app_trivfo_poll_irq();
    if (app_trivfo_settle > 0) {
        app_trivfo_settle--;
        return APP_TRIVFO_SCAN;
    }

    const bool qualified = app_trivfo_qualified();
    if (qualified) {
        app_trivfo_hold = 0;
        if (!app_trivfo_receiving) {
            if (app_trivfo_selected != app_trivfo_current) {
                app_trivfo_pre_rx_selected = app_trivfo_selected;
                app_trivfo_restore_selection = true;
                app_trivfo_selected = app_trivfo_current;
            }
            app_trivfo_receiving = true;
            AUDIO_AudioPathOn();
            gEnableSpeaker = true;
            BK4819_SetRxAudioGain();
            /* BK4819_SetupSquelch() ends by selecting AF_MUTE. Mirror
             * APP_StartListening(): restore the channel demodulator only once
             * the carrier/CSS has qualified. */
            RADIO_SetModulation(gRxVfo->Modulation);
            BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
        }
        return APP_TRIVFO_RX;
    }

    if (app_trivfo_receiving) {
        app_trivfo_receiving = false;
        AUDIO_AudioPathOff();
        gEnableSpeaker = false;
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
        /* Use the same receive-response dwell as resident DWR.  The resident
         * value is expressed in 10 ms ticks; Triple VFO ticks every 20 ms. */
        app_trivfo_hold = (dual_watch_count_after_2_10ms + 1u) / 2u;
        app_trivfo_candidate_wait = 50u;
    }
    if (app_trivfo_hold > 0) {
        app_trivfo_hold--;
        if (app_trivfo_hold == 0 && app_trivfo_restore_selection) {
            app_trivfo_selected = app_trivfo_pre_rx_selected;
            app_trivfo_restore_selection = false;
        }
        return APP_TRIVFO_HOLD;
    }

    /* Like resident dual watch, give a coded carrier time to acquire its CSS.
     * A wrong tone/code must not monopolise the receiver indefinitely. */
    if (app_trivfo_sql_open && app_trivfo_candidate_wait < 50u) {
        app_trivfo_candidate_wait++;
        return APP_TRIVFO_HOLD;
    }

    app_trivfo_tune((uint8_t)((app_trivfo_current + 1u) % APP_TRIVFO_COUNT));
    return APP_TRIVFO_SCAN;
}

static uint8_t app_trivfo_ptt(bool pressed)
{
    if (!app_trivfo_running)
        return 1;
    if (!pressed) {
        if (!app_trivfo_transmitting)
            return 0;
        /* ONEPUSH mirrors the resident PTT sequence: the first release keeps
         * TX keyed; the release following the second press ends TX. */
        if (gSetting_set_ptt_session && !app_trivfo_onepush_stop_armed)
            return 0;
        app_trivfo_end_tx();
        return 0;
    }
    if (app_trivfo_transmitting) {
        if (gSetting_set_ptt_session)
            app_trivfo_onepush_stop_armed = true;
        return 0;
    }

    VFO_Info_t *vfo = app_trivfo_vfo(app_trivfo_selected);
    if ((TX_freq_check(vfo->pTX->Frequency) != 0 && vfo->TX_LOCK) ||
        vfo->Modulation != MODULATION_FM ||
        (vfo->BUSY_CHANNEL_LOCK && app_trivfo_receiving) ||
        gBatteryDisplayLevel == 0 || gBatteryDisplayLevel > 6)
        return 1;

    AUDIO_AudioPathOff();
    gEnableSpeaker = false;
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    gRxVfo = gTxVfo = gCurrentVfo = vfo;
    RADIO_SetTxParameters();
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
    BK4819_DisableScramble();
    app_trivfo_current = app_trivfo_selected;
    app_trivfo_receiving = false;
    app_trivfo_transmitting = true;
    app_trivfo_onepush_stop_armed = false;
    app_trivfo_tx_ticks = 0;
    return 0;
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
    /* The resident scheduler deliberately skips ADC battery updates while the
     * PA is keyed.  Do the same for Triple VFO: sampling the loaded voltage as
     * capacity made an 80% pack appear to fall immediately to about 16%. */
    if (app_trivfo_transmitting)
        return;

    BATTERY_Sample(false);
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

static bool app_shortcuts_cached;
static uint8_t app_shortcut_mask;
static uint8_t app_shortcut_slots[3];

static int8_t app_shortcut_index(uint8_t shortcut)
{
    if (shortcut == APP_SHORTCUT_FM)      return 0;
    if (shortcut == APP_SHORTCUT_FOXHUNT) return 1;
    if (shortcut == APP_SHORTCUT_BEACON)  return 2;
    return -1;
}

static void app_cache_shortcuts(void)
{
    if (app_shortcuts_cached)
        return;

    app_shortcut_mask = 0;
    const uint32_t overlay_vma = (uint32_t)PY25Q16_OverlayBuffer();

    for (uint8_t slot = 0; slot < APP_SLOT_COUNT; slot++) {
        app_header_t h;
        if (APP_ValidateSlot(slot, &h) != APP_OK || h.link_vma != overlay_vma)
            continue;

        const uint8_t shortcut = (uint8_t)((h.flags & APP_FLAG_SHORTCUT_MASK) >>
                                           APP_FLAG_SHORTCUT_SHIFT);
        const int8_t index = app_shortcut_index(shortcut);
        if (index >= 0) {
            if (!(app_shortcut_mask & shortcut)) {
                app_shortcut_mask |= shortcut;
                app_shortcut_slots[index] = slot;
            }
        }
    }

    app_shortcuts_cached = true;
}

uint8_t APP_OverlayShortcutMask(void)
{
    app_cache_shortcuts();
    return app_shortcut_mask;
}

uint8_t APP_LaunchOverlayShortcut(uint8_t shortcut)
{
    const int8_t index = app_shortcut_index(shortcut);
    if (index < 0)
        return APP_ERR_MAGIC;

    app_cache_shortcuts();
    return (app_shortcut_mask & shortcut)
         ? APP_LaunchOverlay(app_shortcut_slots[index])
         : APP_ERR_MAGIC;
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

/* All services are immutable.  Keeping the table in flash avoids rebuilding a
 * roughly quarter-kilobyte automatic object on every launch and removes that
 * object from the launcher's stack frame. */
static const app_api_t app_api = {
    .abi_version      = APP_ABI_VERSION,
    .fb               = gFrameBuffer,
    .display_clear    = UI_DisplayClear,
    .status_clear     = UI_StatusClear,
    .draw_line        = UI_DrawLineBuffer,
    .draw_rect        = UI_DrawRectangleBuffer,
    .print_bold       = UI_PrintStringSmallBold,
    .print_tiny       = GUI_DisplaySmallest,
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    .blit_full        = app_blit_full,
#else
    .blit_full        = ST7565_BlitFullScreen,
#endif
    .blit_line        = ST7565_BlitLine,
    .blit_status      = ST7565_BlitStatusLine,
    .get_key          = app_get_key,
    .delay_ms         = SYSTEM_DelayMs,
    .play_tone        = app_play_tone,
    .led              = app_led,
    .print_normal     = UI_PrintStringSmallNormal,
    .print_inverse    = GUI_DisplaySmallestInverse,
    .display_freq     = UI_DisplayFrequency,
    .rssi_dbm         = app_rssi_dbm,
    .bk_read          = app_bk_read,
    .bk_write         = app_bk_write,
    .set_agc          = BK4819_SetAGC,
    .set_af           = app_set_af,
    .audio_path       = app_audio_path,
    .prepare_tone     = app_prepare_tone,
    .play_tone_raw    = app_play_tone_raw,
    .tones_off_rx     = app_tones_off_rx,
    .rx_freq          = app_rx_freq,
    .cfg_load         = app_cfg_load,
    .cfg_save         = app_cfg_save,
    .draw_battery     = app_draw_battery,
    .battery_sample   = app_battery_sample,
    .backlight_on     = app_backlight_on,
    .backlight_update = app_backlight_update,
    .audio_scope      = UI_DisplayAudioScopeOverlay,
    .status_line      = gStatusLine,
    .tx_state         = app_tx_state,
    .tx_set_params    = app_tx_set_params,
    .tx_tone          = app_tx_tone,
    .tx_mute          = app_tx_mute,
    .tx_end           = app_tx_end,
    .tx_freq          = app_tx_freq,
    .boot_callsign    = app_boot_callsign,
    .print_string     = UI_PrintString,
#ifdef ENABLE_FMRADIO
    .fm_enter         = app_fm_enter,
    .fm_exit          = app_fm_exit,
    .fm_set_freq      = app_fm_set_freq,
    .fm_lo            = app_fm_lo,
    .fm_hi            = app_fm_hi,
    .fm_mute          = app_fm_mute,
    .fm_valid         = app_fm_valid,
    .fm_channels      = gFM_Channels,
    .fm_state         = app_fm_state,
    .fm_commit        = app_fm_commit,
#endif
    .nav_dir          = app_nav_dir,
    .trivfo_enter     = app_trivfo_enter,
    .trivfo_leave     = app_trivfo_leave,
    .trivfo_get       = app_trivfo_get,
    .trivfo_select    = app_trivfo_select,
    .trivfo_step      = app_trivfo_step,
    .trivfo_tick      = app_trivfo_tick,
    .trivfo_ptt       = app_trivfo_ptt,
};

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

    if (MB_Crc32Bytes(ws, h.code_size) != h.code_crc32) {
        PY25Q16_InvalidateCache();
        return APP_ERR_CRC;
    }

    /* Ensure every store to the overlay is visible before we branch into it. */
    __DSB();
    __ISB();

    app_run_slot = slot;   /* for cfg_load / cfg_save */
    app_cfg_len  = 0;
#ifdef ENABLE_FMRADIO
    app_fm_dirty = false;
#endif

    app_allow_screen_saver = (h.flags & APP_FLAG_SCREEN_SAVER) != 0;
    app_screen_saver_wake = false;
    APP_ModalScreenSaverExit();
    BACKLIGHT_TurnOn();

#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    /* The caller enters from a debounced key event, so the resident key state
     * still contains that trigger while the modal app is running. Clear it so
     * K5Viewer is allowed to mirror overlay frames immediately. */
    gKeyReading0 = KEY_INVALID;
    gKeyReading1 = KEY_INVALID;
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
    entry(&app_api);

    APP_ModalScreenSaverExit();
    app_allow_screen_saver = false;
    app_screen_saver_wake = false;

    /* A defensive leave also covers an app returning through an error path. */
    app_trivfo_leave();

    /* Restore the resident RX/dual-watch tuning the app ran on top of. */
    gEeprom.RX_VFO = saved_rx_vfo;
    gRxVfo         = saved_rx;
    RADIO_SetupRegisters(true);

    /* The overlay held app code, not a valid config sector. */
    PY25Q16_InvalidateCache();

    if (app_trivfo_ab_dirty) {
        SETTINGS_SaveVfoIndices();
        app_trivfo_ab_dirty = false;
    }

    for (uint8_t i = 0; i < 2u; i++) {
        if (app_trivfo_freq_dirty & (1u << i))
            SETTINGS_SaveChannel(gEeprom.VfoInfo[i].CHANNEL_SAVE, i,
                                 &gEeprom.VfoInfo[i], 1);
    }
    app_trivfo_freq_dirty = 0;

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
    app_shortcuts_cached = false;
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
    app_shortcuts_cached = false;
    return APP_OK;
}

#endif /* ENABLE_FEAT_F4HWN_OVERLAY_APPS */
