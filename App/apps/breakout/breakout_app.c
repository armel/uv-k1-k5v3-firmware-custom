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

/*
 * Breakout — overlay-app port (POC).
 *
 * Self-contained: no firmware headers, no libc. Every resident service is
 * reached through the app_api_t table; the entry point app_main() is forced to
 * blob offset 0 via section ".text.entry". The loader zeroes the 4 KiB overlay,
 * copies this blob in, and calls app_main(&api).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"

#define LCD_WIDTH     128
#define BRICK_NUMBER   18
#define BALL_NUMBER     5

#define BRICK_WIDTH  14
#define BRICK_HEIGHT  5
#define BALL_WIDTH    3
#define BALL_HEIGHT   3
#define RACKET_WIDTH 24
#define RACKET_HEIGHT 2
#define RACKET_Y     50

typedef struct { uint8_t x; uint8_t y; bool destroy; } Brick;
typedef struct { int8_t  x; uint8_t p; } Racket;
typedef struct { int16_t x; int8_t y; int8_t dx; int8_t dy; } Ball;

/* ---- tiny freestanding helpers (replace libc / libgcc-free where cheap) ---- */
void *memset(void *d, int c, size_t n) {
    uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d;
}
void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d;
}
static int iabs(int v) { return v < 0 ? -v : v; }
static int imin(int a, int b) { return a < b ? a : b; }

/* zero-padded unsigned -> string, width w (<= 5). Replaces sprintf. */
static void u2str(char *out, const char *label, uint16_t v, uint8_t w) {
    char *o = out;
    while (*label) *o++ = *label++;
    char tmp[6]; int8_t n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 6);
    while (n < w) tmp[n++] = '0';
    while (n--) *o++ = tmp[n];
    *o = '\0';
}

/* ---- app-wide handle to the resident ABI, set once at entry ---- */
static const app_api_t *A;

/* ---- game state (lives in the overlay; re-initialised at entry) ---- */
static uint32_t randSeed;
static uint8_t  blockAnim;
static bool     isInitialized;
static bool     isPaused;
static uint8_t  levelCountBreakout;
static uint16_t tone;
static uint16_t score;
static int16_t  ballCount;
static char     str[12];
static uint8_t  kbdPrev, kbdCur;

static Brick  brick[BRICK_NUMBER];
static Racket racket;
static Ball   ball;

static const uint8_t BRICK_ANIM_PATTERNS[4] =
    {0b00110001, 0b00101001, 0b00100101, 0b00100011};

static void srand_custom(uint32_t seed) { randSeed = seed ? seed : 1; }
static int  rand_custom(void) { randSeed = randSeed * 1103515245u + 12345u; return (randSeed >> 16) & 0x7FFF; }
static int  randInt(int min, int max) { return min + (rand_custom() % (max - min + 1)); }

static void reset(void) { ballCount = BALL_NUMBER; levelCountBreakout = 1; score = 0; }

static void playBeep(uint16_t t) { A->play_tone(t, 100); }

static void drawScore(void) {
    A->status_clear();
    u2str(str, "Level ", levelCountBreakout, 2); A->print_tiny(str, 0,  1, true, true);
    u2str(str, "Ball ",  (ballCount < 0) ? 0 : ballCount, 2); A->print_tiny(str, 45, 1, true, true);
    u2str(str, "Score ", score, 4); A->print_tiny(str, 88, 1, true, true);
}

static void renderBall(bool state) {
    A->draw_rect(A->fb, ball.x, ball.y, ball.x + BALL_WIDTH - 1, ball.y + BALL_HEIGHT - 1, state);
    A->draw_line(A->fb, ball.x - 1, ball.y + 1, ball.x + BALL_WIDTH, ball.y + 1, state);
}

static void initBall(void) { ball.x = 62; ball.y = 30; ball.dx = 0; ball.dy = 1; renderBall(true); }

static void directionBall(int16_t x, uint8_t w, int8_t num) {
    ball.dx = (int16_t)(x + w - ball.x) * (-num - num) / w + num;
    ball.dy *= -1;
}

static void initWall(void) {
    Brick *current = brick;
    for (uint8_t y = 0; y < 24; y += 8)
        for (uint8_t x = 6; x < 126; x += 20) {
            current->x = x; current->y = y; current->destroy = false; current++;
        }
}

static void initRacket(void);

static void drawBall(void) {
    renderBall(false);
    ball.x += ball.dx; ball.y += ball.dy;

    if (ball.y <= 0)        { ball.dx = randInt(-3, 3); ball.dy = 1; }
    else if (ball.x <= 2)   { ball.x = 2;   ball.dx = iabs(ball.dx); }
    else if (ball.x >= 124) { ball.x = 124; ball.dx = -iabs(ball.dx); }

    if (ball.y == 47) {
        if (ball.x + 1 >= racket.x && ball.x - 1 <= racket.x + RACKET_WIDTH) {
            directionBall(racket.x, RACKET_WIDTH, 3); tone = 400;
        }
    } else if (ball.y > 49) {
        ballCount--;
        A->display_clear();
        drawScore();
        tone = 800;
        if (ballCount < 0) {
            reset(); initWall(); /* drawWall drawn by main loop */
            isPaused = true;
            A->print_bold("GAME OVER", 0, LCD_WIDTH - 1, 4);
        }
        initRacket(); initBall();
    }
    renderBall(true);
}

