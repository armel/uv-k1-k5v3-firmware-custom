/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
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

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "frequencies.h"
#include <helper/battery.h>
#include "radio.h"
#include <driver/backlight.h>

/* Shared PY25Q16 locations for the two configurable boot-message lines. */
#define SETTINGS_BOOT_MESSAGE_LINE1_ADDR 0x00A0C8u
#define SETTINGS_BOOT_MESSAGE_LINE2_ADDR 0x00A0D8u

enum POWER_OnDisplayMode_t {
#ifdef ENABLE_FEAT_F4HWN
    POWER_ON_DISPLAY_MODE_ALL,
    POWER_ON_DISPLAY_MODE_SOUND,
#else
    POWER_ON_DISPLAY_MODE_FULL_SCREEN = 0,
#endif
    POWER_ON_DISPLAY_MODE_MESSAGE,
    POWER_ON_DISPLAY_MODE_VOLTAGE,
#ifdef ENABLE_FEAT_F4HWN_LOGO
    POWER_ON_DISPLAY_MODE_LOGO,
#endif
    POWER_ON_DISPLAY_MODE_NONE,
};
typedef enum POWER_OnDisplayMode_t POWER_OnDisplayMode_t;

#ifdef ENABLE_CW_MODULATOR

// Bit flags for CW keyer input configuration
#define CW_KEY_FLAG_REVERSED      0x01  // 0=normal, 1=reversed
#define CW_KEY_FLAG_PORT_RING     0x02  // 0=no port ring, 1=use port ring input
#define CW_KEY_FLAG_SIDE1         0x04  // 0=not side1, 1=use SIDE1 as the ring paddle contact (PTT is tip)
#define CW_KEY_FLAG_NO_KEYER      0x08  // 0=keyer enabled, 1=handkey only
#define CW_KEY_FLAG_PORT_GROUND   0x10  // 0=no port ground, 1=use port ground
#define CW_KEY_FLAG_USB_PORT      0x20  // 0=not USB port, 1=use USB port (PA11/PA12) as paddle

// for reference, from ui/menu.c:
	// "PTT\nHandKey",
	// "Port\nHandKey",
	// "Side Btn\nIambic",
	// "Side Btn\nIambic\nReversed",
	// "Port\nIambic",
	// "Port\nIambic\nReversed",
	// "Port+Btn\nIambic",
	// "Port+Btn\nIambic\nReversed",
	// "USB Port\nIambic",
	// "USB Port\nIambic\nReversed",
	// "USB Port\nHandKey",

// CW key input selection (0-10) mapped to bitmap value - used for menu and eeprom
static const uint8_t CW_KEY_INPUT_menu_to_bitmap[11] = {
	0x08, // menu item 0: CW_KEY_FLAG_NO_KEYER - handkey only
	0x18, // menu item 1: CW_KEY_FLAG_NO_KEYER | CW_KEY_FLAG_PORT_GROUND - handkey + port ground
	0x04, // menu item 2: CW_KEY_FLAG_SIDE1 - buttons (PTT + SIDE1)
	0x05, // menu item 3: CW_KEY_FLAG_SIDE1 | CW_KEY_FLAG_REVERSED - buttons + reversed
	0x12, // menu item 4: CW_KEY_FLAG_PORT_RING | CW_KEY_FLAG_PORT_GROUND - port ring + port ground
	0x13, // menu item 5: CW_KEY_FLAG_PORT_RING | CW_KEY_FLAG_PORT_GROUND | CW_KEY_FLAG_REVERSED - port ring + port ground + reversed
	0x16, // menu item 6: CW_KEY_FLAG_SIDE1 | CW_KEY_FLAG_PORT_RING | CW_KEY_FLAG_PORT_GROUND - buttons + port ring + port ground
	0x17, // menu item 7: CW_KEY_FLAG_SIDE1 | CW_KEY_FLAG_PORT_RING | CW_KEY_FLAG_PORT_GROUND | CW_KEY_FLAG_REVERSED - buttons + port ring + port ground + reversed
	0x20, // menu item 8: CW_KEY_FLAG_USB_PORT - USB port (PA11=ring/DM, PA12=tip/DP) iambic
	0x21, // menu item 9: CW_KEY_FLAG_USB_PORT | CW_KEY_FLAG_REVERSED - USB port iambic reversed
	0x28, // menu item 10: CW_KEY_FLAG_USB_PORT | CW_KEY_FLAG_NO_KEYER - USB port handkey (either pin)
};

