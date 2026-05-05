/* Copyright 2025 muzkr https://github.com/muzkr
 * Copyright 2023 Manuel Jinger
 * Copyright 2023 Dual Tachyon
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

#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/systick.h"
#include "driver/i2c.h"
#include "misc.h"

KEY_Code_t gKeyReading0     = KEY_INVALID;
KEY_Code_t gKeyReading1     = KEY_INVALID;
uint16_t   gDebounceCounter = 0;
bool       gWasFKeyPressed  = false;

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
// Short press: hold key for SERIAL_KEY_SHORT_POLLS calls.
// Must exceed key_debounce_10ms (2) to trigger ProcessKey(key, true, false).
#define SERIAL_KEY_SHORT_POLLS  5

// Long press: hold key for SERIAL_KEY_LONG_POLLS calls.
// Must exceed key_repeat_delay_10ms (40) to trigger ProcessKey(key, true, true).
#define SERIAL_KEY_LONG_POLLS   45

// Packet types for serial key injection (K5Viewer → radio)
#define SERIAL_KEY_TYPE         0x03
#define SERIAL_KEY_TYPE_LONG    0x04

volatile KEY_Code_t gKeyFromSerial      = KEY_INVALID;
static   uint8_t    gSerialKeyHoldCount = 0;
static   uint8_t    gSerialKeyLong      = 0;  // 0 = short press, 1 = long press

// Inject a short or long press from serial (UART or VCP).
// KEY_PTT is explicitly blocked — PTT release cannot be guaranteed over serial.
static inline void KEYBOARD_InjectKey(uint8_t keyCode, bool keyLong)
{
    if (keyCode < KEY_INVALID && keyCode != KEY_PTT) {
        gKeyFromSerial      = (KEY_Code_t)keyCode;
        gSerialKeyHoldCount = 0;
        gSerialKeyLong      = keyLong;
    }
}

bool KEYBOARD_ProcessProtocolByte(ParseState_t *state, uint8_t b)
{
    bool connected = false;

    switch (*state)
    {
        case STATE_IDLE:
            if      (b == 0x55) *state = STATE_KA_1;
            else if (b == 0xAA) *state = STATE_KEY_1;
            break;
            
        case STATE_KA_1:
            *state = (b == 0xAA) ? STATE_KA_2 : STATE_IDLE;
            break;
            
        case STATE_KA_2:
            *state = (b == 0x00) ? STATE_KA_3 : STATE_IDLE;
            break;
            
        case STATE_KA_3:
            if (b == 0x00) connected = true;
            *state = STATE_IDLE;
            break;
            
        case STATE_KEY_1:
            *state = (b == 0x55) ? STATE_KEY_2 : STATE_IDLE;
            break;
            
        case STATE_KEY_2:
            if      (b == SERIAL_KEY_TYPE)      *state = STATE_KEY_3;
            else if (b == SERIAL_KEY_TYPE_LONG) *state = STATE_KEY_3L;
            else                                *state = STATE_IDLE;
            break;
            
        case STATE_KEY_3:
        case STATE_KEY_3L:
            KEYBOARD_InjectKey(b, *state == STATE_KEY_3L);
            connected = true;
            *state = STATE_IDLE;
            break;

        default:
            *state = STATE_IDLE;
            break;
    }

    return connected;
}
#endif

#define GPIOx               GPIOB
#define PIN_MASK_COLS       (LL_GPIO_PIN_6 | LL_GPIO_PIN_5 | LL_GPIO_PIN_4 | LL_GPIO_PIN_3)
#define PIN_COLS            GPIO_MAKE_PIN(GPIOx, PIN_MASK_COLS)
#define PIN_COL(n)          GPIO_MAKE_PIN(GPIOx, 1u << (6 - (n)))

#define PIN_MASK_ROWS       (LL_GPIO_PIN_15 | LL_GPIO_PIN_14 | LL_GPIO_PIN_13 | LL_GPIO_PIN_12)
#define PIN_MASK_ROW(n)     (1u << (15 - (n)))

static inline uint32_t read_rows()
{
    return PIN_MASK_ROWS & LL_GPIO_ReadInputPort(GPIOx);
}

static const KEY_Code_t keyboard[5][4] = {
    {   // Zero col
        // Set to zero to handle special case of nothing pulled down
        KEY_SIDE1, 
        KEY_SIDE2, 

        // Duplicate to fill the array with valid values
        KEY_INVALID, 
        KEY_INVALID, 
    },
    {   // First col
        KEY_MENU, 
        KEY_1, 
        KEY_4, 
        KEY_7, 
    },
    {   // Second col
        KEY_UP, 
        KEY_2 , 
        KEY_5 , 
        KEY_8 , 
    },
    {   // Third col
        KEY_DOWN, 
        KEY_3   , 
        KEY_6   , 
        KEY_9   , 
    },
    {   // Fourth col
        KEY_EXIT, 
        KEY_STAR, 
        KEY_0   , 
        KEY_F   , 
    }
};

KEY_Code_t KEYBOARD_Poll(void)
{
#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
    // Serial-injected key: hold it for SHORT or LONG polls depending on press type,
    // so the debounce counter in app.c reaches the right threshold:
    //   - Short: key_debounce_10ms (2)  → ProcessKey(key, true, false)
    //   - Long:  key_repeat_delay_10ms (40) → ProcessKey(key, true, true)
    // Once the hold count is exhausted we clear it — next call returns KEY_INVALID,
    // which triggers the release path in app.c naturally.
    if (gKeyFromSerial != KEY_INVALID) {
        KEY_Code_t injected  = gKeyFromSerial;
        uint8_t    threshold = gSerialKeyLong ? SERIAL_KEY_LONG_POLLS : SERIAL_KEY_SHORT_POLLS;
        if (++gSerialKeyHoldCount >= threshold) {
            gKeyFromSerial      = KEY_INVALID;
            gSerialKeyHoldCount = 0;
            gSerialKeyLong      = 0;
        }
        return injected;
    }
#endif

    KEY_Code_t Key = KEY_INVALID;

    // Scan all 5 columns - never break early to avoid GPIO state issues
    for (unsigned int j = 0; j < 5; j++)
    {
        uint32_t reg;
        uint32_t match_count = 0;  // Count consecutive matching reads

        // Set all columns high first
        GPIO_SetOutputPin(PIN_COLS);

        // Clear the specific column we are selecting
        if (j > 0)
        {
            GPIO_ResetOutputPin(PIN_COL(j - 1));
        }

        // Debounce: Read rows multiple times and look for stable reads
        // CRITICAL FIX #1: Proper debounce logic (replaces confusing i *= syntax)
        // CRITICAL FIX #2: Increased delay from 1µs to 10µs for real keyboard bounce capture
        // CRITICAL FIX #3: Clear match_count if reads differ (noise detection)
        reg = 0;
        for (unsigned int k = 0; k < 8; k++)
        {
            SYSTICK_DelayUs(10);  // FIX #2: Increased from 1µs to 10µs
            uint32_t reg2 = read_rows();
            
            // FIX #3: Clear match counter if values don't match
            if (reg2 != reg)
            {
                match_count = 0;
                reg = reg2;
            }
            else
            {
                match_count++;
            }
            
            // Success: We have 3 consecutive matching reads = stable signal
            if (match_count >= 2)
            {
                break;  // Debounce complete for this column
            }
        }

        // FIX #1: Do NOT break on noise - continue scanning all columns
        // Only skip key detection if debounce failed, but GPIO state is cleaned
        if (match_count < 2)
        {
            // Debounce failed (too much noise), but continue to next column
            // This prevents GPIO from staying in invalid state and blocking other columns
            continue;
        }

        // Debounce successful, check which row is pressed in this column
        for (unsigned int i = 0; i < 4; i++)
        {
            if (!(reg & PIN_MASK_ROW(i)))
            {
                Key = keyboard[j][i];
                break;
            }
        }

        if (Key != KEY_INVALID)
        {
            break;  // Found a valid key, stop scanning
        }
    }

    // CRITICAL FIX #4: Always clean up GPIO state - set all columns high at end
    // This ensures GPIO pins are in a known state even if function exited early due to noise
    GPIO_SetOutputPin(PIN_COLS);

    return Key;
}

KEY_Code_t KEYBOARD_GetKey(void)
{
    KEY_Code_t btn = KEYBOARD_Poll();
    if (btn == KEY_INVALID && GPIO_IsPttPressed())
    {
        btn = KEY_PTT;
    }
    return btn;
}

void HideFKeyIcon(void) {
    gWasFKeyPressed       = false;
    gUpdateStatus         = true;
}