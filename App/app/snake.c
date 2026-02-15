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

#include "app/snake.h"

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
#include "screenshot.h"
#endif

#define BLOCK_SIZE 5
#define OFFSET_X 2
#define OFFSET_Y 1
#define GRID_W ((128 - 2 * OFFSET_X) / BLOCK_SIZE)
#define GRID_H (((64 - OFFSET_Y - 1) / BLOCK_SIZE) - 1) // Reserve top pixels for status/score and bottom margin

static uint32_t randSeed = 1;

static bool isInitialized = false;
static bool isPaused = false;
static bool isBeep = false;
static bool isGameOver = false;

static uint16_t score = 0;
static uint16_t highScore = 0;
static uint16_t tone = 0;

static char str[16];

static KeyboardState kbd = {KEY_INVALID, KEY_INVALID, 0};

static Snake snake;
static Point food;

// Initialise seed
static void srand_custom(uint32_t seed) {
    randSeed = seed;
}

// Return pseudo-random from 0 to RAND_MAX (here 32767)
static int rand_custom(void) {
    randSeed = randSeed * 1103515245 + 12345;
    return (randSeed >> 16) & 0x7FFF; // 15 bits
}

// Return integer from min to max include
static int randInt(int min, int max) {
    return min + (rand_custom() % (max - min + 1));
}

// PlayBeep
static void playBeep(uint16_t freq, uint16_t duration)
{
    BK4819_PlayTone(freq, true);
    AUDIO_AudioPathOn();
    BK4819_ExitTxMute();
    SYSTEM_DelayMs(duration);
    BK4819_EnterTxMute();
    AUDIO_AudioPathOff();
}

static void playGameOverMusic(void)
{
    playBeep(880, 150);
    playBeep(600, 150);
    playBeep(440, 150);
    playBeep(220, 300);
}

static void initFood(void) {
    bool onSnake = true;
    uint16_t attempts = 0;
    while(onSnake && attempts < 100) {
        food.x = randInt(0, GRID_W - 1) * BLOCK_SIZE + OFFSET_X;
        food.y = randInt(0, GRID_H - 1) * BLOCK_SIZE + OFFSET_Y;
        
        onSnake = false;
        for(int i = 0; i < snake.length; i++) {
            if(snake.segments[i].x == food.x && snake.segments[i].y == food.y) {
                onSnake = true;
                break;
            }
        }
        attempts++;
    }
}

static void initSnake(void) {
    snake.length = 3;
    snake.segments[0].x = (GRID_W / 2) * BLOCK_SIZE + OFFSET_X;
    snake.segments[0].y = (GRID_H / 2) * BLOCK_SIZE + OFFSET_Y;
    snake.segments[1].x = snake.segments[0].x - BLOCK_SIZE;
    snake.segments[1].y = snake.segments[0].y;
    snake.segments[2].x = snake.segments[0].x - (2 * BLOCK_SIZE);
    snake.segments[2].y = snake.segments[0].y;
    snake.dx = BLOCK_SIZE;
    snake.dy = 0;
    
    score = 0;
    isGameOver = false;
    
    initFood();
}

static void drawScore(void) {
    // Clean status line
    memset(gStatusLine, 0, sizeof(gStatusLine));

    sprintf(str, "Score: %u", score);
    GUI_DisplaySmallest(str, 0, 1, true, true);
    
    sprintf(str, "Hi: %u", highScore);
    GUI_DisplaySmallest(str, 80, 1, true, true);
}

static void updateSnake(void) {
    if (isGameOver || isPaused) return;

    // Calculate new head position
    int16_t newX = snake.segments[0].x + snake.dx;
    int16_t newY = snake.segments[0].y + snake.dy;

    // Check collision with walls
    if (newX < OFFSET_X || newX >= OFFSET_X + GRID_W * BLOCK_SIZE || newY < OFFSET_Y || newY >= OFFSET_Y + GRID_H * BLOCK_SIZE) {
        isGameOver = true;
        playGameOverMusic();
        return;
    }

    // Check collision with self
    for (int i = 0; i < snake.length; i++) {
        if (newX == snake.segments[i].x && newY == snake.segments[i].y) {
            isGameOver = true;
            playGameOverMusic();
            return;
        }
    }

    // Check collision with food
    if (newX == food.x && newY == food.y) {
        score += 10;
        if (score > highScore) highScore = score;
        
        if (snake.length < SNAKE_MAX_LENGTH) {
            snake.length++;
        }
        
        isBeep = true;
        tone = 800;
        
        initFood();
    }

    // Move body
    for (int i = snake.length - 1; i > 0; i--) {
        snake.segments[i] = snake.segments[i - 1];
    }

    // Move head
    snake.segments[0].x = newX;
    snake.segments[0].y = newY;
}