#define CW_KEY_INPUT_HANDKEY 0x08 // shortcut for the default no-keyer mode (menu item 0)

enum CW_IambicMode_t {
	CW_IAMBIC_MODE_A      = 0,
	CW_IAMBIC_MODE_B      = 1,
	CW_KEYER_MODE_ULTIMATIC = 2,
	CW_IAMBIC_MODE_BUG    = 3,
};
typedef enum CW_IambicMode_t CW_IambicMode_t;

// Proper roger dah length in dits (CWrgr menu); 3 is a plain R
#define CW_ROGER_DAH_DITS_MIN     3
#define CW_ROGER_DAH_DITS_MAX     20
#define CW_ROGER_DAH_DITS_DEFAULT 9

// Sidetone level (CWvol menu): 0 = off; see CW_ApplySidetoneGain for the gain curve
#define CW_SIDETONE_LEVEL_MAX     15
#define CW_SIDETONE_LEVEL_DEFAULT 7

// Break-in hang time in 10 ms units (CWhang menu): how long TX stays up after the last element
#define CW_HANG_10MS_MIN          1
#define CW_HANG_10MS_MAX          200
#define CW_HANG_10MS_DEFAULT      30

// Copy practice effective speed (Farnsworth): the slowest the gaps between
// characters can stretch to. It always stays below the keyer speed (max 45).
#define CW_FARNSWORTH_WPM_MIN     5

#endif

enum TxLockModes_t {
    F_LOCK_DEF, //all default frequencies + configurable
    F_LOCK_FCC,
#ifdef ENABLE_FEAT_F4HWN_CA
    F_LOCK_CA,
#endif
    F_LOCK_CE,
    F_LOCK_GB,
    F_LOCK_430,
    F_LOCK_438,
#ifdef ENABLE_FEAT_F4HWN_PMR
    F_LOCK_PMR,
#endif
#ifdef ENABLE_FEAT_F4HWN_GMRS_FRS_MURS
    F_LOCK_GMRS_FRS_MURS,
#endif
    F_LOCK_ALL, // disable TX on all frequencies
    F_LOCK_NONE, // enable TX on all frequencies
    F_LOCK_LEN
};

/*
enum {
    SCAN_RESUME_TO = 0,
    SCAN_RESUME_CO,
    SCAN_RESUME_SE
};
*/

enum {
    CROSS_BAND_OFF = 0,
    CROSS_BAND_CHAN_A,
    CROSS_BAND_CHAN_B
};

enum {
    DUAL_WATCH_OFF = 0,
    DUAL_WATCH_CHAN_A,
    DUAL_WATCH_CHAN_B,
#ifdef ENABLE_FEAT_F4HWN_FULL_WATCH
    DUAL_WATCH_FULL
#endif
};

enum {
    TX_OFFSET_FREQUENCY_DIRECTION_OFF = 0,
    TX_OFFSET_FREQUENCY_DIRECTION_ADD,
    TX_OFFSET_FREQUENCY_DIRECTION_SUB
};

enum {
    OUTPUT_POWER_USER = 0,
    OUTPUT_POWER_LOW1,
    OUTPUT_POWER_LOW2,
    OUTPUT_POWER_LOW3,
    OUTPUT_POWER_LOW4,
    OUTPUT_POWER_LOW5,
    OUTPUT_POWER_MID,
    OUTPUT_POWER_HIGH
};

enum ACTION_OPT_t {
    /* Persisted in EEPROM: never renumber or make these values conditional. */
    ACTION_OPT_NONE           = 0,
    ACTION_OPT_FLASHLIGHT     = 1,
    ACTION_OPT_POWER          = 2,
    ACTION_OPT_MONITOR        = 3,
    ACTION_OPT_SCAN           = 4,
    ACTION_OPT_VOX            = 5,
    ACTION_OPT_FM             = 6,
    ACTION_OPT_1750           = 7,
    ACTION_OPT_KEYLOCK        = 8,
    ACTION_OPT_A_B            = 9,
    ACTION_OPT_VFO_MR         = 10,
    ACTION_OPT_SWITCH_DEMODUL = 11,
    ACTION_OPT_RXMODE         = 12,
    ACTION_OPT_MAINONLY       = 13,
    ACTION_OPT_PTT            = 14,
    ACTION_OPT_WN             = 15,
    ACTION_OPT_MUTE           = 16,
    ACTION_OPT_RXA            = 17,

