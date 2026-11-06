// Exercise the production scanner with mocked RF I/O and channel storage.
#include <assert.h>
#include <stdio.h>
#include "../../App/app/chFrScanner.c"
#include "../../App/ui/ui.c"

bool gIsInSubMenu;
uint8_t gInputBoxIndex;
void DTMF_clear_input_box(void) {}
void HideFKeyIcon(void) {}

EEPROM_Config_t gEeprom;
FUNCTION_Type_t gCurrentFunction;
VFO_Info_t *gRxVfo = &gEeprom.VfoInfo[0];
VFO_Info_t *gTxVfo = &gEeprom.VfoInfo[0];
static bool valid[MR_CHANNELS_MAX];
static uint32_t now;
static unsigned rfTunes, notices, audioOff;

uint32_t SCHEDULER_GetTick10ms(void) { return now; }
#ifdef ENABLE_FEAT_F4HWN_SCAN_PROGRESS
void UI_MAIN_NotifyScanProgressDataChanged(void) { ++notices; }
void UI_MAIN_NotifyScanListChanged(void) {}
#endif
bool FUNCTION_IsRx(void) { return gCurrentFunction == FUNCTION_RECEIVE || gCurrentFunction == FUNCTION_INCOMING; }
void FUNCTION_Select(FUNCTION_Type_t f) { gCurrentFunction = f; }
void FUNCTION_Init(void) {}
void GPIO_DisableAudioPath(void) { ++audioOff; }
void RADIO_SelectVfos(void) {}
void RADIO_SetupRegisters(bool foreground) { ++rfTunes; if (foreground) gCurrentFunction = FUNCTION_FOREGROUND; }
void RADIO_ConfigureChannel(unsigned vfo, unsigned mode) {
    (void)mode;
    gRxVfo->CHANNEL_SAVE = gEeprom.MrChannel[vfo];
}
void RADIO_ConfigureSquelchAndOutputPower(VFO_Info_t *v) { (void)v; }
void RADIO_ApplyOffset(VFO_Info_t *v) { (void)v; }
void SETTINGS_SaveVfoIndices(void) {}
void SETTINGS_SaveChannel(uint16_t c, uint8_t v, const VFO_Info_t *i, uint8_t m) { (void)c; (void)v; (void)i; (void)m; }
uint32_t APP_SetFrequencyByStep(VFO_Info_t *v, int8_t d) { return v->freq_config_RX.Frequency + d * v->StepFrequency; }
void APP_StartListening(FUNCTION_Type_t f) { gCurrentFunction = f; }
bool RADIO_CheckValidList(uint8_t list) { (void)list; return true; }
void RADIO_NextValidList(int8_t direction) { (void)direction; }
bool RADIO_CheckValidChannel(uint16_t c, bool checkList, uint8_t list) {
    (void)checkList; (void)list;
    return IS_MR_CHANNEL(c) && valid[c];
}
uint16_t RADIO_FindNextChannel(uint16_t c, int8_t direction, bool checkList, uint8_t list) {
    for (unsigned i = 0; i < MR_CHANNELS_MAX; ++i, c += direction) {
        if (c == UINT16_MAX) c = MR_CHANNEL_LAST;
        else if (!IS_MR_CHANNEL(c)) c = MR_CHANNEL_FIRST;
        if (RADIO_CheckValidChannel(c, checkList, list)) return c;
    }
    return UINT16_MAX;
}
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
bool SETTINGS_FetchChannelScanDisplayInfo(uint16_t c, ChannelScanDisplayInfo_t *info) {
    memset(info, 0, sizeof(*info));
    info->rx.Frequency = 14600000 + c * 1000;
    info->modulation = MODULATION_FM;
    return valid[c];
}
void SETTINGS_ApplyChannelScanDisplayInfo(VFO_Info_t *v, uint16_t c, const ChannelScanDisplayInfo_t *info) {
    v->CHANNEL_SAVE = c;
    v->freq_config_RX = info->rx;
}
uint8_t BK4819_GetGlitchIndicator(void) { return 0; }
uint16_t BK4819_GetRSSI(void) { return 200; }
void SYSTICK_DelayUs(uint32_t d) { (void)d; }
void BK4819_PickRXFilterPathBasedOnFrequency(uint32_t f) { (void)f; }
void BK4819_SetFrequency(uint32_t f) { (void)f; ++rfTunes; }
uint16_t BK4819_ReadRegister(BK4819_REGISTER_t r) { (void)r; return 0; }
void BK4819_WriteRegister(BK4819_REGISTER_t r, uint16_t v) { (void)r; (void)v; }
void RADIO_SetModulation(ModulationMode_t m) { (void)m; }
BK4819_FilterBandwidth_t RADIO_GetAMFilterBandwidth(const VFO_Info_t *v) { (void)v; return BK4819_FILTER_BW_WIDE; }
void BK4819_SetFilterBandwidth(BK4819_FilterBandwidth_t b, bool weak) { (void)b; (void)weak; }
#endif

