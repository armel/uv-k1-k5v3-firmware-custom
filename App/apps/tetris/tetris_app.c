/* Copyright 2026 Armel F4HWN
 * https://github.com/armel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Tetris — compact overlay game for the 128x64 monochrome LCD.
 *
 * The 16x16 visible well fills the screen height down the left side; two hidden
 * spawn rows sit just above it.  Rather than solid walls, the well is framed by
 * short corner brackets and a floor line.  The narrow right-hand panel shows the
 * next piece, score, cleared lines, level and persistent best score.  Pieces
 * come from a shuffled seven-piece bag; a dotted ghost previews the landing.
 *
 * Keys: LEFT/RIGHT or 4/6 move, MENU or 2 rotates, 8 soft-drops,
 * STAR or 0 hard-drops, F pauses and EXIT quits.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"

#define W             128u
#define H              64u
#define COLS           16
#define VISIBLE_ROWS   16
#define HIDDEN_ROWS     2
#define ROWS            (VISIBLE_ROWS + HIDDEN_ROWS)
#define CELL            4
#define WELL_X          3       /* physical x of board column 0              */
#define WELL_Y          0       /* physical y of visible row 0 (full height) */
#define WELL_L          1       /* container left rail  (corner brackets)    */
#define WELL_R         67       /* container right rail (corner brackets)    */
#define WELL_B         63       /* solid floor line, physical y              */
#define LABEL_X        72       /* left column shared by every panel label   */
#define PREVIEW_CX    108       /* centre of the NEXT-piece preview area      */
#define PREVIEW_CY     15
#define TICK_MS        20u
#define CFG_MAGIC  0x5445u

typedef struct {
    uint16_t magic;
    uint16_t version;
    uint32_t best;
} config_t;

static const app_api_t *A;

/* GCC may lower aggregate clears to memset even for this freestanding blob. */
void *memset(void *dst, int value, size_t size)
{
    uint8_t *p = dst;
    while (size--)
        *p++ = (uint8_t)value;
    return dst;
}

/* Four 4x4 masks for I, O, T, S, Z, J and L. */
static const uint16_t MASK[7][4] = {
    {0x00F0, 0x4444, 0x0F00, 0x2222},
    {0x0066, 0x0066, 0x0066, 0x0066},
    {0x0072, 0x0262, 0x0270, 0x0232},
    {0x0036, 0x0462, 0x0360, 0x0231},
    {0x0063, 0x0264, 0x0630, 0x0132},
    {0x0071, 0x0226, 0x0470, 0x0322},
    {0x0074, 0x0622, 0x0170, 0x0223},
};

static const uint16_t LINE_POINTS[5] = {0, 100, 300, 500, 800};

static uint8_t board[ROWS][COLS];
static uint8_t bag[7], bagPos;
static uint8_t piece, nextPiece, rotation;
static int8_t  pieceX, pieceY;
static uint32_t randomState, score, best;
static uint16_t lines, gravityMs;
static uint8_t level;
static bool paused, gameOver, running, saverPaused, saverActive, bestDirty, newBest;
static uint8_t previousKey;
static uint16_t repeatMs;
static char text[9];

static void render(bool showPiece, uint32_t clearRows, uint8_t effect);
static void new_game(void);

/* -------------------------------------------------------------------------- */
/*  RNG and the shuffled 7-piece bag                                          */
/* -------------------------------------------------------------------------- */

static uint32_t random_next(void)
{
    randomState = randomState * 1664525u + 1013904223u;
    return randomState;
}

static void refill_bag(void)
{
    for (uint8_t i = 0; i < 7u; i++)
        bag[i] = i;
    for (uint8_t i = 6u; i > 0u; i--) {
        const uint8_t j = (uint8_t)(random_next() % (uint32_t)(i + 1u));
        const uint8_t t = bag[i]; bag[i] = bag[j]; bag[j] = t;
    }
    bagPos = 0;
}

static uint8_t take_piece(void)
{
    if (bagPos >= 7u)
        refill_bag();
    return bag[bagPos++];
}

/* -------------------------------------------------------------------------- */
/*  Framebuffer drawing: cells, pieces, side panel and dialogs                */
/* -------------------------------------------------------------------------- */