    /* Preset-specific actions keep their IDs even when not compiled. */
    ACTION_OPT_RXTX_LOG       = 18,
    ACTION_OPT_BEAM           = 19,
    ACTION_OPT_POWER_HIGH     = 20,
    ACTION_OPT_REMOVE_OFFSET  = 21,
    ACTION_OPT_FOXHUNT        = 22,
    ACTION_OPT_BEACON         = 23,
    ACTION_OPT_TONECALL       = 24,

    ACTION_OPT_UPSTREAM_END   = 25,   // one past upstream's last action

    /* NR7Y CW actions: fixed IDs in a block of their own from 32, so upstream
     * can keep adding actions at 25 and up without renumbering these. 25-31
     * are left unused. Ordered as the menu lists them. The CHIRP driver carries
     * the same numbers; v1.4 and older numbered them by build options (see
     * _NR7Y_LEGACY_ACTION_IDS in the CHIRP driver). */
    ACTION_OPT_CW_FIRST          = 32,
    ACTION_OPT_CW_KEY_INPUT      = 32,
    ACTION_OPT_CW_KEYER_MODE     = 33,
    ACTION_OPT_CW_SPEED          = 34,
    ACTION_OPT_CW_BREAK_IN       = 35,
    ACTION_OPT_CPO               = 36,
    ACTION_OPT_PLAY_CWMSG1       = 37,
    ACTION_OPT_PLAY_CWMSG2       = 38,
    ACTION_OPT_PLAY_CWMSG3       = 39,
    ACTION_OPT_PLAY_CWMSG4       = 40,
    ACTION_OPT_REPEAT_CWMSG1     = 41,
    ACTION_OPT_REPEAT_CWMSG2     = 42,
    ACTION_OPT_REPEAT_CWMSG3     = 43,
    ACTION_OPT_REPEAT_CWMSG4     = 44,
    ACTION_OPT_CW_PROPER_ROGER   = 45,
    ACTION_OPT_CW_END            = 46,   // one past the last CW action

#ifdef ENABLE_CW_MODULATOR
    ACTION_OPT_LEN            = ACTION_OPT_CW_END
#else
    ACTION_OPT_LEN            = ACTION_OPT_UPSTREAM_END
#endif
};

// Whether an ID names an action this build knows, CW or not. IDs between
// upstream's last action and the CW block are not actions: they read as unset,
// and menus step over them.
#define ACTION_OPT_IS_DEFINED(a) \
    ((a) < ACTION_OPT_UPSTREAM_END || ((a) >= ACTION_OPT_CW_FIRST && (a) < ACTION_OPT_LEN))

// Side key and M long actions used while the stored one is unset (erased 0xFF, or
// anything else that fails ACTION_OPT_IS_DEFINED): a fresh radio, or after Reset ALL. A stored
// action always wins, so a layout set from the menu or CHIRP is never replaced.
// The CHIRP driver carries the same table.
#ifdef ENABLE_CW_MODULATOR
    #define KEY_1_SHORT_DEFAULT  ACTION_OPT_CW_PROPER_ROGER
    #define KEY_1_LONG_DEFAULT   ACTION_OPT_PLAY_CWMSG1   // a CQ recorded in message 1
    #define KEY_2_SHORT_DEFAULT  ACTION_OPT_CW_SPEED
    #define KEY_2_LONG_DEFAULT   ACTION_OPT_CW_KEY_INPUT  // on F2: F1 becomes a paddle in the side button inputs
    #define KEY_M_LONG_DEFAULT   ACTION_OPT_MONITOR
#else
    #define KEY_1_SHORT_DEFAULT  ACTION_OPT_MONITOR
    #define KEY_1_LONG_DEFAULT   ACTION_OPT_NONE
    #define KEY_2_SHORT_DEFAULT  ACTION_OPT_SCAN
    #define KEY_2_LONG_DEFAULT   ACTION_OPT_NONE
    #define KEY_M_LONG_DEFAULT   ACTION_OPT_NONE
#endif

