#pragma once

#include "keyboard_state.h"
#include "../audio.h"
#include "../driver/bk4819.h"
#include "../driver/keyboard.h"
#include "../driver/st7565.h"
#include "../driver/system.h"
#include "../external/printf/printf.h"
#include "../font.h"
#include "../settings.h"
#include "../ui/helper.h"
#include "../driver/gpio.h"
#include "../helper/battery.h"
#include "../driver/bk4819-regs.h"
#include <string.h>

#define SNAKE_MAX_LENGTH 64

typedef struct {
    uint8_t x;
    uint8_t y;
} Point;

typedef struct {
    Point segments[SNAKE_MAX_LENGTH];
    uint8_t length;
    int8_t dx;
    int8_t dy;
} Snake;

void APP_RunSnake(void);
