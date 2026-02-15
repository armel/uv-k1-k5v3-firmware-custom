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

#include <stdint.h>
#include <stdio.h>     // NULL

#include "py32f071_ll_bus.h"
#include "py32f071_ll_spi.h"
#include "py32f071_ll_gpio.h"
#include "driver/gpio.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "misc.h"
#include "screenshot.h"

#define SPIx SPI1

#define PIN_CS GPIO_MAKE_PIN(GPIOB, LL_GPIO_PIN_2)
#define PIN_A0 GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_6)

uint8_t gStatusLine[LCD_WIDTH];
uint8_t gFrameBuffer[FRAME_LINES][LCD_WIDTH];

static void SPI_Init()
{
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SPI1);
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);

    do
    {
        LL_GPIO_InitTypeDef InitStruct;
        LL_GPIO_StructInit(&InitStruct);
        InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
        InitStruct.Alternate = LL_GPIO_AF0_SPI1;
        InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
        InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;

        // SCK: PA5
        InitStruct.Pin = LL_GPIO_PIN_5;
        InitStruct.Pull = LL_GPIO_PULL_UP;
        LL_GPIO_Init(GPIOA, &InitStruct);

        // SDA: PA7
        InitStruct.Pin = LL_GPIO_PIN_7;
        InitStruct.Pull = LL_GPIO_PULL_NO;
        LL_GPIO_Init(GPIOA, &InitStruct);
    } while (0);

    LL_SPI_InitTypeDef InitStruct;
    LL_SPI_StructInit(&InitStruct);
    InitStruct.TransferDirection = LL_SPI_FULL_DUPLEX;
    InitStruct.Mode = LL_SPI_MODE_MASTER;
    InitStruct.DataWidth = LL_SPI_DATAWIDTH_8BIT;
    InitStruct.ClockPolarity = LL_SPI_POLARITY_HIGH;
    InitStruct.ClockPhase = LL_SPI_PHASE_2EDGE;
    InitStruct.NSS = LL_SPI_NSS_SOFT;
    InitStruct.BitOrder = LL_SPI_MSB_FIRST;
    InitStruct.CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE;
    InitStruct.BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV64;
    LL_SPI_Init(SPIx, &InitStruct);

    LL_SPI_Enable(SPIx);
}

static inline void CS_Assert()
{
    GPIO_ResetOutputPin(PIN_CS);
}

static inline void CS_Release()
{
    GPIO_SetOutputPin(PIN_CS);
}

static inline void A0_Set()
{
    GPIO_SetOutputPin(PIN_A0);
}

static inline void A0_Reset()
{
    GPIO_ResetOutputPin(PIN_A0);
}

static uint8_t SPI_WriteByte(uint8_t Value)
{
    while (!LL_SPI_IsActiveFlag_TXE(SPIx))
        ;

    LL_SPI_TransmitData8(SPIx, Value);

    while (!LL_SPI_IsActiveFlag_RXNE(SPIx))
        ;

    return LL_SPI_ReceiveData8(SPIx);
}

static void DrawLine(uint8_t column, uint8_t line, const uint8_t * lineBuffer, unsigned size_defVal)
{   
    ST7565_SelectColumnAndLine(column + 4, line);
    A0_Set();
    for (unsigned i = 0; i < size_defVal; i++) {
        SPI_WriteByte(lineBuffer ? lineBuffer[i] : size_defVal);
    }
}

void ST7565_DrawLine(const unsigned int Column, const unsigned int Line, const uint8_t *pBitmap, const unsigned int Size)
{
    CS_Assert();
    DrawLine(Column, Line, pBitmap, Size);
    CS_Release();
}


#ifdef ENABLE_FEAT_F4HWN
    // Optimization
    //
    // ST7565_BlitScreen(0) = ST7565_BlitStatusLine()
    // ST7565_BlitScreen(1..7) = ST7565_BlitLine()
    // ST7565_BlitScreen(8) = ST7565_BlitFullScreen()
    //

    static void ST7565_BlitScreen(uint8_t line)
    {
        CS_Assert();
        ST7565_WriteByte(0x40);

        if(line == 0)
        {
            DrawLine(0, 0, gStatusLine, LCD_WIDTH);
        }
        else if(line <= FRAME_LINES)
        {
            DrawLine(0, line, gFrameBuffer[line - 1], LCD_WIDTH);
        }
        else
        {
            for (line = 1; line <= FRAME_LINES; line++) {
                DrawLine(0, line, gFrameBuffer[line - 1], LCD_WIDTH);
            }
        }

        CS_Release();
    }

    void ST7565_BlitFullScreen(void)
    {
        ST7565_BlitScreen(8);
    }

    void ST7565_BlitLine(unsigned line)
    {
        ST7565_BlitScreen(line + 1);
        getScreenShot(true);  // Force immediate capture
    }

    void ST7565_BlitStatusLine(void)
    {
        ST7565_BlitScreen(0);
    }