#ifdef ENABLE_VOICE
    enum VOICE_Prompt_t
    {
        VOICE_PROMPT_OFF = 0,
        VOICE_PROMPT_CHINESE,
        VOICE_PROMPT_ENGLISH
    };
    typedef enum VOICE_Prompt_t VOICE_Prompt_t;
#endif

enum ROGER_Mode_t {
    ROGER_MODE_OFF = 0,
    ROGER_MODE_ROGER,
    ROGER_MODE_MDC
};
typedef enum ROGER_Mode_t ROGER_Mode_t;

enum CHANNEL_DisplayMode_t {
    MDF_FREQUENCY = 0,
    MDF_CHANNEL,
    MDF_NAME,
    MDF_NAME_FREQ
};
typedef enum CHANNEL_DisplayMode_t CHANNEL_DisplayMode_t;

typedef struct {
    uint16_t               ScreenChannel[2]; // current channels set in the radio (memory or frequency channels)
    uint16_t               FreqChannel[2]; // last frequency channels used
    uint16_t               MrChannel[2]; // last memory channels used
#ifdef ENABLE_NOAA
    uint16_t           NoaaChannel[2];
#endif

    // The actual VFO index (0-upper/1-lower) that is now used for RX, 
    // It is being alternated by dual watch, and flipped by crossband
    uint8_t               RX_VFO;

    // The main VFO index (0-upper/1-lower) selected by the user
    // 
    uint8_t               TX_VFO;

    uint8_t               field7_0xa;
    uint8_t               field8_0xb;

#ifdef ENABLE_FMRADIO
    uint16_t          FM_SelectedFrequency;
    uint8_t           FM_SelectedChannel;
    bool              FM_IsMrMode;
    uint16_t          FM_FrequencyPlaying;
    uint8_t           FM_Band  : 2;
    //uint8_t         FM_Space : 2;
#endif

    uint8_t               SQUELCH_LEVEL;
    uint8_t               TX_TIMEOUT_TIMER;
    bool                  KEY_LOCK;
#ifdef ENABLE_FEAT_F4HWN
    bool                  SET_NAV;
#endif
#ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
    bool                  MENU_LOCK;
    uint8_t               SET_KEY;
#endif
    bool                  VOX_SWITCH;
    uint8_t               VOX_LEVEL;
#ifdef ENABLE_VOICE
    VOICE_Prompt_t    VOICE_PROMPT;
#endif
    bool                  BEEP_CONTROL;
    uint8_t               CHANNEL_DISPLAY_MODE;
    bool                  TAIL_TONE_ELIMINATION;
    bool                  VFO_OPEN;
    uint8_t               DUAL_WATCH;
    uint8_t               CROSS_BAND_RX_TX;
    uint8_t               BATTERY_SAVE;
    uint8_t               BACKLIGHT_TIME;
    uint8_t               SCAN_RESUME_MODE;
    uint8_t               SCAN_LIST_DEFAULT;
    uint32_t              SCAN_LIST_MIX_MASK;
    bool                  SCAN_LIST_ENABLED;
    uint16_t              SCANLIST_PRIORITY_CH[6];
//#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE // Fix me !!! What the hell is this?
    uint8_t               CURRENT_STATE;
    uint8_t               CURRENT_LIST;
//#endif                                // Fix me !!! What the hell is this?

    uint8_t               field29_0x26;
    uint8_t               field30_0x27;
    
    uint8_t               field37_0x32;
    uint8_t               field38_0x33;

    uint8_t               AUTO_KEYPAD_LOCK;
    POWER_OnDisplayMode_t POWER_ON_DISPLAY_MODE;
    ROGER_Mode_t          ROGER;
    uint8_t               REPEATER_TAIL_TONE_ELIMINATION;
    uint8_t               KEY_1_SHORT_PRESS_ACTION;
    uint8_t               KEY_1_LONG_PRESS_ACTION;
    uint8_t               KEY_2_SHORT_PRESS_ACTION;
    uint8_t               KEY_2_LONG_PRESS_ACTION;
    uint8_t               MIC_SENSITIVITY;
    uint8_t               MIC_SENSITIVITY_TUNING;
    uint16_t              CHAN_1_CALL;
#ifdef ENABLE_DTMF_CALLING
    char                  ANI_DTMF_ID[8];
    char                  KILL_CODE[8];
    char                  REVIVE_CODE[8];
#endif
    char                  DTMF_UP_CODE[16];

    uint8_t               field57_0x6c;
    uint8_t               field58_0x6d;

    char                  DTMF_DOWN_CODE[16];

    uint8_t               field60_0x7e;
    uint8_t               field61_0x7f;

#ifdef ENABLE_DTMF_CALLING
    char                  DTMF_SEPARATE_CODE;
    char                  DTMF_GROUP_CALL_CODE;
    uint8_t               DTMF_DECODE_RESPONSE;
    uint8_t               DTMF_auto_reset_time;
#endif  
    uint16_t              DTMF_PRELOAD_TIME;
    uint16_t              DTMF_FIRST_CODE_PERSIST_TIME;
    uint16_t              DTMF_HASH_CODE_PERSIST_TIME;
    uint16_t              DTMF_CODE_PERSIST_TIME;
    uint16_t              DTMF_CODE_INTERVAL_TIME;
    bool                  DTMF_SIDE_TONE;
#ifdef ENABLE_DTMF_CALLING
    bool                  PERMIT_REMOTE_KILL;
#endif
    int16_t               BK4819_XTAL_FREQ_LOW;
#ifdef ENABLE_NOAA
    bool              NOAA_AUTO_SCAN;
#endif
    uint8_t               VOLUME_GAIN;
    #ifdef ENABLE_FEAT_F4HWN
        uint8_t           VOLUME_GAIN_BACKUP;
    #endif
    uint8_t               DAC_GAIN;

    VFO_Info_t            VfoInfo[2];
    uint32_t              POWER_ON_PASSWORD;
    uint16_t              VOX1_THRESHOLD;
    uint16_t              VOX0_THRESHOLD;

    uint8_t               field77_0x95;
    uint8_t               field78_0x96;
    uint8_t               field79_0x97;

    uint8_t               KEY_M_LONG_PRESS_ACTION;
    uint8_t               BACKLIGHT_MIN;
    uint8_t               BACKLIGHT_MAX;
    BATTERY_Type_t        BATTERY_TYPE;
#ifdef ENABLE_RSSI_BAR
    uint8_t               S0_LEVEL;
    uint8_t               S9_LEVEL;
#endif
#ifdef ENABLE_CW_MODULATOR
	uint8_t			  	  CW_TONE_FREQUENCY; 	// Actual frequency in 10s of Hz (e.g. 60 for 600 Hz), stored in eeprom as 50 Hz steps from 450 (0=450, 1=500, ..., 15=1200), default 600
	uint8_t               CW_SIDETONE_LEVEL;	// CW sidetone level: raw menu index 0=off, 1-15 = volume level (see CW_ApplySidetoneGain for the actual BK4819 gain curve)
	CW_IambicMode_t       CW_KEYER_MODE;		// Iambic A, Iambic B, Ultimatic, or Bug/SAB (keyer disabled when CW_KEY_INPUT == HANDKEY)
	uint8_t               CW_KEY_WPM;			// actual WPM
	uint8_t               CW_KEY_INPUT;			// Bitmapped button/port input selections for CW keyer
	uint16_t			  CW_KEY_INPUT_MENU;	// index of the chosen input method in the menu
	bool     			  CW_BREAKIN_ENABLE;    // TX on key
	uint8_t               CW_MESSAGE_REPEAT_DELAY;  // Repeat delay in seconds
	uint8_t               CW_ROGER_DAH_DITS;    // proper roger dah length in dits
	uint8_t               CW_HANG_10MS;         // break-in hang time in 10 ms units
	uint8_t               CW_FARNSWORTH_WPM;    // copy practice effective speed, 0 = keyer spacing
	bool                  CW_FARNSWORTH_AUTO;   // copy practice narrows and widens the spacing on its own
#endif

} EEPROM_Config_t;

