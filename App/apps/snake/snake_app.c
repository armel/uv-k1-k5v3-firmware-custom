/* Copyright 2026 mrkusypl    https://github.com/mrkusypl
 *                Armel F4HWN https://github.com/armel
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
 * Snake - compact overlay game for the 128x64 monochrome LCD.
 *
 * The playfield is a large 31x13 grid covering most of the display,
 * with game status displayed in a single line at the top.
 * Features connected snake body segments in classic Nokia 3310 style.
 *
 * Controls: 2 move up, 4 move left, 6 move right, 8 move down
 * MENU / STAR restarts after GAME OVER, F pauses and EXIT quits
 */

#include <stdint.h>
#include <stdbool.h>
#include "../app_api.h"

#define W                 128u

#define COLS              31u
#define ROWS              13u

/* Board content coordinates */
#define BOARD_X           2u
#define BOARD_Y           10u

#define TICK_MS           20u
#define RING_SIZE         512u /* Power of 2 for zero-branch modulo */
#define RING_MASK         511u
#define MOVE_INTERVAL     120u
#define SCORE_PER_FOOD    10u
#define CFG_MAGIC         0x534Eu

typedef struct {
    uint16_t magic;
    uint16_t version;
    uint32_t best;
} config_t;

typedef struct {
    uint8_t x;
    uint8_t y;
} segment_t;

static const app_api_t *A;

static uint32_t board_map[ROWS];
static uint32_t randomState, best, initial_best;
static segment_t snake[RING_SIZE];
static uint16_t head_idx, tail_idx, repeatMs;
static int16_t moveTimer;
static uint8_t foodX, foodY, direction, nextDirection, previousKey;
static bool paused, gameOver, running, newBest, saverPaused, saverActive;
static char text[6];

static const int8_t DX[4] = { 0, 1, 0, -1 };
static const int8_t DY[4] = {-1, 0, 1,  0 };

/* --- Branchless Direction Map --- */
static const uint8_t DIR_MASK[8] = { [6]=1, [1]=2, [2]=4, [7]=8 };

/* Macro for compact direction evaluation */
#define GET_DIR(n_idx, s_x, s_y) DIR_MASK[(((snake[n_idx].x & 0x7F) - (s_x)) + ((snake[n_idx].y - (s_y)) << 1)) & 7]

/* --- Sprites --- */
static const uint16_t HEAD_SPRITE[4]      = { 0x6566, 0x0FD2, 0x66A6, 0x0FB4 };
static const uint16_t HEAD_OPEN_SPRITE[4] = { 0x65A9, 0xC35A, 0x95A6, 0x3CA5 };
static const uint16_t BODY_SPRITE[16]     = {
    0, 0, 0, 0x0CA6, 0, 0x6426, 0x6AC0, 0, 0, 0x0356, 0x0DB0, 0, 0x6530, 0, 0, 0
};
static const uint16_t TAIL_SPRITE[16]     = {
    0, 0x0466, 0x0CE0, 0, 0x6620, 0, 0, 0, 0x0370, 0, 0, 0, 0, 0, 0, 0
};

static void new_game(void);

/* -------------------------------------------------------------------------- */
/* Utilities & RNG                                                            */
/* -------------------------------------------------------------------------- */

static uint32_t random_next(void)
{
    randomState = randomState * 1664525u + 1013904223u;
    return randomState;
}

static void place_food(void)
{
    for (uint32_t tries = 0; tries < 128u; tries++) {
        const uint32_t r = random_next();
        const uint32_t x = ((r >> 24) * COLS) >> 8;
        const uint32_t y = (((r >> 16) & 0xFFu) * ROWS) >> 8;
        
        if (!(board_map[y] & (1UL << x))) { 
            foodX = (uint8_t)x;
            foodY = (uint8_t)y;
            return;
        }
    }
    for (uint32_t y = 0; y < ROWS; y++) {
        for (uint32_t x = 0; x < COLS; x++) {
            if (!(board_map[y] & (1UL << x))) {
                foodX = (uint8_t)x;
                foodY = (uint8_t)y;
                return;
            }
        }
    }
    gameOver = true;
}

/* -------------------------------------------------------------------------- */
/* Hardware Rendering                                                         */
/* -------------------------------------------------------------------------- */

