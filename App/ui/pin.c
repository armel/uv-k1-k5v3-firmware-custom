/* PIN entry UI for DualMode Blackout activation
 * 4-6 digits, MENU=OK, EXIT=cancel. 3 failed attempts = 30 sec lockout.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef ENABLE_FEAT_DUALMODE

#include <string.h>

#include "app/op_mode.h"
#include "audio.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "misc.h"
#include "ui/helper.h"
#include "ui/pin.h"

#define PIN_FAILED_LOCKOUT_10MS 3000  /* 30 sec */

static void Render(uint8_t pin[OP_MODE_PIN_LEN], uint8_t len, bool locked)
{
    char str[OP_MODE_PIN_LEN + 1];

    memset(gStatusLine, 0, sizeof(gStatusLine));
    UI_DisplayClear();

    if (locked) {
        UI_PrintString("Wait...", 0, 127, 1, 10);
    } else {
        UI_PrintString("Enter PIN:", 0, 127, 1, 10);
        for (unsigned int i = 0; i < OP_MODE_PIN_LEN; i++)
            str[i] = (i < len) ? ('0' + pin[i]) : '-';
        str[OP_MODE_PIN_LEN] = '\0';
        UI_PrintString(str, 0, 127, 3, 12);
    }

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

bool UI_DisplayPinEntry(void)
{
    KEY_Code_t Key;
    uint8_t pin[OP_MODE_PIN_LEN] = {0};
    uint8_t len = 0;
    uint8_t failedAttempts = 0;
    uint16_t lockoutCountdown = 0;

    gUpdateDisplay = true;

    while (1)
    {
        while (!gNextTimeslice) {}

        gNextTimeslice = false;

        if (lockoutCountdown > 0) {
            lockoutCountdown--;
            if (lockoutCountdown == 0) {
                failedAttempts = 0;
                gUpdateDisplay = true;
            }
        } else {
            Key = KEYBOARD_Poll();

            if (gKeyReading0 == Key)
            {
                if (++gDebounceCounter == key_debounce_10ms)
                {
                    if (Key == KEY_INVALID)
                        gKeyReading1 = KEY_INVALID;
                    else
                    {
                        gKeyReading1 = Key;

                        switch (Key)
                        {
                            case KEY_0:
                            case KEY_1:
                            case KEY_2:
                            case KEY_3:
                            case KEY_4:
                            case KEY_5:
                            case KEY_6:
                            case KEY_7:
                            case KEY_8:
                            case KEY_9:
                                if (len < OP_MODE_PIN_LEN)
                                {
                                    pin[len++] = (uint8_t)(Key - KEY_0);
                                    gUpdateDisplay = true;
                                    AUDIO_PlayBeep(BEEP_1KHZ_60MS_OPTIONAL);
                                }
                                break;

                            case KEY_MENU:
                                if (len >= OP_MODE_PIN_MIN && len <= OP_MODE_PIN_MAX)
                                {
                                    if (OpMode_VerifyPin(pin, len))
                                    {
                                        AUDIO_PlayBeep(BEEP_1KHZ_60MS_OPTIONAL);
                                        return true;
                                    }
                                    failedAttempts++;
                                    AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
                                    len = 0;
                                    if (failedAttempts >= 3) {
                                        lockoutCountdown = PIN_FAILED_LOCKOUT_10MS;
                                    }
                                    gUpdateDisplay = true;
                                }
                                break;

                            case KEY_EXIT:
                                AUDIO_PlayBeep(BEEP_1KHZ_60MS_OPTIONAL);
                                return false;

                            default:
                                break;
                        }
                    }
                    gKeyBeingHeld = false;
                }
            }
            else
            {
                gDebounceCounter = 0;
                gKeyReading0 = Key;
            }
        }

        if (gUpdateDisplay)
        {
            Render(pin, len, lockoutCountdown > 0);
            gUpdateDisplay = false;
        }
    }
}

bool UI_DisplaySetPin(void)
{
    KEY_Code_t Key;
    uint8_t pin[OP_MODE_PIN_LEN] = {0};
    uint8_t len = 0;
    uint8_t step = 0;  /* 0=enter new, 1=confirm */
    uint8_t firstPin[OP_MODE_PIN_LEN] = {0};
    uint8_t firstLen = 0;

    gUpdateDisplay = true;

    while (1)
    {
        while (!gNextTimeslice) {}

        gNextTimeslice = false;
        Key = KEYBOARD_Poll();

        if (gKeyReading0 == Key)
        {
            if (++gDebounceCounter == key_debounce_10ms)
            {
                if (Key == KEY_INVALID)
                    gKeyReading1 = KEY_INVALID;
                else
                {
                    gKeyReading1 = Key;

                    switch (Key)
                    {
                        case KEY_0:
                        case KEY_1:
                        case KEY_2:
                        case KEY_3:
                        case KEY_4:
                        case KEY_5:
                        case KEY_6:
                        case KEY_7:
                        case KEY_8:
                        case KEY_9:
                            if (len < OP_MODE_PIN_LEN)
                            {
                                pin[len++] = (uint8_t)(Key - KEY_0);
                                gUpdateDisplay = true;
                                AUDIO_PlayBeep(BEEP_1KHZ_60MS_OPTIONAL);
                            }
                            break;

                        case KEY_MENU:
                            if (len >= OP_MODE_PIN_MIN && len <= OP_MODE_PIN_MAX)
                            {
                                if (step == 0) {
                                    firstLen = len;
                                    for (uint8_t i = 0; i < len; i++)
                                        firstPin[i] = pin[i];
                                    len = 0;
                                    step = 1;
                                } else {
                                    if (len == firstLen) {
                                        bool match = true;
                                        for (uint8_t i = 0; i < len && match; i++)
                                            if (pin[i] != firstPin[i]) match = false;
                                        if (match) {
                                            OpMode_SetPin(pin, len);
                                            AUDIO_PlayBeep(BEEP_1KHZ_60MS_OPTIONAL);
                                            return true;
                                        }
                                    }
                                    AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
                                    len = 0;
                                    step = 0;
                                }
                                gUpdateDisplay = true;
                            }
                            break;

                        case KEY_EXIT:
                            AUDIO_PlayBeep(BEEP_1KHZ_60MS_OPTIONAL);
                            return false;

                        default:
                            break;
                    }
                }
                gKeyBeingHeld = false;
            }
        }
        else
        {
            gDebounceCounter = 0;
            gKeyReading0 = Key;
        }

        if (gUpdateDisplay)
        {
            memset(gStatusLine, 0, sizeof(gStatusLine));
            UI_DisplayClear();
            if (step == 0)
                UI_PrintString("New PIN:", 0, 127, 1, 10);
            else
                UI_PrintString("Confirm:", 0, 127, 1, 10);
            {
                char str[OP_MODE_PIN_LEN + 1];
                for (unsigned int i = 0; i < OP_MODE_PIN_LEN; i++)
                    str[i] = (i < len) ? ('0' + pin[i]) : '-';
                str[OP_MODE_PIN_LEN] = '\0';
                UI_PrintString(str, 0, 127, 3, 12);
            }
            ST7565_BlitStatusLine();
            ST7565_BlitFullScreen();
            gUpdateDisplay = false;
        }
    }
}

#endif /* ENABLE_FEAT_DUALMODE */