static void setup(void) {
    memset(&gEeprom, 0, sizeof(gEeprom));
    memset(valid, 0, sizeof(valid));
    for (unsigned i = 0; i < 3; ++i) valid[i] = true;
    gEeprom.SCAN_LIST_DEFAULT = 1;
    gEeprom.SCANLIST_PRIORITY_CH[0] = MR_CHANNELS_MAX;
    gEeprom.SCANLIST_PRIORITY_CH[1] = MR_CHANNELS_MAX;
    gRxVfo->SquelchOpenRSSIThresh = 100;
    gRxVfo->pRX = &gRxVfo->freq_config_RX;
    now = 0;
    gScanSkipMode = SCAN_SKIP_SESSION;
    SCAN_SKIP_Reset(now);
    gCurrentFunction = FUNCTION_FOREGROUND;
}

static void scenarios(void) {
    setup();
    SCAN_SKIP_Add(1, now);
    CHFRSCANNER_Start(true, 1);
    assert(gNextMrChannel == 2 && !gScanMemoryBlocked);
    CHFRSCANNER_ManualResume(-1);
    assert(gNextMrChannel == 0 && SCAN_SKIP_Contains(1));
    // Pausing and resuming, and switching scan lists, retain the session.
    gScanPauseMode = true;
    gEeprom.SCAN_LIST_DEFAULT = 2;
    CHFRSCANNER_ManualResume(1);
    assert(SCAN_SKIP_Contains(1));
    CHFRSCANNER_Stop();
    assert(!SCAN_SKIP_Contains(1) && gScanStateDir == SCAN_OFF);

    setup();
    gEeprom.SCAN_LIST_ENABLED = true;
    gEeprom.SCANLIST_PRIORITY_CH[0] = 1;
    gEeprom.SCANLIST_PRIORITY_CH[1] = 2;
    SCAN_SKIP_Add(1, now);
    CHFRSCANNER_Start(true, 1);
    assert(gNextMrChannel == 2); // First priority skipped, second remains valid.
    SCAN_SKIP_Add(2, now);
    gCurrentFunction = FUNCTION_RECEIVE;
    unsigned beforeMute = audioOff;
    CHFRSCANNER_ManualResume(1);
    assert(audioOff == beforeMute + 1 && gCurrentFunction == FUNCTION_FOREGROUND);
    assert(gNextMrChannel == 0);
    SCAN_SKIP_Add(0, now);
    unsigned beforeTunes = rfTunes;
    CHFRSCANNER_ManualResume(1);
    assert(gScanMemoryBlocked && gScanStateDir != SCAN_OFF && rfTunes == beforeTunes);
    for (unsigned i = 0; i < 4; ++i) {
        NextMemChannel();
        assert(gScanMemoryBlocked && rfTunes == beforeTunes);
    }
    assert(SCAN_SKIP_Remove(2));
    NextMemChannel();
    assert(!gScanMemoryBlocked && gNextMrChannel == 2); // Priority resumes.
    CHFRSCANNER_Stop();

    setup();
    gScanSkipMode = SCAN_SKIP_1_MIN;
    for (unsigned i = 0; i < 3; ++i) SCAN_SKIP_Add(i, now);
    CHFRSCANNER_Start(true, 1);
    assert(gScanMemoryBlocked);
    CHFRSCANNER_Stop();
    assert(SCAN_SKIP_Contains(1));
    now = 6000; // Expiration while stopped; Start updates elapsed time itself.
    CHFRSCANNER_Start(true, 1);
    assert(!gScanMemoryBlocked && !SCAN_SKIP_Contains(1));
    CHFRSCANNER_Stop();

    setup();
    SCAN_SKIP_Add(1, now);
    gScanSkipMode = SCAN_SKIP_5_MIN;
    SCAN_SKIP_Add(2, now);
    CHFRSCANNER_Start(true, 1);
    gScreenToDisplay = DISPLAY_MAIN;
    GUI_SelectNextDisplay(DISPLAY_MENU);
    assert(gScanStateDir == SCAN_OFF && !gScanMemoryBlocked);
    assert(!SCAN_SKIP_Contains(1) && SCAN_SKIP_Contains(2));

    setup();
    for (unsigned i = 0; i < 34; ++i) valid[i] = true;
    for (unsigned i = 0; i < 33; ++i) SCAN_SKIP_Add(i, now);
    assert(!SCAN_SKIP_Contains(0));
    CHFRSCANNER_Start(true, 1);
    assert(gNextMrChannel == 33); // Walk past all 32 skipped candidates.
    CHFRSCANNER_ManualResume(-1);
    assert(gNextMrChannel == 0);
    CHFRSCANNER_Stop();
}
int main(void) {
    scenarios();
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
    gSetting_set_scn = true;
    scenarios();
#endif
    puts("PASS: production scanner normal/priority selection, direction/list changes, RX abort, all-skipped wait/recovery, timed restart, screen-change cleanup, FIFO overflow.");
}
