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

#include "app/breakout.h"

#if defined(ENABLE_UART) || defined(ENABLE_USB)
#include "app/uart.h"
#endif

#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
#include "k5viewer.h"
#endif

#define BRICK_WIDTH  14
#define BRICK_HEIGHT 5
#define BALL_WIDTH    3
#define BALL_HEIGHT   3
#define RACKET_WIDTH  24
#define RACKET_HEIGHT 2
#define RACKET_Y      50

static uint32_t randSeed = 1;
static uint8_t blockAnim = 0;

static bool isInitialized = false;
bool isPaused = false;

uint8_t levelCountBreakout = 1;

uint16_t tone = 0;
uint16_t score = 0;

int16_t ballCount = BALL_NUMBER;

char str[12];

static KeyboardState kbd = {KEY_INVALID, KEY_INVALID, 0};

Brick brick[BRICK_NUMBER];
Racket racket;
Ball ball;

static const uint8_t BRICK_ANIM_PATTERNS[4] = {0b00110001, 0b00101001, 0b00100101, 0b00100011};

// Initialise seed
void srand_custom(uint32_t seed) {
    randSeed = seed;
}

// Return pseudo-random from 0 to RAND_MAX (here 32767)
int rand_custom(void) {
    randSeed = randSeed * 1103515245 + 12345;
    return (randSeed >> 16) & 0x7FFF; // 15 bits
}

// Return integer from min to max include
int randInt(int min, int max) {
    return min + (rand_custom() % (max - min + 1));
}

// Reset
void reset(void)
{
    ballCount = BALL_NUMBER;
    levelCountBreakout = 1;
    score = 0;
}

// PlayBeep
void playBeep(uint16_t tone)
{
    BK4819_PrepareToPlayTone(true);
    AUDIO_AudioPathOn();
    BK4819_PlayToneRaw(tone, 100);
    AUDIO_AudioPathOff();
}

// Draw score
void drawScore()
{
    // Clean status line
    UI_StatusClear();

    // Level
    sprintf(str, "Level %02u", levelCountBreakout);
    GUI_DisplaySmallest(str, 0, 1, true, true);

    // Ball
    sprintf(str, "Ball %02u", (ballCount < 0) ? 0 : ballCount);
    GUI_DisplaySmallest(str, 45, 1, true, true);

    // Score
    sprintf(str, "Score %04u", score);
    GUI_DisplaySmallest(str, 88, 1, true, true);
}

// Render the ball
void renderBall(bool state) {
    UI_DrawRectangleBuffer(gFrameBuffer, ball.x, ball.y, ball.x + BALL_WIDTH - 1, ball.y + BALL_HEIGHT - 1, state);
    UI_DrawLineBuffer(gFrameBuffer, ball.x - 1, ball.y + 1, ball.x + BALL_WIDTH, ball.y + 1, state);
}

// Init ball
void initBall() {
    ball.x  = 62;
    ball.y  = 30;
    ball.dx = 0;
    ball.dy = 1;

    renderBall(true);
}

// Calculate the direction of the bounced ball
void directionBall(int16_t x, uint8_t w, int8_t num) {
    ball.dx = (int16_t)(x + w - ball.x) * (-num - num) / w + num;
    ball.dy *= -1;
}

// Draw ball
void drawBall() {
    renderBall(false);

    ball.x += ball.dx;
    ball.y += ball.dy;

    if (ball.y <= 0)  // Up
    {
        ball.dx = randInt(-3, 3);
        ball.dy = 1;
    }
    else if (ball.x <= 2)  // Left
    {
        ball.dx = abs(ball.dx);
    } 
    else if (ball.x >= 124)  // Right
    {
        ball.dx = -abs(ball.dx);
    }
    // And now Down...
    if (ball.y == 47) {
        if (ball.x + 1 >= racket.x && ball.x - 1 <= racket.x + RACKET_WIDTH) {
            directionBall(racket.x, RACKET_WIDTH, 3);
            tone = 400;
        }
    } 
    else if(ball.y > 49) {
        ballCount--;
        UI_DisplayClear();
        drawScore();
        tone = 800;
        if (ballCount < 0) {
            reset();
            initWall();
            drawWall();

            isPaused = true;
            UI_PrintStringSmallBold("GAME OVER", 0, LCD_WIDTH - 1, 4);
        }
        initRacket();
        initBall();
    }

    renderBall(true);
}

// Init wall
void initWall() {
    Brick *current = brick;

    for (uint8_t y = 0; y < 24; y += 8) {
        for (uint8_t x = 6; x < 126; x += 20) {
            current->x       = x;
            current->y       = y;
            current->destroy = false;
            current++;
        }
    }
}

