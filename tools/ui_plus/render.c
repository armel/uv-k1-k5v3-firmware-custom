// Host-only fixture: runs the production UI_DisplayMain and font renderers.
// Radio I/O is mocked; this program never accesses a radio or its settings.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "settings.h"
#include "misc.h"
#include "functions.h"
#include "driver/st7565.h"
#include "ui/main.h"
#include "ui/helper.h"
#include "ui/ui_plus.h"
#include "ui/inputbox.h"
#include "ui/ui.h"
#include "app/app.h"
#include "app/chFrScanner.h"
#include "app/dtmf.h"
#include "app/action.h"
#include "driver/bk4819.h"

EEPROM_Config_t gEeprom;
FUNCTION_Type_t gCurrentFunction;
uint8_t gFrameBuffer[FRAME_LINES][LCD_WIDTH];
uint8_t gStatusLine[LCD_WIDTH];
GUI_DisplayType_t gScreenToDisplay;
bool gLowBattery, gLowBatteryConfirmed;
int8_t gScanStateDir;
uint32_t gScanRangeStart, gScanRangeStop;
DCS_CodeType_t gScanRangeCssType;
uint8_t gScanRangeCssCode = 0xff;
VFO_Info_t *gRxVfo, *gTxVfo;
uint8_t gActionPickerKey;
uint8_t gActionPickerSelection[2];
bool gDTMF_InputMode;
char gDTMF_InputBox[15];
char gDTMF_RX_live[20];
const char gModulationStr[3][4] = {"FM", "AM", "USB"};
const char *const gSubMenu_SET_AUD_AM[] = {"SHARP", "STOCK", "OPEN"};
const char *const gSubMenu_SET_AUD_FM[] = {"FLAT", "CLEAN", "MID", "BOOST", "MAX"};
const char *const gSubMenu_SIDEFUNCTIONS[] = {"NONE"};

void ST7565_BlitFullScreen(void) {}
void ST7565_BlitLine(unsigned line) {}
void ST7565_DrawLine(unsigned a, unsigned b, const uint8_t *c, unsigned d) {}
bool FUNCTION_IsRx(void) {
    return gCurrentFunction == FUNCTION_MONITOR ||
           gCurrentFunction == FUNCTION_INCOMING || gCurrentFunction == FUNCTION_RECEIVE;
}
bool APP_IsScreenSaverDisplayed(void) { return false; }
bool ACTION_IsAvailable(uint8_t a) { return true; }
void UI_FormatFrequency(uint32_t f, char *s) { snprintf(s,16,"%03u.%05u",f/100000,f%100000); }
static unsigned scenario;
void SETTINGS_FetchChannelName(char *s, uint16_t ch) {
    strcpy(s, scenario == 3 ? "" : scenario == 4 ? "jgy_pq~ABC" :
              ch == 0 ? "LOCAL TEST" : "ABCDEFGHIJ");
}
void PY25Q16_ReadBuffer(uint32_t a, void *s, uint32_t n) { memset(s,0,n); }
int32_t TX_freq_check(uint32_t f) { return -1; }
VFO_Info_t *APP_GetFullWatchDisplayVfo(uint8_t n) { return NULL; }
VFO_Info_t *const *APP_GetFullWatchBackgroundVfos(uint8_t *count) {
    static VFO_Info_t *background[1];
    background[0] = &gEeprom.VfoInfo[1];
    *count = scenario == 14 ? 1 : 0;
    return background;
}
uint8_t APP_GetFullWatchScrollPhase(void) { return 0; }
bool CHFRSCANNER_HasScanRssiSparkline(void) { return scenario == 18; }
uint8_t CHFRSCANNER_GetScanRssiSparklineLevel(uint8_t i) { return i % 6 + 1; }
const VFO_Info_t *CHFRSCANNER_GetScanDisplayVfo(void) { return NULL; }

VfoState_t VfoState[2];
static int16_t receivedStrength = -93;
int16_t BK4819_GetRSSI_dBm(void) { return receivedStrength; }
void DisplayRSSIBar(bool now);
static uint16_t voiceAmplitude;
uint16_t BK4819_GetVoiceAmplitudeOut(void) { return voiceAmplitude; }
void BK4819_ToggleGpioOut(BK4819_GPIO_PIN_t pin, bool set) {}
bool CHFRSCANNER_HasScanRangeExcludedOrdinal(uint32_t a, uint32_t b) { return false; }
bool RADIO_IsChannelInScanList(uint8_t a, uint8_t b) { return true; }

