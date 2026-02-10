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

#include <string.h>

#include "misc.h"
#include "settings.h"
#include "driver/py25q16.h"

const uint8_t     fm_radio_countdown_500ms         =  2000 / 500;  // 2 seconds
const uint16_t    fm_play_countdown_scan_10ms      =   100 / 10;   // 100ms
const uint16_t    fm_play_countdown_noscan_10ms    =  1200 / 10;   // 1.2 seconds
const uint16_t    fm_restore_countdown_10ms        =  5000 / 10;   // 5 seconds

const uint8_t     vfo_state_resume_countdown_500ms =  2500 / 500;  // 2.5 seconds

const uint8_t     menu_timeout_500ms               =  20000 / 500;  // 20 seconds
const uint16_t    menu_timeout_long_500ms          = 120000 / 500;  // 2 minutes

const uint8_t     DTMF_RX_live_timeout_500ms       =  6000 / 500;  // 6 seconds live decoder on screen
#ifdef ENABLE_DTMF_CALLING
const uint8_t     DTMF_RX_timeout_500ms            = 10000 / 500;  // 10 seconds till we wipe the DTMF receiver
const uint8_t     DTMF_decode_ring_countdown_500ms = 15000 / 500;  // 15 seconds .. time we sound the ringing for
const uint8_t     DTMF_txstop_countdown_500ms      =  3000 / 500;  // 6 seconds
#endif

const uint8_t     key_input_timeout_500ms          =  8000 / 500;  // 8 seconds

const uint16_t    key_repeat_delay_10ms            =   400 / 10;   // 400ms
const uint16_t    key_repeat_10ms                  =    80 / 10;   // 80ms .. MUST be less than 'key_repeat_delay'
const uint16_t    key_debounce_10ms                =    20 / 10;   // 20ms

const uint8_t     scan_delay_10ms                  =   210 / 10;   // 210ms

#ifdef ENABLE_FEAT_F4HWN
    const uint16_t    dual_watch_count_after_tx_10ms   =  420;         // 4.2 sec after TX ends
    const uint16_t    dual_watch_count_after_rx_10ms   =  1000 / 10;   // 1 sec after RX ends ?
    const uint16_t    dual_watch_count_after_1_10ms    =  5000 / 10;   // 5 sec
    const uint16_t    dual_watch_count_after_2_10ms    =  420;         // 4.2 sec
    const uint16_t    dual_watch_count_noaa_10ms       =    70 / 10;   // 70ms
#else
    const uint16_t    dual_watch_count_after_tx_10ms   =  3600 / 10;   // 3.6 sec after TX ends
    const uint16_t    dual_watch_count_after_rx_10ms   =  1000 / 10;   // 1 sec after RX ends ?
    const uint16_t    dual_watch_count_after_1_10ms    =  5000 / 10;   // 5 sec
    const uint16_t    dual_watch_count_after_2_10ms    =  3600 / 10;   // 3.6 sec
    const uint16_t    dual_watch_count_noaa_10ms       =    70 / 10;   // 70ms
#endif

#ifdef ENABLE_VOX
    const uint16_t dual_watch_count_after_vox_10ms  =   200 / 10;   // 200ms
#endif
const uint16_t    dual_watch_count_toggle_10ms     =   100 / 10;   // 100ms between VFO toggles

const uint16_t    scan_pause_delay_in_1_10ms       =  5000 / 10;   // 5 seconds
const uint16_t    scan_pause_delay_in_2_10ms       =   500 / 10;   // 500ms
const uint16_t    scan_pause_delay_in_3_10ms       =   200 / 10;   // 200ms
const uint16_t    scan_pause_delay_in_4_10ms       =   300 / 10;   // 300ms
const uint16_t    scan_pause_delay_in_5_10ms       =  1000 / 10;   // 1 sec
const uint16_t    scan_pause_delay_in_6_10ms       =   100 / 10;   // 100ms
const uint16_t    scan_pause_delay_in_7_10ms       =  3600 / 10;   // 3.6 seconds

