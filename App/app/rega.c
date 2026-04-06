/*
 * REGA Alam and Test
 * ==================
 *
 * This bit of code is to implement the REGA test and alarm functions.
 * This function sends out a fixed ZVEI tone sequence for testing or alarm purposes.
 * Further information: https://www.rega.ch/en/our-missions/sites-and-infrastructure/emergency-radio
 *
 * There are two REGA ACTIONS: Test and Alarm.
 * The Test action sends out a fixed ZVEI tone sequence for testing purposes.
 * The Alarm action sends out a fixed ZVEI tone sequence for alarm purposes.
 * 
 * These actions can be assigned to a key in the settings menu.
 * 
 * The Test/Alarm function will perform the following actions:
 * - Set the radio to transmit mode
 * - Wait 100ms to allow the radio to switch to transmit mode and stabilize the tramsmitter
 * - Send out the ZVEI tone sequence
 * - Wait 100ms to allow the radio to finish transmitting
 * - Set the radio back to receive mode
 * 
 * The ZVEI tone squence for alarm is: 21414
 * The ZVEI tone squence for test is: 21301 
 *
 * To save space the two tone sequences are stored as precalculated register values.
 * This avoids the need to calculate the register values at runtime.
 * They are hardcoded as array of 32bit integers in the rega.h file.
 *
 * Copyright 2025 Markus Bärtschi
 * https://github.com/markusb
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
#include <stdio.h>
#include <stdbool.h>

#include "rega.h"
#include "radio.h"
#include "action.h"
#include "misc.h"
#include "settings.h"
#include "functions.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "driver/gpio.h"
#include "ui/helper.h"
#include "ui/ui.h"


const uint16_t rega_alarm_tones[5] = {
    1160,  // 2 1160Hz
    1060,  // 1 1060Hz
    1400,  // 4 1400Hz
    1060,  // 1 1060Hz
    1400,  // 4 1400Hz
};
const uint16_t rega_test_tones[5] = {
    1160, // 2 1160Hz
    1060, // 1 1060Hz
    1260, // 3 1260Hz
    2400, // 0 2400Hz
    1060, // 1 1060Hz
};

// Global variable to pass the message to the display
char rega_message[16];

// Transmit the ZVEI tone sequence for alarm
void ACTION_RegaAlarm()
{
    const char message[16] = "REGA Alarm";
    REGA_TransmitZvei(rega_alarm_tones,message);
}

// Tranmit the ZVEI tone sequence for test
void ACTION_RegaTest()
{
    const char message[16] = "REGA Test";
    REGA_TransmitZvei(rega_test_tones,message);
}

// Display the REGA message on the screen
void UI_DisplayREGA()
{
    UI_DisplayClear();
    UI_DisplayPopup(rega_message);
    ST7565_BlitFullScreen();
}

// Transmit a ZVEI tone sequence on the REGA frequency
// Configures the radio on VFO A with the required parameters
// tones: array of 5 ZVEI tones
// message: message to display on the screen
void REGA_TransmitZvei(const uint16_t tones[],const char message[])
{
    // Copy the message text
    strncpy(rega_message,message,16);
    // Trigger the display
    gScreenToDisplay = DISPLAY_REGA;

    // Flash the green LED twice
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
    SYSTEM_DelayMs(70);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    SYSTEM_DelayMs(50);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
    SYSTEM_DelayMs(70);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);

    // Select VFO A
    gEeprom.TX_VFO = 0;
    gEeprom.ScreenChannel[gEeprom.TX_VFO] = gEeprom.FreqChannel[gEeprom.TX_VFO];
    gRxVfo = gTxVfo;
    gRequestSaveVFO = true;
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;

    // Set the REGA frequency, no offset
    gTxVfo->freq_config_RX.Frequency = REGA_FREQUENCY;
    gTxVfo->freq_config_TX           = gTxVfo->freq_config_RX;
    gTxVfo->TX_OFFSET_FREQUENCY = 0;

    // Set the modulation to FM narrow
    gTxVfo->Modulation = MODULATION_FM;
    gTxVfo->CHANNEL_BANDWIDTH = BK4819_FILTER_BW_NARROW;

    // Set Tx Squelch Tone
    gTxVfo->freq_config_TX.CodeType = CODE_TYPE_CONTINUOUS_TONE;
    gTxVfo->freq_config_TX.Code     = REGA_CTCSS_FREQ_INDEX;
    BK4819_SetCTCSSFrequency(CTCSS_Options[gTxVfo->freq_config_TX.Code]);
    // Turn Rx squelch tone off
    gTxVfo->freq_config_RX.CodeType = CODE_TYPE_OFF;

    // Set the transmit power to high
    gTxVfo->OUTPUT_POWER = OUTPUT_POWER_HIGH;

    // Lock the keyboard
    gEeprom.KEY_LOCK = true;

    gRequestSaveChannel = 1;
    gRequestSaveSettings = true;

    // Configure the receiver
    BK4819_RX_TurnOn();

    // Set the radio to transmit mode
    RADIO_PrepareCssTX();
    FUNCTION_Select(FUNCTION_TRANSMIT);

    // Wait to allow the radio to switch to transmit mode and stabilize the transmitter
    SYSTEM_DelayMs(ZVEI_PRE_LENGTH_MS);

    // Send out the ZVEI2 tone sequence
    for (int i = 0; i < ZVEI_NUM_TONES; i++)
    {
        BK4819_PlaySingleTone(tones[i], ZVEI_TONE_LENGTH_MS, 100, true);
        SYSTEM_DelayMs(ZVEI_PAUSE_LENGTH_MS);
    }

    // Wait to allow the radio to finish transmitting
    SYSTEM_DelayMs(ZVEI_POST_LENGTH_MS);

    // Flash the green LED twice
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
    SYSTEM_DelayMs(70);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    SYSTEM_DelayMs(70);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
    SYSTEM_DelayMs(70);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);

    // Display the normal screen
    gRequestDisplayScreen = DISPLAY_MAIN;
}