#else
    void ST7565_BlitFullScreen(void)
    {
        CS_Assert();
        ST7565_WriteByte(0x40);
        for (unsigned line = 0; line < FRAME_LINES; line++) {
            DrawLine(0, line+1, gFrameBuffer[line], LCD_WIDTH);
        }
        CS_Release();
    }

    void ST7565_BlitLine(unsigned line)
    {
        CS_Assert();
        ST7565_WriteByte(0x40);    // start line ?
        DrawLine(0, line+1, gFrameBuffer[line], LCD_WIDTH);
        CS_Release();
    }

    void ST7565_BlitStatusLine(void)
    {   // the top small text line on the display
        CS_Assert();
        ST7565_WriteByte(0x40);    // start line ?
        DrawLine(0, 0, gStatusLine, LCD_WIDTH);
        CS_Release();
    }
#endif

void ST7565_FillScreen(uint8_t value)
{
    CS_Assert();
    for (unsigned i = 0; i < 8; i++) {
        // TODO: This is wrong
        DrawLine(0, i, NULL, value);
    }
    CS_Release();
}

// Software reset
#define ST7565_CMD_SOFTWARE_RESET 0xE2 
// Bias Select
// 1 0 1 0 0 0 1 BS
// Select bias setting 0=1/9; 1=1/7 (at 1/65 duty)
#define ST7565_CMD_BIAS_SELECT 0xA2 
// COM Direction
// 1 1 0 0 MY - - -
// Set output direction of COM
// MY=1, reverse direction
// MY=0, normal direction
#define ST7565_CMD_COM_DIRECTION 0xC0 
// SEG Direction
// 1 0 1 0 0 0 0 MX
// Set scan direction of SEG
// MX=1, reverse direction
// MX=0, normal direction
#define ST7565_CMD_SEG_DIRECTION 0xA0 
// Inverse Display
// 1 0 1 0 0 1 1 INV
// INV =1, inverse display
// INV =0, normal display
#define ST7565_CMD_INVERSE_DISPLAY 0xA6 
// All Pixel ON
// 1 0 1 0 0 1 0 AP
// AP=1, set all pixel ON
// AP=0, normal display
#define ST7565_CMD_ALL_PIXEL_ON 0xA4 
// Regulation Ratio
// 0 0 1 0 0 RR2 RR1 RR0
// This instruction controls the regulation ratio of the built-in regulator
#define ST7565_CMD_REGULATION_RATIO 0x20 
// Double command!! Set electronic volume (EV) level
// Send next: 0 0 EV5 EV4 EV3 EV2 EV1 EV0  contrast 0-63
#define ST7565_CMD_SET_EV 0x81 
// Control built-in power circuit ON/OFF - 0 0 1 0 1 VB VR VF
// VB: Built-in Booster
// VR: Built-in Regulator
// VF: Built-in Follower
#define ST7565_CMD_POWER_CIRCUIT 0x28 
// Set display start line 0-63
// 0 0 0 1 S5 S4 S3 S2 S1 S0 
#define ST7565_CMD_SET_START_LINE 0x40 
// Display ON/OFF 
// 0 0 1 0 1 0 1 1 1 D 
// D=1, display ON
// D=0, display OFF
#define ST7565_CMD_DISPLAY_ON_OFF 0xAE 

uint8_t cmds[] = {
    ST7565_CMD_BIAS_SELECT | 0,             // Select bias setting: 1/9
    ST7565_CMD_COM_DIRECTION  | (0 << 3),   // Set output direction of COM: normal
    ST7565_CMD_SEG_DIRECTION | 1,           // Set scan direction of SEG: reverse
    ST7565_CMD_INVERSE_DISPLAY | 0,         // Inverse Display: false
    ST7565_CMD_ALL_PIXEL_ON | 0,            // All Pixel ON: false - normal display
    ST7565_CMD_REGULATION_RATIO | (4 << 0), // Regulation Ratio 5.0

    ST7565_CMD_SET_EV,                      // Set contrast
    31,

    ST7565_CMD_POWER_CIRCUIT | 0b111,       // Built-in power circuit ON/OFF: VB=1 VR=1 VF=1 
    ST7565_CMD_SET_START_LINE | 0,          // Set Start Line: 0
    ST7565_CMD_DISPLAY_ON_OFF | 1,          // Display ON/OFF: ON
};