static void drawWall(void) {
    for (uint8_t i = 0; i < BRICK_NUMBER; i++) {
        if (brick[i].destroy) continue;
        uint8_t *fb_ptr = A->fb[brick[i].y / 8] + brick[i].x;
        fb_ptr[0]  = 0b00011110;
        fb_ptr[14] = 0b00011110;

        if ((ball.x + 1 >= brick[i].x && ball.x - 1 <= brick[i].x + BRICK_WIDTH) &&
            (ball.y + 1 >= brick[i].y && ball.y - 1 <= brick[i].y + BRICK_HEIGHT)) {
            brick[i].destroy = true; score++;
            directionBall(brick[i].x, BRICK_WIDTH, 2);

            A->led(true);
            memset(fb_ptr + 1, 0b00111111, 13);
            A->blit_line(brick[i].y / 8);
            playBeep(600);
            memset(fb_ptr + 0, 0b00000000, 15);
            A->blit_line(brick[i].y / 8);
            A->led(false);

            if (score % BRICK_NUMBER == 0) {
                levelCountBreakout++; ballCount++; initWall(); return;
            }
        } else {
            for (uint8_t k = 0; k < 13; k++)
                fb_ptr[k + 1] = BRICK_ANIM_PATTERNS[(blockAnim + k) % 4];
        }
    }
}

static void renderRacket(int x, bool state) {
    A->draw_rect(A->fb, x + 1, RACKET_Y, x + RACKET_WIDTH - 2, RACKET_Y + RACKET_HEIGHT, state);
    A->draw_line(A->fb, x, RACKET_Y + 1, x + RACKET_WIDTH - 1, RACKET_Y + 1, state);
}

static void initRacket(void) { racket.x = 64 - (RACKET_WIDTH / 2); racket.p = racket.x; renderRacket(racket.x, true); }

static void drawRacket(void) {
    if (racket.p != racket.x) {
        renderRacket(racket.p, false);
        racket.p = racket.x;
        renderRacket(racket.x, true);
    }
}

static void OnKeyDown(uint8_t key) {
    bool wasPaused = isPaused;
    switch (key) {
    case APP_KEY_4:
    case APP_KEY_UP:   if (!isPaused && racket.x > 0)   racket.x -= 2; isPaused = false; break;
    case APP_KEY_0:
    case APP_KEY_DOWN: if (!isPaused && racket.x < 102) racket.x += 2; isPaused = false; break;
    case APP_KEY_MENU:
        isPaused = !isPaused;
        if (isPaused) A->print_bold("PAUSE", 0, LCD_WIDTH - 1, 4);
        break;
    case APP_KEY_EXIT: isPaused = false; isInitialized = false; break;
    }
    if (wasPaused && !isPaused)
        for (uint8_t i = 0; i < 8; i++)
            A->draw_line(A->fb, 32, 32 + i, 96, 32 + i, false);
}

static void handleInput(void) {
    kbdPrev = kbdCur;
    kbdCur = A->get_key();
    if (kbdCur == APP_KEY_INVALID) return;
    if (kbdCur == APP_KEY_UP || kbdCur == APP_KEY_DOWN ||
        kbdCur == APP_KEY_4  || kbdCur == APP_KEY_0    || kbdCur != kbdPrev)
        OnKeyDown(kbdCur);
}

/* ---- entry point, pinned to blob offset 0 ---- */
__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api) {
    A = api;

#ifdef APP_POC_HELLO
    /* Minimal bisection test: prove jump + ABI + return work, no game body. */
    A->display_clear();
    A->print_bold("OVL HELLO", 0, LCD_WIDTH - 1, 3);
    A->blit_full();
    A->delay_ms(2000);
    return;
#endif

    /* reset all state (the overlay is not persistent across launches) */
    blockAnim = 0; isPaused = false; tone = 0; score = 0;
    kbdPrev = APP_KEY_INVALID; kbdCur = APP_KEY_INVALID;
    uint8_t swap = 0;

    /* The low BK4819 counter bits vary continuously; mix them with the tuned
     * frequency instead of carrying a launch-only seed field in every ABI
     * table. */
    srand_custom(((uint32_t)A->bk_read(0x67u) << 16) ^ A->rx_freq());
    A->led(false);
    A->backlight_on();

    A->display_clear();
    reset(); initWall(); initRacket(); initBall();
    A->status_clear();
    isInitialized = true;

    while (isInitialized) {
        handleInput();
        if (!isPaused) {
            if (swap == 0) blockAnim = (blockAnim + 1) % 4;
            swap = (swap + 1) % 4;
            drawScore(); drawWall(); drawRacket(); drawBall();
            if (tone != 0) { playBeep(tone); tone = 0; }
            else A->delay_ms(40 - imin(levelCountBreakout - 1, 20));
        }
        A->blit_status();
        A->blit_full();
    }
}