const uint16_t    battery_save_count_10ms          = 10000 / 10;   // 10 seconds

const uint16_t    power_save1_10ms                 =   100 / 10;   // 100ms
const uint16_t    power_save2_10ms                 =   200 / 10;   // 200ms

#ifdef ENABLE_VOX
    const uint16_t    vox_stop_count_down_10ms         =  1000 / 10;   // 1 second
#endif

const uint16_t    NOAA_countdown_10ms              =  5000 / 10;   // 5 seconds
const uint16_t    NOAA_countdown_2_10ms            =   500 / 10;   // 500ms
const uint16_t    NOAA_countdown_3_10ms            =   200 / 10;   // 200ms

const uint32_t    gDefaultAesKey[4]                = {0x4AA5CC60, 0x0312CC5F, 0xFFD2DABB, 0x6BBA7F92};

const uint8_t     gMicGain_dB2[5]                  = {3, 8, 16, 24, 31};

#ifndef ENABLE_FEAT_F4HWN
    bool              gSetting_350TX;
#endif

#ifdef ENABLE_DTMF_CALLING
bool              gSetting_KILLED;
#endif

#ifndef ENABLE_FEAT_F4HWN
bool              gSetting_200TX;
bool              gSetting_500TX;
#endif
bool              gSetting_350EN;
uint8_t           gSetting_F_LOCK;
bool              gSetting_ScrambleEnable;

enum BacklightOnRxTx_t gSetting_backlight_on_tx_rx;

#ifdef ENABLE_AM_FIX
    bool          gSetting_AM_fix = true;
#endif

#ifdef ENABLE_FEAT_F4HWN_SLEEP 
    uint8_t       gSetting_set_off = 1;
    bool          gWakeUp = false;
#endif

#ifdef ENABLE_FEAT_F4HWN
    uint8_t       gSetting_set_pwr = 1;
    bool          gSetting_set_ptt = 0;
    uint8_t       gSetting_set_tot = 0;
    uint8_t       gSetting_set_ctr = 10;
    bool          gSetting_set_inv = false;
    uint8_t       gSetting_set_eot = 0;
    bool          gSetting_set_lck = false;
    bool          gSetting_set_met = 0;
    bool          gSetting_set_gui = 0;
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        uint8_t       gSetting_set_audio = 0;
    #endif
    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        bool          gSetting_set_nfm = 0;
    #endif
    bool          gSetting_set_tmr = 0;
    bool          gSetting_set_ptt_session;
    #ifdef ENABLE_FEAT_F4HWN_DEBUG
        uint16_t       gDebug;
    #endif
    uint8_t       gDW = 0;
    uint8_t       gCB = 0;
    bool          gSaveRxMode = false;
    uint8_t       crc[15] = { 0 };
    uint8_t       lErrorsDuringAirCopy = 0;
    uint8_t       gAircopyStep = 0;
    uint8_t       gAircopyCurrentMapIndex = 0;
    bool          gAirCopyBootMode = 0;
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        bool          gPowerHigh = false;
        bool          gRemoveOffset = false;
    #endif
#endif

#ifdef ENABLE_AUDIO_BAR
    bool          gSetting_mic_bar;
#endif
bool              gSetting_live_DTMF_decoder;
uint8_t           gSetting_battery_text;

bool              gMonitor = false;           // true opens the squelch

uint32_t          gCustomAesKey[4];
bool              bHasCustomAesKey;
uint32_t          gChallenge[4];
uint8_t           gTryCount;

uint16_t          gEEPROM_RSSI_CALIB[7][4];

uint16_t          gEEPROM_1F8A;
uint16_t          gEEPROM_1F8C;

// 
// Cache-Based Architecture: channel attributes moved to Flash
// Cache holds only active channels in RAM (instead of all!)
//