static void draw_sprite(uint32_t bx, uint32_t by, uint32_t sprite)
{
    for (uint32_t i = 0; sprite; i++, sprite >>= 1) {
        if (sprite & 1u) {
            uint32_t py = by + (i >> 2u);
            A->fb[(py >> 3) - 1u][bx + (i & 3u)] |= (1u << (py & 7u));
        }
    }
}

static void draw_snake(void)
{
    uint32_t idx = head_idx;
    uint32_t next_idx = 0;
    uint32_t len = (head_idx - tail_idx + 1u) & RING_MASK;
    
    for (uint32_t i = 0; i < len; i++) {
        uint32_t sx = snake[idx].x & 0x7F;
        uint32_t sy = snake[idx].y;
        uint32_t sprite = 0;

        if (i == 0) {
            bool open = ((sx + DX[direction]) == foodX && (sy + DY[direction]) == foodY);
            sprite = open ? HEAD_OPEN_SPRITE[direction] : HEAD_SPRITE[direction];
        } else if (snake[idx].x & 0x80) {
            sprite = 0x6FF6; 
        } else if (i == len - 1u) {
            sprite = TAIL_SPRITE[GET_DIR(next_idx, sx, sy)];
        } else {
            uint32_t prev_idx = (idx - 1u) & RING_MASK;
            sprite = BODY_SPRITE[GET_DIR(next_idx, sx, sy) | GET_DIR(prev_idx, sx, sy)];
        }

        draw_sprite(BOARD_X + (sx << 2), BOARD_Y + (sy << 2), sprite);
        next_idx = idx;
        idx = (idx - 1u) & RING_MASK;
    }
}

static void number_5(uint32_t value)
{
    static const uint16_t place[4] = { 10000u, 1000u, 100u, 10u };
    if (value > 99999u)
        value = 99999u;

    for (uint32_t i = 0; i < 4u; i++) {
        uint32_t p = place[i];
        uint32_t digit = '0';

        while (value >= p) {
            value -= p;
            digit++;
        }

        text[i] = (char)digit;
    }

    text[4] = (char)('0' + value);
    text[5] = '\0';
}

static void render(void)
{
    A->display_clear();
    A->status_clear();

    A->draw_rect(A->fb, 1, 1, 126, 54, true);

    if (!gameOver)
        draw_sprite(BOARD_X + (foodX << 2), BOARD_Y + (foodY << 2), 0x6996);

    draw_snake();

    /* Score computed dynamically based on current length */
    A->print_tiny("SCORE:", 2, 1, true, true);
    number_5((((head_idx - tail_idx + 1u) & RING_MASK) - 3u) * SCORE_PER_FOOD);
    A->print_tiny(text, 32, 1, true, true);

    A->print_tiny("BEST:", 81, 1, true, true);
    number_5(best);
    A->print_tiny(text, 107, 1, true, true);

    if (gameOver)
        A->print_bold(newBest ? "NEW SCORE" : "GAME OVER", 0, W - 1, 3);
    else if (paused)
        A->print_bold("PAUSE", 0, W - 1, 3);
}

/* -------------------------------------------------------------------------- */
/* Game Logic                                                                 */
/* -------------------------------------------------------------------------- */

static void move_snake(void)
{
    /* Casting to unsigned handles both upper limit and negative underflow in one instruction */
    uint32_t nx = (snake[head_idx].x & 0x7F) + DX[nextDirection];
    uint32_t ny = snake[head_idx].y + DY[nextDirection];

    if (nx >= COLS || ny >= ROWS) {
        gameOver = true;
        return;
    }

    bool ate = (nx == foodX && ny == foodY);
    uint32_t tail_x = snake[tail_idx].x & 0x7F;
    uint32_t tail_y = snake[tail_idx].y;

    if (!ate)
        board_map[tail_y] &= ~(1UL << tail_x);

    if (board_map[ny] & (1UL << nx)) {
        if (!ate)
            board_map[tail_y] |= (1UL << tail_x);

        gameOver = true;
        return;
    }

    direction = nextDirection;
    board_map[ny] |= (1UL << nx);

    uint32_t new_head = (head_idx + 1u) & RING_MASK;
    
    if (!ate)
        tail_idx = (tail_idx + 1u) & RING_MASK;

    snake[new_head].x = (uint8_t)(nx | (ate ? 0x80 : 0));
    snake[new_head].y = (uint8_t)ny;
    head_idx = (uint16_t)new_head;

    if (ate) {
        uint32_t new_score = (((new_head - tail_idx + 1u) & RING_MASK) - 3u) * SCORE_PER_FOOD;
        if (new_score > best) {
            best = new_score;
            newBest = true;
        }
        place_food();
    }
}