extern EEPROM_Config_t gEeprom;

typedef struct {
    FREQ_Config_t    rx;
    FREQ_Config_t    tx;
    uint32_t         offset;
    uint16_t         stepFrequency;
    STEP_Setting_t   stepSetting;
    ModulationMode_t modulation;
    uint8_t          txOffsetFrequencyDirection;
    uint8_t          outputPower;
    bool             frequencyReverse;
    uint8_t          channelBandwidth;
    uint8_t          busyChannelLock;
    uint8_t          txLock;
#ifdef ENABLE_DTMF_CALLING
    uint8_t          dtmfDecodingEnable;
#endif
    PTT_ID_t         dtmfPttIdTxMode;
} ChannelScanDisplayInfo_t;

#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT_HOT_CFG
void     SETTINGS_InitEEPROM(bool preserve_display_mode);
#else
void     SETTINGS_InitEEPROM(void);
#endif
void     SETTINGS_LoadCalibration(void);
uint32_t SETTINGS_FetchChannelFrequency(const uint16_t channel);
bool     SETTINGS_FetchChannelScanInfo(const uint16_t channel, uint32_t *frequency, ModulationMode_t *modulation);
bool     SETTINGS_FetchChannelScanDisplayInfo(const uint16_t channel, ChannelScanDisplayInfo_t *info);
#if defined(ENABLE_FEAT_F4HWN_FULL_WATCH) || defined(ENABLE_FEAT_F4HWN_SCAN_FASTER)
void     SETTINGS_ApplyChannelScanDisplayInfo(VFO_Info_t *vfo, uint16_t channel, const ChannelScanDisplayInfo_t *info);
#endif
void     SETTINGS_FetchChannelName(char *s, const uint16_t channel);
void     SETTINGS_FactoryReset(bool bIsAll);
#ifdef ENABLE_FMRADIO
    void SETTINGS_SaveFM(void);