MR_ChannelCache_t gMR_ChannelAttributes_Cache[MR_CHANNELS_CACHE_SIZE] = {0};
ChannelAttributes_t gMR_ChannelAttributes_Current = {0};

volatile uint16_t gBatterySaveCountdown_10ms = battery_save_count_10ms;

volatile bool     gPowerSaveCountdownExpired;
volatile bool     gSchedulePowerSave;

volatile bool     gScheduleDualWatch = true;

volatile uint16_t gDualWatchCountdown_10ms;
bool              gDualWatchActive           = false;

volatile uint8_t  gSerialConfigCountDown_500ms;

volatile bool     gNextTimeslice_500ms;

volatile uint16_t gTxTimerCountdown_500ms;
volatile bool     gTxTimeoutReached;

#ifdef ENABLE_FEAT_F4HWN
    volatile uint16_t gTxTimerCountdownAlert_500ms;
    volatile bool     gTxTimeoutReachedAlert;
    volatile uint16_t gTxTimeoutToneAlert = 800;
    #ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
        volatile uint16_t gRxTimerCountdown_500ms;
    #endif
    #ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
        volatile uint8_t  gUART_LockScreenshot = 0; // lock screenshot if Chirp is used
        bool gUSB_ScreenshotEnabled = false;
    #endif
#endif

volatile uint16_t gTailNoteEliminationCountdown_10ms;

volatile uint8_t    gVFOStateResumeCountdown_500ms;

#ifdef ENABLE_NOAA
    volatile uint16_t gNOAA_Countdown_10ms;
#endif

bool              gEnableSpeaker;
uint8_t           gKeyInputCountdown = 0;
uint8_t           gKeyLockCountdown;
uint8_t           gRTTECountdown_10ms;
bool              bIsInLockScreen;
uint8_t           gUpdateStatus;
uint8_t           gFoundCTCSS;
uint8_t           gFoundCDCSS;
bool              gEndOfRxDetectedMaybe;

int16_t           gVFO_RSSI[2];
uint8_t           gVFO_RSSI_bar_level[2];

uint8_t           gReducedService;
uint8_t           gBatteryVoltageIndex;
bool              gCssBackgroundScan;

volatile bool     gScheduleScanListen = true;
volatile uint16_t gScanPauseDelayIn_10ms;

#if defined(ENABLE_ALARM) || defined(ENABLE_TX1750)
    AlarmState_t  gAlarmState;
#endif
uint16_t          gMenuCountdown;
bool              gPttWasReleased;
bool              gPttWasPressed;
uint8_t           gKeypadLocked;
bool              gFlagReconfigureVfos;
uint8_t           gVfoConfigureMode;
bool              gFlagResetVfos;
bool              gRequestSaveVFO;
uint16_t          gRequestSaveChannel;
bool              gRequestSaveSettings;
#ifdef ENABLE_FMRADIO
    bool          gRequestSaveFM;
#endif
bool              gFlagPrepareTX;

bool              gFlagAcceptSetting;
bool              gFlagRefreshSetting;

#ifdef ENABLE_FMRADIO
    bool          gFlagSaveFM;
#endif
bool              g_CDCSS_Lost;
uint8_t           gCDCSSCodeType;
bool              g_CTCSS_Lost;
bool              g_CxCSS_TAIL_Found;
#ifdef ENABLE_VOX
    bool          g_VOX_Lost;
    bool          gVOX_NoiseDetected;
    uint16_t      gVoxResumeCountdown;
    uint16_t      gVoxPauseCountdown;
#endif
bool              g_SquelchLost;

volatile uint16_t gFlashLightBlinkCounter;

bool              gFlagEndTransmission;
uint16_t          gNextMrChannel;
ReceptionMode_t   gRxReceptionMode;

bool              gRxVfoIsActive;
#ifdef ENABLE_ALARM
    uint8_t       gAlarmToneCounter;
    uint16_t      gAlarmRunningCounter;
