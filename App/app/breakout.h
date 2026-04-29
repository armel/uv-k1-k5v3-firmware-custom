/* Copyright 2025 Armel F4HWN
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

#pragma once

#include "keyboard_state.h"

#include "../bitmaps.h"
#include "../board.h"
#include "py32f0xx.h"
#include "../driver/bk4819-regs.h"
#include "../driver/bk4819.h"
#include "../driver/gpio.h"
#include "../driver/keyboard.h"
#include "../driver/st7565.h"
#include "../driver/system.h"
#include "../driver/systick.h"
#include "../external/printf/printf.h"
#include "../font.h"
#include "../helper/battery.h"
#include "../misc.h"
#include "../radio.h"
#include "../settings.h"
#include "../ui/helper.h"
#include "../audio.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define BRICK_NUMBER 18
#define BALL_NUMBER  5

typedef struct {
    uint8_t x;       // x
    uint8_t y;       // y
    uint8_t w;       // width
    uint8_t h;       // height
    bool destroy;    // active, if true, check this button, else bypass
} Brick;

typedef struct {
    int8_t x;   // x
    uint8_t y;  // y
    uint8_t w;  // width
    uint8_t h;  // height
    uint8_t p;  // previous x
} Racket;

typedef struct {
    int16_t x;  // x
    int8_t y;   // y
    uint8_t w;  // width
    uint8_t h;  // height
    int8_t dx;  // move x
    int8_t dy;  // move y
} Ball;

void initWall(void);
void drawWall(void);
void initRacket(void);
void drawRacket(void);
void APP_RunBreakout(void);