int main(int argc, char **argv) {
    scenario = argc > 7 ? atoi(argv[7]) : 0;
    gSetting_set_gui = argc > 1 ? atoi(argv[1]) : 1;
    gEeprom.DUAL_WATCH = argc > 2 ? atoi(argv[2]) : DUAL_WATCH_CHAN_A;
    gEeprom.CHANNEL_DISPLAY_MODE = argc > 3 ? atoi(argv[3]) : MDF_NAME_FREQ;
    gEeprom.TX_VFO = argc > 4 ? atoi(argv[4]) : 0;
    gCurrentFunction = argc > 5 ? atoi(argv[5]) : FUNCTION_FOREGROUND;
    gEeprom.RX_VFO = gEeprom.TX_VFO;
    gMonitor = argc > 6 ? atoi(argv[6]) : 0;
    gSetting_set_met = argc > 10 ? atoi(argv[10]) : 0;
    gEeprom.SQUELCH_LEVEL = 2;
    for (unsigned i=0; i<2; i++) {
        VFO_Info_t *v = &gEeprom.VfoInfo[i];
        gEeprom.ScreenChannel[i] = i ? 1023 : 0;
        v->CHANNEL_SAVE = gEeprom.ScreenChannel[i];
        v->pRX = &v->freq_config_RX; v->pTX = &v->freq_config_TX;
        v->freq_config_RX.Frequency = i ? 109012345 : 14652000;
        v->freq_config_TX.Frequency = v->freq_config_RX.Frequency + 60000;
        v->freq_config_RX.CodeType = v->freq_config_TX.CodeType = CODE_TYPE_CONTINUOUS_TONE;
        v->OUTPUT_POWER = i ? OUTPUT_POWER_USER : OUTPUT_POWER_HIGH;
        v->TX_OFFSET_FREQUENCY_DIRECTION = TX_OFFSET_FREQUENCY_DIRECTION_ADD;
        v->FrequencyReverse = i;
        v->CHANNEL_BANDWIDTH = i ? BANDWIDTH_NARROW : BANDWIDTH_WIDE;
        v->StepFrequency = 1250;
        v->TX_LOCK = true;
    }
    gSetting_set_nfm = 1; gSetting_set_pwr = 6;
    gRxVfo = &gEeprom.VfoInfo[gEeprom.RX_VFO]; gTxVfo = &gEeprom.VfoInfo[gEeprom.TX_VFO];
    // Optional VFO override exercises the same scenarios without memory mode.
    if (argc > 14 && atoi(argv[14])) {
        for (unsigned i = 0; i < 2; ++i) {
            if (i != gEeprom.TX_VFO && atoi(argv[14]) != 2)
                continue;
            gEeprom.ScreenChannel[i] = FREQ_CHANNEL_FIRST + (i ? 6 : 2);
            gEeprom.VfoInfo[i].CHANNEL_SAVE = gEeprom.ScreenChannel[i];
        }
        gNextMrChannel = 0xffff; // Frequency scan, not a memory scan.
    }
    if (scenario == 1 || scenario == 2) {
        memset(gInputBox, 10, sizeof(gInputBox));
        const char digits[] = {1, 0, 2, 4};
        gInputBoxIndex = scenario == 1 ? 1 : 4;
        memcpy(gInputBox, digits, gInputBoxIndex);
    }
    if (scenario == 5)
        gEeprom.ScreenChannel[1] = FREQ_CHANNEL_FIRST;
    if (scenario == 19)
        gEeprom.ScreenChannel[0] = FREQ_CHANNEL_FIRST;
    if (scenario == 6)
        VfoState[gEeprom.TX_VFO] = VFO_STATE_TX_DISABLE;
    if (scenario == 7 || scenario == 18) {
        gScanStateDir = 1;
        gEeprom.VfoInfo[1].freq_config_RX.Frequency = 44600000;
    }
    if (scenario >= 8 && scenario <= 10) {
        for (unsigned i=0; i<2; i++) {
            VFO_Info_t *v = &gEeprom.VfoInfo[i];
            if (scenario == 8)
                v->freq_config_RX.CodeType = v->freq_config_TX.CodeType = CODE_TYPE_REVERSE_DIGITAL;
            else
                v->Modulation = scenario == 9 ? MODULATION_AM : MODULATION_USB;
        }
    }
    if (scenario == 11) {
        gEeprom.KEY_LOCK = true;
        gKeypadLocked = 2;
    }
    if (scenario == 12) {
        gSetting_live_DTMF_decoder = true;
        strcpy(gDTMF_RX_live, "123456");
    }
    if (scenario == 13)
        gSetting_mic_bar = true;
    if (scenario == 14)
        gEeprom.DUAL_WATCH = DUAL_WATCH_FULL;
    if (scenario == 16)
        gSetting_set_audio_fm = 1; // Longest FM receive label: CLEAN.
    if (scenario == 17)
        gRxVfo->Modulation = MODULATION_AM; // Longest AM receive label: SHARP.
    if (scenario == 20) {
        gRxVfo->freq_config_TX.Frequency = gRxVfo->freq_config_RX.Frequency;
        gRxVfo->TX_OFFSET_FREQUENCY_DIRECTION = 0;
    }
    if (scenario == 21) {
        gScanStateDir = 1;
        UI_MAIN_NotifyScanListChanged();
    }
    if (scenario == 22) {
        gDTMF_InputMode = true;
        strcpy(gDTMF_InputBox, "12345");
    }
    if (scenario == 23) {
        gRxVfo->freq_config_RX.CodeType = CODE_TYPE_OFF;
        gRxVfo->freq_config_TX.CodeType = CODE_TYPE_OFF;
    }
    if (scenario == 24) {
        gScanStateDir = 1;
        gEeprom.SCAN_LIST_ENABLED = true;
        gEeprom.SCANLIST_PRIORITY_CH[0] = 0;
        gEeprom.SCANLIST_PRIORITY_CH[1] = 1;
    }
    if (scenario == 25 || scenario == 26) {
        gScanStateDir = 1;
        gScanRangeStart = 14500000;
        gScanRangeStop = 14700000;
        if (scenario == 26) {
            gScanRangeCssType = CODE_TYPE_CONTINUOUS_TONE;
            gScanRangeCssCode = 0;
        }
    }
    if (scenario == 27)
        MR_GetChannelAttributes(gEeprom.ScreenChannel[gEeprom.TX_VFO])->compander = true;
    if (scenario == 15 && gCurrentFunction == FUNCTION_FOREGROUND) {
        gCurrentFunction = FUNCTION_RECEIVE;
        UI_DisplayMain();
        gCurrentFunction = FUNCTION_FOREGROUND; // Exercise the last-received >> marker.
    }
    // Optional calibrated dBm input for exact S-meter threshold checks.
    if (argc > 12)
        receivedStrength = atoi(argv[12]) - dBmCorrTable[gRxVfo->Band];
    UI_DisplayMain();
    // Exercise the real periodically refreshed TX renderers, including decay
    // after a peak. Each refresh must leave every other LCD page intact.
    const unsigned frames = argc > 8 ? atoi(argv[8]) : 0;
    const unsigned meter = argc > 9 ? atoi(argv[9]) : 0;
    const uint16_t amplitudes[] = {0, 200, 250, 500, 2000, 65535, 250, 200};
    const unsigned meterLine = gEeprom.DUAL_WATCH ? 3 : 5;
    for (unsigned frame = 0; frame < frames; frame++) {
        uint8_t previous[sizeof(gFrameBuffer)];
        memcpy(previous, gFrameBuffer, sizeof(previous));
        voiceAmplitude = amplitudes[frame % (sizeof(amplitudes) / sizeof(amplitudes[0]))];
        receivedStrength = argc > 12 ? atoi(argv[12]) - dBmCorrTable[gRxVfo->Band] :
                                      -160 + (frame % 28) * 5;
        if (meter == 1)
            UI_DisplayAudioBar();
        else if (meter == 2)
            UI_DisplayAudioScopeOverlay(meterLine, true);
        else if (meter == 3)
            DisplayRSSIBar(true);
        else
            UI_DisplayAudioScope();
        for (unsigned byte = 0; byte < sizeof(previous); byte++)
            if (byte / LCD_WIDTH != meterLine &&
                !(gSetting_set_gui && !gEeprom.DUAL_WATCH && meter != 2 && byte / LCD_WIDTH == 4) &&
                !(meter == 3 && byte % LCD_WIDTH < 7))
                assert(((uint8_t *)gFrameBuffer)[byte] == previous[byte]);
    }
    if (argc > 11 && atoi(argv[11])) {
        gCurrentFunction = FUNCTION_FOREGROUND;
        UI_DisplayAudioScope(); // The periodic callback resets its TX session.
        UI_DisplayMain();
    }
    fwrite(gFrameBuffer,1,sizeof(gFrameBuffer),stdout);
}