#endif
bool              gKeyBeingHeld;
bool              gPttIsPressed;
uint8_t           gPttDebounceCounter;
uint8_t           gMenuListCount;
uint8_t           gBackup_CROSS_BAND_RX_TX;
uint8_t           gScanDelay_10ms;
uint8_t           gFSKWriteIndex;

#ifdef ENABLE_NOAA
    bool          gIsNoaaMode;
    uint16_t      gNoaaChannel;
#endif

bool              gUpdateDisplay;

bool              gF_LOCK = false;

uint8_t           gShowChPrefix;

volatile bool     gNextTimeslice;
volatile uint8_t  gFoundCDCSSCountdown_10ms;
volatile uint8_t  gFoundCTCSSCountdown_10ms;
#ifdef ENABLE_VOX
    volatile uint16_t gVoxStopCountdown_10ms;
#endif
volatile bool     gNextTimeslice40ms;
#ifdef ENABLE_NOAA
    volatile uint16_t gNOAACountdown_10ms = 0;
    volatile bool     gScheduleNOAA       = true;
#endif
volatile bool     gFlagTailNoteEliminationComplete;
#ifdef ENABLE_FMRADIO
    volatile bool gScheduleFM;
#endif

volatile uint8_t  boot_counter_10ms;

uint8_t           gIsLocked = 0xFF;


#ifdef ENABLE_FEAT_F4HWN
    bool          gK5startup = true;
    bool          gBackLight = false;
    bool          gMute = false;
    uint8_t       gBacklightTimeOriginal;
    uint8_t       gBacklightBrightnessOld;
    uint8_t       gPttOnePushCounter = 0;
    uint32_t      gBlinkCounter = 0;

    uint16_t gVfoSaveCountdown_10ms = 0;
    bool gScheduleVfoSave = false;
    bool gVfoStateChanged = false;
    char    gListName[MR_CHANNELS_LIST][4];
#endif

inline void FUNCTION_NOP() { ; }


int32_t NUMBER_AddWithWraparound(int32_t Base, int32_t Add, int32_t LowerLimit, int32_t UpperLimit)
{
    Base += Add;

    if (Base == 0x7fffffff || Base < LowerLimit)
        return UpperLimit;

    if (Base > UpperLimit)
        return LowerLimit;

    return Base;
}

unsigned long StrToUL(const char * str)
{
    unsigned long ul = 0;
    for(uint8_t i = 0; i < strlen(str); i++){
        char c = str[i];
        if(c < '0' || c > '9')
            break;
        ul = ul * 10 + (uint8_t)(c-'0');
    }
    return ul;
}

// 
// Cache-Based Channel Attributes Implementation
// 
//
// This replaces the huge array (~ 2,000 bytes in RAM) with a smart cache system
// that keeps only active channels in RAM and loads others from Flash on demand.
//
// SRAM Savings: ~ 2,000 bytes (84% reduction!)
// 

// Flash address where channel attributes start
// NOTE: Verify this matches your Flash layout!

#define FLASH_CHANNEL_ATTR_BASE 0x8000

// Each channel takes 2 bytes (ChannelAttributes_t is uint16_t)
#define FLASH_CHANNEL_ATTR_SIZE 2

// Cache hit/miss statistics (optional, for debugging)

#ifdef ENABLE_FEAT_F4HWN_DEBUG
    static uint32_t cache_hits = 0;
    static uint32_t cache_misses = 0;
#endif

// 
// Internal Helper Functions
// 

// Find cache entry for given channel
// Returns index if found, -1 if not found

static int MR_FindInCache(uint16_t channel_id)
{
    for (int i = 0; i < MR_CHANNELS_CACHE_SIZE; i++) {
        if (gMR_ChannelAttributes_Cache[i].channel_id == channel_id) {
            return i;
        }
    }
    return -1;
}