static void set_pixel(int x, int y, bool on)
{
    if ((unsigned)x >= W || (unsigned)y >= H)
        return;
    uint8_t *p;
    if (y < 8)
        p = &A->status_line[x];
    else
        p = &A->fb[(y >> 3) - 1][x];
    const uint8_t bit = (uint8_t)(1u << (y & 7));
    if (on) *p |= bit; else *p &= (uint8_t)~bit;
}

/* Short straight runs, in physical coordinates so they may cross the
 * status-line / framebuffer seam that A->draw_line cannot. */
static void hline(int x0, int x1, int y)
{
    for (int x = x0; x <= x1; x++) set_pixel(x, y, true);
}

static void vline(int x, int y0, int y1)
{
    for (int y = y0; y <= y1; y++) set_pixel(x, y, true);
}

/* Corner brackets plus a solid floor — the well's frame, no vertical walls. */
static void container(void)
{
    hline(WELL_L, WELL_R, WELL_B);
    hline(WELL_L, WELL_L + 5, WELL_Y);
    hline(WELL_R - 5, WELL_R, WELL_Y);
    vline(WELL_L, WELL_Y, WELL_Y + 5);
    vline(WELL_R, WELL_Y, WELL_Y + 5);
    vline(WELL_L, WELL_B - 5, WELL_B);
    vline(WELL_R, WELL_B - 5, WELL_B);
}

static void cell(int gx, int gy, uint8_t style)
{
    const int visibleY = gy - HIDDEN_ROWS;
    if ((unsigned)gx >= COLS || (unsigned)visibleY >= VISIBLE_ROWS)
        return;
    const int x = WELL_X + gx * CELL;
    const int y = WELL_Y + visibleY * CELL;
    if (style == 2u) {
        set_pixel(x, y + 1, true);
        set_pixel(x + 1, y, true);
        set_pixel(x + 2, y + 1, true);
        set_pixel(x + 1, y + 2, true);
    } else {
        const uint8_t size = style == 3u ? CELL : CELL - 1u;
        for (uint8_t dy = 0; dy < size; dy++)
            for (uint8_t dx = 0; dx < size; dx++)
                set_pixel(x + dx, y + dy, true);
    }
}

static bool collision(uint8_t type, uint8_t rot, int8_t px, int8_t py)
{
    const uint16_t mask = MASK[type][rot & 3u];
    for (uint8_t i = 0; i < 16u; i++) {
        if (!(mask & (uint16_t)(1u << i)))
            continue;
        const int x = px + (i & 3u);
        const int y = py + (i >> 2);
        if (x < 0 || x >= COLS || y >= ROWS)
            return true;
        if (y >= 0 && board[y][x])
            return true;
    }
    return false;
}

static void draw_piece(uint8_t type, uint8_t rot, int8_t px, int8_t py, uint8_t style)
{
    const uint16_t mask = MASK[type][rot & 3u];
    for (uint8_t i = 0; i < 16u; i++)
        if (mask & (uint16_t)(1u << i))
            cell(px + (i & 3u), py + (i >> 2), style);
}

static void number(uint32_t value, uint8_t width)
{
    text[width] = 0;
    while (width) {
        text[--width] = (char)('0' + value % 10u);
        value /= 10u;
    }
}