static void drawGame(void) {
    // Clear buffer
    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    
    // Draw Frame
    UI_DrawLineBuffer(gFrameBuffer, OFFSET_X - 1, OFFSET_Y - 1, OFFSET_X + GRID_W * BLOCK_SIZE, OFFSET_Y - 1, true); // Top
    UI_DrawLineBuffer(gFrameBuffer, OFFSET_X - 1, (OFFSET_Y - 1) + GRID_H * BLOCK_SIZE, OFFSET_X + GRID_W * BLOCK_SIZE, OFFSET_Y + GRID_H * BLOCK_SIZE, true); // Bottom
    UI_DrawLineBuffer(gFrameBuffer, OFFSET_X - 1, OFFSET_Y - 1, OFFSET_X - 1, OFFSET_Y + GRID_H * BLOCK_SIZE, true); // Left
    UI_DrawLineBuffer(gFrameBuffer, OFFSET_X + GRID_W * BLOCK_SIZE, OFFSET_Y - 1, OFFSET_X + GRID_W * BLOCK_SIZE, OFFSET_Y + GRID_H * BLOCK_SIZE, true); // Right

    // Draw Snake
    for(int i = 0; i < snake.length; i++) {
        UI_DrawRectangleBuffer(gFrameBuffer, 
            snake.segments[i].x, snake.segments[i].y, 
            snake.segments[i].x + BLOCK_SIZE - 1, snake.segments[i].y + BLOCK_SIZE - 1, true);
    }
    
    // Draw Food
    UI_DrawRectangleBuffer(gFrameBuffer, 
        food.x + 1, food.y + 1, 
        food.x + BLOCK_SIZE - 1, food.y + BLOCK_SIZE - 1, true);
    // Inner dot for food
    UI_DrawLineBuffer(gFrameBuffer, food.x + 3, food.y + 3, food.x + 4, food.y + 4, false);

    if (isGameOver) {
        UI_PrintStringSmallBold("GAME", 48, 16, 2);
        UI_PrintStringSmallBold("OVER", 48, 16, 4);
    }
    
    if (isPaused) {
        UI_PrintStringSmallBold("PAUSED", 40, 0, 3);
    }
}

static void OnKeyDown(uint8_t key)
{
    switch (key)
    {
    case KEY_UP:
        if (snake.dy == 0) { snake.dx = 0; snake.dy = -BLOCK_SIZE; }
        break;
    case KEY_DOWN:
        if (snake.dy == 0) { snake.dx = 0; snake.dy = BLOCK_SIZE; }
        break;
    case KEY_MENU:
        isPaused = !isPaused;
        break;
    case KEY_EXIT:
        isInitialized = false;
        break;
    
    // Direction controls using keypad
    case KEY_2: // Up
        if (snake.dy == 0) { snake.dx = 0; snake.dy = -BLOCK_SIZE; }
        break;
    case KEY_8: // Down
        if (snake.dy == 0) { snake.dx = 0; snake.dy = BLOCK_SIZE; }
        break;
    case KEY_4: // Left
        if (snake.dx == 0) { snake.dx = -BLOCK_SIZE; snake.dy = 0; }
        break;
    case KEY_6: // Right
        if (snake.dx == 0) { snake.dx = BLOCK_SIZE; snake.dy = 0; }
        break;
        
    case KEY_PTT:
        if (isGameOver) {
            initSnake();
        }
        break;
    }
}

// Key 
static KEY_Code_t GetKey()
{
    KEY_Code_t btn = KEYBOARD_Poll();
    if (btn == KEY_INVALID && GPIO_IsPttPressed())
    {
        btn = KEY_PTT;
    }
    return btn;
}

// HandleUserInput 
static bool HandleUserInput()
{
    kbd.prev = kbd.current;
    kbd.current = GetKey();
    
    if (kbd.current != KEY_INVALID && kbd.current == kbd.prev) {
        kbd.counter++;
    } else {
        kbd.counter = 0;
    }
    
    if (kbd.counter == 0 || kbd.counter > 20) // Initial press or repeat delay
    {
        if (kbd.current != KEY_INVALID) {
            OnKeyDown(kbd.current);
            if(kbd.counter > 20) kbd.counter = 15; // Faster repeat
        }
    }
    
    return true;
}

void APP_RunSnake(void) {
    // Init seed
    srand_custom(BK4819_ReadRegister(BK4819_REG_67) & 0x01FF * gBatteryVoltageAverage);

    UI_DisplayClear();
    initSnake();
    isInitialized = true;
    isPaused = false;

    while(isInitialized)
    {
        // For screenshot
        #ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
            getScreenShot(false);
        #endif

        HandleUserInput();
        
        // Simple timing
        
        static uint8_t tickDiv = 0;
        tickDiv++;
        if (tickDiv >= 10) { // Adjust speed
            tickDiv = 0;
            updateSnake();
        }

        drawScore();
        drawGame();
        
        if(isBeep)
        {
            playBeep(tone, 100);
            isBeep = false;
        }

        ST7565_BlitStatusLine();
        ST7565_BlitFullScreen();
        
        SYSTEM_DelayMs(20);
    }
}