#ifdef ENABLE_FEAT_F4HWN
    static void ST7565_Cmd(uint8_t i)
    {
        switch(i) {
            case 3:
                ST7565_WriteByte(ST7565_CMD_INVERSE_DISPLAY | gSetting_set_inv);
                break;
            case 7:
                ST7565_WriteByte(21 + gSetting_set_ctr);
                break;
            default:
                ST7565_WriteByte(cmds[i]);
        }
    }

    #if defined(ENABLE_FEAT_F4HWN_CTR) || defined(ENABLE_FEAT_F4HWN_INV)
    void ST7565_ContrastAndInv(void)
    {
        CS_Assert();
        ST7565_WriteByte(ST7565_CMD_SOFTWARE_RESET);   // software reset

        for(uint8_t i = 0; i < 8; i++)
        {
            ST7565_Cmd(i);
        }

        // TODO: Release CS??
    }
    #endif

    int16_t map(int16_t x, int16_t in_min, int16_t in_max, int16_t out_min, int16_t out_max) {
        return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    }

    //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
    void ST7565_Gauge(uint8_t line, uint8_t min, uint8_t max, uint8_t value)
    {
        gFrameBuffer[line][54] = 0x0c;
        gFrameBuffer[line][55] = 0x12;

        gFrameBuffer[line][121] = 0x12;
        gFrameBuffer[line][122] = 0x0c;

        uint8_t filled = map(value, min, max, 56, 120);

        for (uint8_t i = 56; i <= 120; i++) {
            gFrameBuffer[line][i] = (i <= filled) ? 0x2d : 0x21;
        }
    }
    //#endif
#endif
    
void ST7565_Init(void)
{
    SPI_Init();
    ST7565_HardwareReset();
    CS_Assert();
    ST7565_WriteByte(ST7565_CMD_SOFTWARE_RESET);   // software reset
    SYSTEM_DelayMs(120);

    for(uint8_t i = 0; i < 8; i++)
    {
#ifdef ENABLE_FEAT_F4HWN
        ST7565_Cmd(i);
#else
        ST7565_WriteByte(cmds[i]);
#endif
    }

    ST7565_WriteByte(ST7565_CMD_POWER_CIRCUIT | 0b011);   // VB=0 VR=1 VF=1
    SYSTEM_DelayMs(1);
    ST7565_WriteByte(ST7565_CMD_POWER_CIRCUIT | 0b110);   // VB=1 VR=1 VF=0
    SYSTEM_DelayMs(1);

    for(uint8_t i = 0; i < 4; i++) // why 4 times?
        ST7565_WriteByte(ST7565_CMD_POWER_CIRCUIT | 0b111);   // VB=1 VR=1 VF=1

    SYSTEM_DelayMs(40);
    
    ST7565_WriteByte(ST7565_CMD_SET_START_LINE | 0);   // line 0
    ST7565_WriteByte(ST7565_CMD_DISPLAY_ON_OFF | 1);   // D=1

    CS_Release();

    ST7565_FillScreen(0x00);
}

#ifdef ENABLE_FEAT_F4HWN_SLEEP
    void ST7565_ShutDown(void)
    {
        CS_Assert();
        ST7565_WriteByte(ST7565_CMD_POWER_CIRCUIT | 0b000);   // VB=0 VR=1 VF=1
        ST7565_WriteByte(ST7565_CMD_SET_START_LINE | 0);   // line 0
        ST7565_WriteByte(ST7565_CMD_DISPLAY_ON_OFF | 0);   // D=1
        CS_Release();
    }
#endif

void ST7565_FixInterfGlitch(void)
{
    CS_Assert();
    for(uint8_t i = 0; i < ARRAY_SIZE(cmds); i++)
#ifdef ENABLE_FEAT_F4HWN
        ST7565_Cmd(i);
#else
        ST7565_WriteByte(cmds[i]);
#endif

    CS_Release();
}

void ST7565_HardwareReset(void)
{
    // Not supported on K1
    // TODO: Delete this function
}

void ST7565_SelectColumnAndLine(uint8_t Column, uint8_t Line)
{
    A0_Reset();
    SPI_WriteByte(Line + 176);
    SPI_WriteByte(((Column >> 4) & 0x0F) | 0x10);
    SPI_WriteByte((Column >> 0) & 0x0F);
}

/**
 *  Write a command (rather than pixel data)
 */
void ST7565_WriteByte(uint8_t Value)
{
    A0_Reset();
    SPI_WriteByte(Value);
}