#endif
void SETTINGS_SaveVfoIndices(void);
void SETTINGS_SaveVfoIndicesFlush(void);
void SETTINGS_SaveSettings(void);
void SETTINGS_SaveChannelName(uint16_t channel, const char * name);
void SETTINGS_SaveChannel(uint16_t Channel, uint8_t VFO, const VFO_Info_t *pVFO, uint8_t Mode);
void SETTINGS_SaveBatteryCalibration(const uint16_t * batteryCalibration);
void SETTINGS_UpdateChannel(uint16_t channel, const VFO_Info_t *pVFO, bool keep);
#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
    void SETTINGS_WriteCurrentState(void);
#endif
#ifdef ENABLE_FEAT_F4HWN_VOL
    void SETTINGS_WriteCurrentVol(void);
#endif
#ifdef ENABLE_FEAT_F4HWN
    void SETTINGS_ResetTxLock(void);
#endif

// ---- EEPROM schema ----
//
// Schema 1 (pre-v1.4) reused bit 6 of the channel flags byte as a NARROWEST
// selector on CW/USB records, which meant TX_LOCK could never be set on those
// channels and F Lock could not gate them. Schema 2 gives bit 6 back to
// TX_LOCK and keeps the filter in bit 1 as a plain wide/narrow flag.
//
// v2.0 keeps the marker in the CW block header (CW_BLOCK_SCHEMA_ADDR) and only
// ever writes schema 2. v1.4-v1.x kept it at 0xA170, which upstream armel uses
// for its scan-list mask. The firmware no longer migrates anything: the CHIRP
// module converts schema 1 images, and a radio upgraded without that keeps
// the old filter bits on its pre-v1.4 CW/USB channels.
#define EEPROM_SCHEMA_CURRENT  2   // v1.4: bit 6 is TX_LOCK, bit 1 is the filter

// ---- NR7Y CW block (v2.0) ----
//
// 64 bytes in the settings sector, clear of the upstream settings that end at
// 0xA177. Flash and logical (CHIRP) addresses are the same. The header is
// written last, in one write, so a valid one means everything it describes is
// in place; a future layout change does the same with the new version.
//
//   +0  'C','W','X'  signature
//   +3  layout version
//   +4  EEPROM schema (EEPROM_SCHEMA_*)
//   +5  reserved (3)
//   +8  CW settings, 8 bytes (moved from 0xA140)
//   +16 reserved for new gEeprom values (48)
#define CW_BLOCK_ADDR          0x00A200
#define CW_BLOCK_SIZE          0x40
#define CW_BLOCK_HDR_SIZE      8
#define CW_BLOCK_VERSION       1
#define CW_BLOCK_OFF_VERSION   3
#define CW_BLOCK_OFF_SCHEMA    4
#define CW_BLOCK_SCHEMA_ADDR   (CW_BLOCK_ADDR + CW_BLOCK_OFF_SCHEMA)
#define CW_SETTINGS_ADDR       (CW_BLOCK_ADDR + 0x08)

#define CW_BLOCK_SIG           "CWX"   // first 3 bytes, no terminator stored

#endif