// Draw wall
void drawWall() {
    uint8_t i = 0;

    for (i = 0; i < BRICK_NUMBER; i++) {
        if (brick[i].destroy == false) {
            uint8_t *fb_ptr = gFrameBuffer[brick[i].y / 8] + brick[i].x;

            fb_ptr[0] = 0b00011110;
            fb_ptr[14] = 0b00011110;

            if ((ball.x + 1 >= brick[i].x &&
                 ball.x - 1 <= brick[i].x + BRICK_WIDTH) &&
                ((ball.y + 1 >= brick[i].y && 
                  ball.y - 1 <= brick[i].y + BRICK_HEIGHT))) {
                brick[i].destroy = true;
                score++;

                directionBall(brick[i].x, BRICK_WIDTH, 2);

                BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
                memset(fb_ptr + 1, 0b00111111, 13);
                ST7565_BlitLine(brick[i].y / 8);
                playBeep(600);
                memset(fb_ptr + 0, 0b00000000, 15);
                ST7565_BlitLine(brick[i].y / 8);
                BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);

                if (score % BRICK_NUMBER == 0) {
                    levelCountBreakout++;
                    ballCount++;
                    initWall();
                    return;
                }
            } else {
                for(uint8_t k = 0; k < 13; k++) {
                    fb_ptr[k + 1] = BRICK_ANIM_PATTERNS[(blockAnim + k) % 4];
                }
            }
        }
    }
}

// Render the racket shape
void renderRacket(int x, bool state) {
    UI_DrawRectangleBuffer(gFrameBuffer, x + 1, RACKET_Y, x + RACKET_WIDTH - 2, RACKET_Y + RACKET_HEIGHT, state);
    UI_DrawLineBuffer(gFrameBuffer, x, RACKET_Y + 1, x + RACKET_WIDTH - 1, RACKET_Y + 1, state);
}

// Init racket
void initRacket() {
    racket.x = 64 - (RACKET_WIDTH / 2);
    racket.p = racket.x;

    renderRacket(racket.x, true);
}

// Draw racket
void drawRacket() {
    if (racket.p != racket.x) {
        renderRacket(racket.p, false);
        racket.p = racket.x;
        renderRacket(racket.x, true);
    }
}

// OnKeyDown
static void OnKeyDown(uint8_t key)
{
    bool wasPaused = isPaused;
    
    switch (key)
    {
    case KEY_4:
    case KEY_UP:
        if(!isPaused && racket.x > 0)
            racket.x -= 2;
        isPaused = false;
        break;
    case KEY_0:
    case KEY_DOWN:
        if(!isPaused && racket.x < 102)
            racket.x += 2;
        isPaused = false;        
        break;
    case KEY_MENU:
        isPaused = !isPaused;
        kbd.counter = 0;
        if(isPaused)
        {
            UI_PrintStringSmallBold("PAUSE", 0, LCD_WIDTH - 1, 4);
        }
        break;
    case KEY_EXIT:
        isPaused = false;
        isInitialized = false;
        break;
    }
    
    if(wasPaused == true && isPaused == false)
    {
        // Clear the pause text
        for(uint8_t i = 0; i < 8; i++)
        {
            UI_DrawLineBuffer(gFrameBuffer, 32, 32 + i, 96, 32 + i, false);
        }
    }
}

// HandleUserInput 
static bool HandleUserInput()
{
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    // Parse incoming packets on every tick so serial keys are never missed,
    // regardless of whether the screen needs redrawing.
    K5VIEWER_ParseInput();
#endif

    kbd.prev = kbd.current;
    kbd.current = KEYBOARD_GetKey();

    if (kbd.current == KEY_INVALID)
        return true;

    if (
        // Movement keys: dispatch on every tick while held
        (kbd.current == KEY_UP   || kbd.current == KEY_DOWN ||
         kbd.current == KEY_4    || kbd.current == KEY_0)   ||
        // Action keys (MENU, EXIT): dispatch only on rising edge
        (kbd.current != kbd.prev)
    ){
        OnKeyDown(kbd.current);
    }

    return true;
}

// Tick
static void Tick()
{
    HandleUserInput();
}

// APP_RunBreakout
void APP_RunBreakout(void) {
    static uint8_t swap = 0;

    // Init seed
    srand_custom(BK4819_ReadRegister(BK4819_REG_67) & 0x01FF * gBatteryVoltageAverage * gEeprom.VfoInfo[0].pRX->Frequency);

    // Init led
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);

    // Finish the brightness fade if it is in progress
    BACKLIGHT_UpdateTickless();

    // Init game
    UI_DisplayClear();
    reset();
    initWall();
    initRacket();
    initBall();
    UI_StatusClear();
    isInitialized = true;

    while(isInitialized)
    {
#if defined(ENABLE_UART) || defined(ENABLE_USB)
        UART_ServiceCommands();
#endif
        Tick();
        if(!isPaused)
        {
            if(swap == 0)
            {
                blockAnim = (blockAnim + 1) % 4;
            }
            
            swap = (swap + 1) % 4;

            drawScore();
            drawWall();
            drawRacket();
            drawBall();
                
            if(tone != 0)
            {
                playBeep(tone);
                tone = 0;
            }
            else
            {
                SYSTEM_DelayMs(40 - MIN(levelCountBreakout - 1, 20)); // Add more fun...
            }
        }

        ST7565_BlitStatusLine();  // Blank status line
        ST7565_BlitFullScreen();

        #ifdef ENABLE_FEAT_F4HWN_K5VIEWER
            if(isPaused || swap == 0)
            {
                K5VIEWER_Update(false);
            }
        #endif
    }
}