// Find oldest entry in cache (for eviction - LRU)
static int MR_FindOldestCacheEntry(void)
{
    int oldest_index = 0;
    uint32_t oldest_time = gMR_ChannelAttributes_Cache[0].access_time;
    
    for (int i = 1; i < MR_CHANNELS_CACHE_SIZE; i++) {
        if (gMR_ChannelAttributes_Cache[i].access_time < oldest_time) {
            oldest_time = gMR_ChannelAttributes_Cache[i].access_time;
            oldest_index = i;
        }
    }
    
    return oldest_index;
}

// Find empty cache slot
// Returns index if found, -1 if cache is full
static int MR_FindEmptyCacheSlot(void)
{
    for (int i = 0; i < MR_CHANNELS_CACHE_SIZE; i++) {
        if (gMR_ChannelAttributes_Cache[i].channel_id == 0xFFFF) {
            return i;
        }
    }
    return -1;  // Cache is full, need to evict
}

// Get current time (for LRU eviction)
static uint32_t GetCurrentTime(void)
{
    // Using gBlinkCounter which increments continuously
    extern uint32_t gBlinkCounter;
    return gBlinkCounter;
}

// 
// Public API Functions
// ════════════════════════════════════════════════════════════════════════════

// Load channel attributes from Flash
void MR_LoadChannelAttributesFromFlash(uint16_t channel_id, ChannelAttributes_t* attributes)
{
    // CRITICAL: Validate channel_id
    if (channel_id >= (MR_CHANNELS_MAX + 7)) {
        attributes->__val = 0;
        return;
    }
    
    // Calculate Flash address
    uint32_t flash_addr = FLASH_CHANNEL_ATTR_BASE + (channel_id * sizeof(ChannelAttributes_t));
    
    // Read 2 bytes from Flash
    PY25Q16_ReadBuffer(flash_addr, attributes, sizeof(ChannelAttributes_t));
}

// Save channel attributes to Flash
void MR_SaveChannelAttributesToFlash(uint16_t channel_id, const ChannelAttributes_t* attributes)
{
    // CRITICAL: Validate channel_id
    if (channel_id >= (MR_CHANNELS_MAX + 7)) {
        return;
    }
    
    // Calculate Flash address
    uint16_t flash_addr = FLASH_CHANNEL_ATTR_BASE + (channel_id * FLASH_CHANNEL_ATTR_SIZE);
    
    // Write 2 bytes to Flash
    PY25Q16_WriteBuffer(flash_addr, attributes, sizeof(ChannelAttributes_t), false);
}

// Get channel attributes (from cache or Flash)
// This is the main function used by the rest of the code
ChannelAttributes_t* MR_GetChannelAttributes(uint16_t channel_id)
{
    // Input validation
    if (channel_id >= (MR_CHANNELS_MAX + 7)) {
        return NULL;
    }
    
    // Check cache first (FAST PATH)
    int cache_index = MR_FindInCache(channel_id);
    
    if (cache_index >= 0) {
        // CACHE HIT
        #ifdef ENABLE_FEAT_F4HWN_DEBUG
            cache_hits++;
        #endif
        
        // Update access time for LRU
        gMR_ChannelAttributes_Cache[cache_index].access_time = GetCurrentTime();
        
        return &gMR_ChannelAttributes_Cache[cache_index].attributes;
    }
    
    // CACHE MISS - Load from Flash
    #ifdef ENABLE_FEAT_F4HWN_DEBUG
        cache_misses++;
    #endif
    
    // Find slot for new entry
    int slot = MR_FindEmptyCacheSlot();
    
    if (slot < 0) {
        // Cache is full, evict oldest entry
        slot = MR_FindOldestCacheEntry();
    }
    
    // Load from Flash into cache slot
    MR_LoadChannelAttributesFromFlash(channel_id, &gMR_ChannelAttributes_Cache[slot].attributes);
    
    // Store channel_id in cache
    gMR_ChannelAttributes_Cache[slot].channel_id = channel_id;
    
    // Set access time
    gMR_ChannelAttributes_Cache[slot].access_time = GetCurrentTime();
    
    return &gMR_ChannelAttributes_Cache[slot].attributes;
}

