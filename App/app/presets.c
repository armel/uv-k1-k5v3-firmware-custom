/* DualMode channel presets - PMR446, Emergency, Marine
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef ENABLE_FEAT_DUALMODE

#include <string.h>

#include "app/presets.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"

/* PMR446: 446.00625 + ch*12.5kHz, ch 0-15, FM Narrow, List 1 */
static const uint32_t pmr446_freqs[] = {
    44600625, 44601875, 44603125, 44604375, 44605625, 44606875, 44608125, 44609375,
    44610625, 44611875, 44613125, 44614375, 44615625, 44616875, 44618125, 44619375
};

/* Emergency+Marine: ch 16-27, List 2. freq, mod(0=FM,1=AM), bw(0=W,1=N), name */
struct preset_ch {
    uint32_t freq;
    uint8_t  mod;   /* 0=FM, 1=AM */
    uint8_t  bw;    /* 0=Wide, 1=Narrow */
    const char name[11];
};
static const struct preset_ch emergency_ch[] = {
    {12150000, 1, 0, "AIR-SOS"},
    {12345000, 1, 0, "AIR-A2A"},
    {15652500, 0, 0, "MAR-DSC"},
    {15680000, 0, 0, "MAR-CH16"},
    {15630000, 0, 0, "MAR-CH06"},
    {15665000, 0, 0, "MAR-CH13"},
    {24300000, 1, 0, "MIL-SOS"},
    {14550000, 0, 1, "HAM-2M"},
    {43350000, 0, 1, "HAM-70CM"},
    {14580000, 0, 1, "ISS-DN"},
    {13710000, 0, 0, "NOAA-19"},
    {40300000, 0, 1, "SONDE"},
};

/* Find first empty channel in List 3 (ch 28-77), return 0xFFFF if full */
uint16_t PRESETS_FindEmptyList3Channel(void)
{
    for (uint16_t ch = 28; ch <= 77; ch++) {
        ChannelAttributes_t *att = MR_GetChannelAttributes(ch);
        if (att && att->scanlist != 3)
            return ch;
    }
    return 0xFFFF;
}

/* Save frequency to List 3. freqHz=0 uses gRxVfo->pRX->Frequency. Returns true if saved. */
bool PRESETS_QuickBookmark(uint32_t freqHz)
{
    uint16_t ch = PRESETS_FindEmptyList3Channel();
    if (ch == 0xFFFF)
        return false;
    uint32_t f = freqHz ? freqHz : gRxVfo->pRX->Frequency;
    VFO_Info_t vfo;
    RADIO_InitInfo(&vfo, ch, f);
    vfo.SCANLIST_PARTICIPATION = 3;
    vfo.Modulation = gRxVfo->Modulation;
    vfo.CHANNEL_BANDWIDTH = gRxVfo->CHANNEL_BANDWIDTH;
    SETTINGS_SaveChannel(ch, gEeprom.RX_VFO, &vfo, 2);
    return true;
}

void PRESETS_LoadDualModeChannels(void)
{
    VFO_Info_t vfo;
    char name[11];

    /* List 1: PMR446 ch 0-15 */
    for (int i = 0; i < 16; i++) {
        RADIO_InitInfo(&vfo, (uint16_t)i, pmr446_freqs[i]);
        vfo.SCANLIST_PARTICIPATION = 1;
        vfo.Modulation = MODULATION_FM;
        vfo.CHANNEL_BANDWIDTH = BANDWIDTH_NARROW;
        SETTINGS_SaveChannel((uint16_t)i, 0, &vfo, 2);
        sprintf(name, "PMR-%02u", i + 1);
        SETTINGS_SaveChannelName((uint16_t)i, name);
    }

    /* List 2: Emergency+Marine ch 16-27 */
    for (int i = 0; i < 12; i++) {
        RADIO_InitInfo(&vfo, (uint16_t)(16 + i), emergency_ch[i].freq);
        vfo.SCANLIST_PARTICIPATION = 2;
        vfo.Modulation = emergency_ch[i].mod ? MODULATION_AM : MODULATION_FM;
        vfo.CHANNEL_BANDWIDTH = emergency_ch[i].bw;
        SETTINGS_SaveChannel((uint16_t)(16 + i), 0, &vfo, 2);
        SETTINGS_SaveChannelName((uint16_t)(16 + i), emergency_ch[i].name);
    }

    /* List 3: ch 28+ empty - user fills. VFO A/B init to PMR-01 and last freq */
    gEeprom.ScreenChannel[0] = MR_CHANNEL_FIRST;
    gEeprom.ScreenChannel[1] = MR_CHANNEL_FIRST + 16;
    gEeprom.MrChannel[0] = MR_CHANNEL_FIRST;
    gEeprom.MrChannel[1] = MR_CHANNEL_FIRST + 16;
    RADIO_InitInfo(&gEeprom.VfoInfo[0], MR_CHANNEL_FIRST, pmr446_freqs[0]);
    RADIO_InitInfo(&gEeprom.VfoInfo[1], MR_CHANNEL_FIRST + 16, 12150000);
    gEeprom.VfoInfo[0].SCANLIST_PARTICIPATION = 1;
    gEeprom.VfoInfo[0].Modulation = MODULATION_FM;
    gEeprom.VfoInfo[0].CHANNEL_BANDWIDTH = BANDWIDTH_NARROW;
    gEeprom.VfoInfo[1].SCANLIST_PARTICIPATION = 2;
    gEeprom.VfoInfo[1].Modulation = MODULATION_AM;
}

#endif