/* -------------------------------------------------------------------------- */
/* Input & Loop                                                               */
/* -------------------------------------------------------------------------- */

static void key_action(uint32_t key, bool repeat)
{
    if (key == APP_KEY_EXIT) {
        running = false;
        return;
    }

    if (gameOver) {
        if (!repeat && (key == APP_KEY_MENU || key == APP_KEY_STAR || key == APP_KEY_0))
            new_game();
        return;
    }

    if (!repeat && key == APP_KEY_F) {
        paused = !paused;
        return;
    }

    if (paused)
        return;

    uint32_t newDir = 4;
    if      (key == APP_KEY_2 || key == APP_KEY_3)   newDir = 0u;
    else if (key == APP_KEY_6 || key == APP_KEY_0)   newDir = 1u;
    else if (key == APP_KEY_8 || key == APP_KEY_9)   newDir = 2u;
    else if (key == APP_KEY_4 || key == APP_KEY_5)   newDir = 3u;

    if (newDir < 4 && (((direction + 2u) & 3u) != newDir)) 
        nextDirection = (uint8_t)newDir;
}

static void poll_key(void)
{
    uint8_t key = A->get_key();

    if (key == APP_KEY_SAVER) {
        if (!paused && !gameOver) {
            paused = true;
            saverPaused = true;
        }
        saverActive = true;
        previousKey = APP_KEY_INVALID;
        repeatMs = 0;
        return;
    }

    if (key == APP_KEY_WAKE || key == APP_KEY_PTT) {
        if (saverPaused)
            paused = false;
        saverPaused = false;
        saverActive = false;
        key = APP_KEY_INVALID;
    }

    if (key == APP_KEY_INVALID) {
        previousKey = APP_KEY_INVALID;
        repeatMs = 0;
        return;
    }

    if (key != previousKey) {
        key_action(key, false);
        repeatMs = 260u;
    } else if (repeatMs > TICK_MS) {
        repeatMs -= TICK_MS;
    } else {
        key_action(key, true);
        repeatMs = 80u;
    }
    previousKey = (uint8_t)key;
}

static void new_game(void)
{
    head_idx = 2;
    tail_idx = 0;

    for (uint32_t i = 0; i < ROWS; i++)
        board_map[i] = 0;
    
    for (uint32_t i = 0; i < 3; i++) {
        snake[i].x = 13u + i;
        snake[i].y = 6u;
        board_map[6] |= (1UL << (13u + i));
    }

    direction = 1u;
    nextDirection = 1u;
    moveTimer = MOVE_INTERVAL;

    paused =      false;
    gameOver =    false;
    newBest =     false;
    saverPaused = false;
    saverActive = false;

    place_food();
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    config_t cfg;

    A->cfg_load((uint8_t *)&cfg, sizeof(cfg));
    best = (cfg.magic == CFG_MAGIC && cfg.version == 1u) ? cfg.best : 0u;
    initial_best = best;

    randomState = ((uint32_t)A->bk_read(0x67u) << 16) ^ A->rx_freq() ^ best ^ 0x534E414Bu;
    if (!randomState)
         randomState = 1u;

    previousKey = APP_KEY_INVALID;
    repeatMs = 0;
    running = true;

    A->led(false);
    A->backlight_on();

    new_game();

    while (running) {
        poll_key();
        if (!running)
            break;

        if (saverActive) {
            A->backlight_update();
            A->delay_ms(TICK_MS);
            continue;
        }

        if (!paused && !gameOver) {
            if (moveTimer <= 0) {
                moveTimer = MOVE_INTERVAL;
                move_snake();
            } else {
                moveTimer -= (int16_t)TICK_MS;
            }
        }

        render();
        A->blit_status();
        A->blit_full();
        A->backlight_update();
        A->delay_ms(TICK_MS);
    }

    /* Save to NVRAM only if a new record was established compared to boot up */
    if (best > initial_best) {
        cfg.magic = CFG_MAGIC;
        cfg.version = 1u;
        cfg.best = best;
        A->cfg_save((const uint8_t *)&cfg, sizeof(cfg));
    }

    A->led(false);
}