static void preview(void)
{
    const uint16_t mask = MASK[nextPiece][0];
    uint8_t minX = 3u, maxX = 0u, minY = 3u, maxY = 0u;
    for (uint8_t i = 0; i < 16u; i++) {
        if (!(mask & (uint16_t)(1u << i)))
            continue;
        const uint8_t x = i & 3u, y = i >> 2;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
    const int width = (maxX - minX + 1u) * CELL - 1u;
    const int height = (maxY - minY + 1u) * CELL - 1u;
    const int ox = PREVIEW_CX - width / 2 - minX * CELL;
    const int oy = PREVIEW_CY - height / 2 - minY * CELL;
    for (uint8_t i = 0; i < 16u; i++) {
        if (!(mask & (uint16_t)(1u << i)))
            continue;
        const int x = ox + (i & 3u) * CELL;
        const int y = oy + (i >> 2) * CELL;
        for (uint8_t dy = 0; dy < CELL - 1u; dy++)
            for (uint8_t dx = 0; dx < CELL - 1u; dx++)
                set_pixel(x + dx, y + dy, true);
    }
}

/* One "LABEL      value" row of the side panel; y is framebuffer-relative
 * (physical y - 8), value right-aligned near the screen edge. */
static void stat(const char *label, uint8_t y, uint32_t value,
                 uint8_t valueX, uint8_t width)
{
    A->print_tiny(label, LABEL_X, y, false, true);
    number(value, width);
    A->print_tiny(text, valueX, y, false, true);
}

static void panel(void)
{
    A->print_tiny("NEXT", LABEL_X, 2, false, true);
    preview();
    if (gameOver)
        A->print_inverse(newBest ? "NEW BEST!" : "GAME OVER", 82, 2, false, true, 118);
    else if (paused)
        A->print_inverse("PAUSE", 90, 2, false, true, 110);
    stat("SCORE", 24, score > 999999u ? 999999u : score, 101, 6);
    stat("LINES", 32, lines >    999u ? 999u    : lines, 113, 3);
    stat("LEVEL", 40, level,                             117, 2);
    stat("BEST",  48, best  > 999999u ? 999999u : best,  101, 6);
}

/* effect 0 is normal, 1..8 is the line-clear sweep, 0xFF flashes a lock. */
static void render(bool showPiece, uint32_t clearRows, uint8_t effect)
{
    A->display_clear();
    A->status_clear();
    A->print_inverse("TETRIS", 74, 0, true, true, 101);

    container();

    for (uint8_t y = 0; y < ROWS; y++)
        for (uint8_t x = 0; x < COLS; x++)
            if (board[y][x] && (!(clearRows & (1u << y)) ||
                (x >= effect && x < COLS - effect)))
                cell(x, y, 1u);

    if (showPiece && !gameOver) {
        int8_t ghostY = pieceY;
        while (!collision(piece, rotation, pieceX, ghostY + 1))
            ghostY++;
        if (ghostY != pieceY)
            draw_piece(piece, rotation, pieceX, ghostY, 2u);
        draw_piece(piece, rotation, pieceX, pieceY, effect == 0xFFu ? 3u : 1u);
    }

    panel();
}

/* -------------------------------------------------------------------------- */
/*  Board mechanics: scoring, line clears, spawn, lock and moves              */
/* -------------------------------------------------------------------------- */

static void update_best(void)
{
    if (score > best) {
        best = score;
        bestDirty = true;
        newBest = true;
    }
}

static uint8_t clear_lines(void)
{
    uint32_t full = 0;
    uint8_t count = 0;
    for (uint8_t y = 0; y < ROWS; y++) {
        bool complete = true;
        for (uint8_t x = 0; x < COLS; x++)
            if (!board[y][x]) { complete = false; break; }
        if (complete) { full |= 1u << y; count++; }
    }
    if (!count)
        return 0;

    A->led(true);
    for (uint8_t step = 1u; step <= COLS / 2u; step++) {
        render(false, full, step);
        A->blit_status(); A->blit_full(); A->delay_ms(28);
    }
    A->led(false);

    int8_t dst = ROWS - 1;
    for (int8_t src = ROWS - 1; src >= 0; src--) {
        if (full & (1u << src))
            continue;
        if (dst != src)
            for (uint8_t x = 0; x < COLS; x++)
                board[dst][x] = board[src][x];
        dst--;
    }
    while (dst >= 0) {
        for (uint8_t x = 0; x < COLS; x++) board[dst][x] = 0;
        dst--;
    }
    return count;
}

static void spawn_piece(void)
{
    piece = nextPiece;
    nextPiece = take_piece();
    rotation = 0;
    pieceX = COLS / 2 - 2;
    pieceY = HIDDEN_ROWS - 1;
    if (collision(piece, rotation, pieceX, pieceY)) {
        gameOver = true;
        update_best();
    }
}

static void lock_piece(void)
{
    render(true, 0, 0xFFu);
    A->blit_status(); A->blit_full(); A->delay_ms(45);

    const uint16_t mask = MASK[piece][rotation];
    bool above = false;
    for (uint8_t i = 0; i < 16u; i++) {
        if (!(mask & (uint16_t)(1u << i)))
            continue;
        const int x = pieceX + (i & 3u);
        const int y = pieceY + (i >> 2);
        if (y < HIDDEN_ROWS) above = true;
        else board[y][x] = (uint8_t)(piece + 1u);
    }
    if (above) {
        gameOver = true;
        update_best();
        return;
    }

    const uint8_t cleared = clear_lines();
    lines = (uint16_t)(lines + cleared);
    level = (uint8_t)(lines / 10u + 1u);
    if (level > 15u) level = 15u;
    score += (uint32_t)LINE_POINTS[cleared] * level;
    update_best();
    spawn_piece();
}

static bool move_down(bool manual)
{
    if (!collision(piece, rotation, pieceX, pieceY + 1)) {
        pieceY++;
        if (manual) { score++; update_best(); }
        return true;
    }
    lock_piece();
    return false;
}

static void rotate_piece(void)
{
    const uint8_t next = (uint8_t)((rotation + 1u) & 3u);
    static const int8_t kick[5] = {0, -1, 1, -2, 2};
    for (uint8_t i = 0; i < 5u; i++)
        if (!collision(piece, next, pieceX + kick[i], pieceY)) {
            pieceX += kick[i]; rotation = next; return;
        }
    if (!collision(piece, next, pieceX, pieceY - 1)) {
        pieceY--; rotation = next;
    }
}

static void hard_drop(void)
{
    uint8_t distance = 0;
    while (!collision(piece, rotation, pieceX, pieceY + 1)) {
        pieceY++; distance++;
    }
    score += (uint32_t)distance * 2u;
    update_best();
    lock_piece();
}

/* -------------------------------------------------------------------------- */
/*  Input, screensaver handling and the main loop                            */
/* -------------------------------------------------------------------------- */

static void key_action(uint8_t key, bool repeat)
{
    if (key == APP_KEY_EXIT) { running = false; return; }
    if (gameOver) {
        if (!repeat && (key == APP_KEY_MENU || key == APP_KEY_STAR || key == APP_KEY_0))
            new_game();
        return;
    }
    if (!repeat && key == APP_KEY_F) { paused = !paused; return; }
    if (paused)
        return;

    int8_t direction = 0;
    if (key == APP_KEY_4) direction = -1;
    else if (key == APP_KEY_6) direction = 1;
    else if (key == APP_KEY_UP || key == APP_KEY_DOWN) direction = A->nav_dir(key);
    if (direction) {
        if (!collision(piece, rotation, pieceX + direction, pieceY))
            pieceX += direction;
        return;
    }
    if (key == APP_KEY_8) { move_down(true); return; }
    if (repeat)
        return;
    if (key == APP_KEY_MENU || key == APP_KEY_2) rotate_piece();
    else if (key == APP_KEY_STAR || key == APP_KEY_0) hard_drop();
}

static void poll_key(void)
{
    uint8_t key = A->get_key();
    if (key == APP_KEY_SAVER) {
        if (!paused && !gameOver) { paused = true; saverPaused = true; }
        saverActive = true;
        previousKey = APP_KEY_INVALID;
        repeatMs = 0;
        return;
    }
    if (key == APP_KEY_WAKE || key == APP_KEY_PTT) {
        if (saverPaused) paused = false;
        saverPaused = false;
        saverActive = false;
        key = APP_KEY_INVALID;
    }
    if (key == APP_KEY_INVALID) {
        previousKey = key;
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
    previousKey = key;
}

static void new_game(void)
{
    for (uint8_t y = 0; y < ROWS; y++)
        for (uint8_t x = 0; x < COLS; x++)
            board[y][x] = 0;
    score = 0;
    lines = 0;
    level = 1;
    gravityMs = 0;
    paused = false;
    gameOver = false;
    saverPaused = false;
    saverActive = false;
    newBest = false;
    bagPos = 7;
    nextPiece = take_piece();
    spawn_piece();
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    config_t cfg;
    A->cfg_load((uint8_t *)&cfg, sizeof(cfg));
    best = cfg.magic == CFG_MAGIC && cfg.version == 1u ? cfg.best : 0u;
    bestDirty = false;

    randomState = ((uint32_t)A->bk_read(0x67u) << 16) ^ A->rx_freq() ^ best;
    if (!randomState) randomState = 1;
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
            const uint16_t interval = level >= 15u ? 100u : (uint16_t)(900u - (level - 1u) * 55u);
            gravityMs = (uint16_t)(gravityMs + TICK_MS);
            if (gravityMs >= interval) {
                gravityMs = 0;
                move_down(false);
            }
        }

        render(true, 0, 0);
        A->blit_status();
        A->blit_full();
        A->backlight_update();
        A->delay_ms(TICK_MS);
    }

    update_best();
    if (bestDirty) {
        cfg.magic = CFG_MAGIC;
        cfg.version = 1u;
        cfg.best = best;
        A->cfg_save((const uint8_t *)&cfg, sizeof(cfg));
    }
    A->led(false);
}