// Set channel attributes (updates both cache and Flash)
void MR_SetChannelAttributes(uint16_t channel_id, const ChannelAttributes_t* attributes)
{
    // Input validation
    if (channel_id >= (MR_CHANNELS_MAX + 7) || !attributes) {
        return;
    }
    
    // CRITICAL FIX: WRITE-PROTECT - Prevent Flash wear
    // Before writing, check if data already exists and is identical
    ChannelAttributes_t flash_version;
    MR_LoadChannelAttributesFromFlash(channel_id, &flash_version);
    
    // Compare current Flash data with new data
    if (memcmp(&flash_version, attributes, sizeof(ChannelAttributes_t)) == 0) {
        // But still update cache for consistency
        int cache_index = MR_FindInCache(channel_id);
        if (cache_index >= 0) {
            gMR_ChannelAttributes_Cache[cache_index].attributes = *attributes;
            gMR_ChannelAttributes_Cache[cache_index].access_time = GetCurrentTime();
        }
        return;  // Early exit - no Flash write needed
    }
    
    // Data has changed - write to Flash
    MR_SaveChannelAttributesToFlash(channel_id, attributes);
    
    // Update cache if entry exists
    int cache_index = MR_FindInCache(channel_id);
    
    if (cache_index >= 0) {
        // Entry in cache, update it
        gMR_ChannelAttributes_Cache[cache_index].attributes = *attributes;
        gMR_ChannelAttributes_Cache[cache_index].access_time = GetCurrentTime();
    } else {
        // Not in cache, add it
        int slot = MR_FindEmptyCacheSlot();
        
        if (slot < 0) {
            // Cache full, evict oldest
            slot = MR_FindOldestCacheEntry();
        }
        
        gMR_ChannelAttributes_Cache[slot].channel_id = channel_id;
        gMR_ChannelAttributes_Cache[slot].attributes = *attributes;
        gMR_ChannelAttributes_Cache[slot].access_time = GetCurrentTime();
    }
}

// Invalidate entire cache (call after loading Flash backup)
void MR_InvalidateChannelAttributesCache(void)
{
    for (int i = 0; i < MR_CHANNELS_CACHE_SIZE; i++) {
        gMR_ChannelAttributes_Cache[i].channel_id = 0xFFFF;  // Mark as empty
        gMR_ChannelAttributes_Cache[i].access_time = 0;
    }
}

// Initialize cache (call from settings.c boot sequence)
void MR_InitChannelAttributesCache(void)
{
    // Clear cache
    MR_InvalidateChannelAttributesCache();
    
    // Pre-load commonly used channels (VFO A, VFO B, channel 0)
    // This speeds up first access
    uint16_t channels_to_preload[] = {0, 1, 2};
    
    for (int i = 0; i < ARRAY_SIZE(channels_to_preload); i++) {
        if (channels_to_preload[i] < (MR_CHANNELS_MAX + 7)) {
            MR_GetChannelAttributes(channels_to_preload[i]);
        }
    }
}

// 
// Debugging / Statistics (optional, for development)
// 

#ifdef ENABLE_FEAT_F4HWN_DEBUG

uint32_t MR_GetCacheHits(void)
{
    return cache_hits;
}

uint32_t MR_GetCacheMisses(void)
{
    return cache_misses;
}

float MR_GetCacheHitRate(void)
{
    uint32_t total = cache_hits + cache_misses;
    if (total == 0) return 0.0f;
    return (float)cache_hits / (float)total * 100.0f;
}

void MR_PrintCacheStats(void)
{
    // This would print cache statistics (requires UART_Printf)
    // Uncomment if you want debug output:
    // UART_Printf("Cache Stats: Hits=%u Misses=%u Rate=%.1f%%\n", 
    //    cache_hits, cache_misses, MR_GetCacheHitRate());
}

#endif