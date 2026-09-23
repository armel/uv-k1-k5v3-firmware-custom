/* Copyright 2026 Armel F4HWN
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

/*
 * Space Impact-inspired scrolling shooter for the 128x64 monochrome LCD.
 *
 * The keypad reports a single key at a time, so the gun fires automatically
 * and the keys are free for movement.
 *
 * Controls: 2/8 or UP/DOWN move the ship, 5/MENU launches a piercing
 * missile, F pauses and EXIT quits.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"

#define W              128
#define TICK_MS         40u
#define TOP              9      /* first playfield row, below the HUD     */
#define PX               6      /* the ship only moves vertically         */
#define Y_MAX           54      /* lowest sprite row, just above the hills */
#define N_ENEMY          6u
#define N_SHOT           5u
#define N_HOSTILE        6u
#define LEVEL_LEN      700u     /* ticks of waves before the boss         */
#define BOOM            10u     /* explosion length in ticks              */

enum { ST_PLAY, ST_OVER };

/* Sprite offsets in ART[] (column-major, LSB = top row). */
#define SPR_SHIP     0u
#define SPR_ENEMY   10u         /* + type * 8              */
#define SPR_BOOM    42u         /* + frame * 8             */
#define SPR_BOSS    58u         /* octopus, then mothership (+32) */
#define SPR_HEART  122u
#define SPR_MISS   127u         /* 8x5, also the HUD stock icon */

typedef struct { int16_t x; int8_t y, vx, vy; uint8_t live; } shot_t;
/* state: 0 free, 1 alive, >1 exploding while the counter decreases. */
typedef struct { int16_t x; int8_t y, base; uint8_t type, hp, state, phase; } enemy_t;

static const uint8_t ART[] = {
    /* ship    @  0 */ 0x41,0x6B,0x7F,0x3E,0x3E,0x1C,0x1C,0x1C,0x08,0x08,
    /* invader @ 10 */ 0x4C,0x32,0x15,0x21,0x21,0x15,0x32,0x4C,
    /* jet     @ 18 */ 0x08,0x14,0x14,0x14,0x22,0x41,0x49,0x77,
    /* saucer  @ 26 */ 0x0C,0x56,0x3D,0x15,0x15,0x3D,0x56,0x0C,
    /* skull   @ 34 */ 0x0E,0x11,0x6D,0x41,0x41,0x6D,0x11,0x0E,
    /* boom    @ 42 */ 0x00,0x00,0x08,0x1C,0x08,0x00,0x00,0x00,
                       0x41,0x04,0x10,0x40,0x02,0x20,0x08,0x41,
    /* octopus @ 58 */ 0x00,0x78,0x84,0x02,0x32,0x31,0x01,0x01,0x01,0x01,0x31,0x32,0x02,0x84,0x78,0x00,
                       0x30,0x0C,0x82,0x71,0x0D,0x03,0xE0,0x1E,0x02,0x00,0x03,0x0D,0x31,0xC2,0x0C,0x30,
    /* mother  @ 90 */ 0xC0,0x20,0x10,0xDC,0xD2,0x12,0x1D,0xD1,0xD1,0x1D,0x12,0xD2,0xDC,0x10,0x20,0xC0,
                       0x20,0x11,0x0A,0x06,0x02,0x02,0x02,0x3E,0x3E,0x02,0x02,0x02,0x06,0x0A,0x11,0x20,
    /* heart   @122 */ 0x06,0x0F,0x1E,0x0F,0x06,
    /* missile @127 */ 0x1B,0x0E,0x04,0x0E,0x0E,0x0E,0x0E,0x04,
};

static const uint8_t POINTS[4] = {10u, 15u, 40u, 25u};
static const uint8_t SPEED[4] = {1u, 2u, 1u, 1u};   /* pixels per tick */

static const app_api_t *A;
static enemy_t enemies[N_ENEMY];
static shot_t shots[N_SHOT], hostile[N_HOSTILE];
static uint32_t randomState, score;
static uint16_t distance;
static uint16_t frame;   /* 16-bit so the slow star layers wrap without a jump */
static uint8_t mode, prevKey, kills;
static uint8_t py, lives, level, missiles;
static uint8_t fireCd, hostileCd, spawnCd, invul, banner, waitCd;
static int16_t bossX;
static int8_t bossY, bossDir;
static uint8_t bossHp, bossMax;
static bool running, paused, boss, saver;
static char text[6];
static char levelText[] = "LEVEL 1";

void *memset(void *dst, int value, size_t size)
{
    uint8_t *p = dst;
    while (size--) *p++ = (uint8_t)value;
    return dst;
}

static uint32_t rnd(void)
{
    randomState = randomState * 1664525u + 1013904223u;
    return randomState >> 8;
}

static void pixel(int16_t x, int16_t y)
{
    if ((uint16_t)x >= W || (uint16_t)y >= 64u) return;
    uint8_t *p = y < 8 ? &A->status_line[x] : &A->fb[(y >> 3) - 1][x];
    *p |= (uint8_t)(1u << (y & 7));
}

/* Draw a column-major sprite (up to 8 rows high, LSB = top). */
static void blit(int16_t x, int16_t y, const uint8_t *col, uint8_t w)
{
    for (; w; w--, x++) {
        uint8_t v = *col++;
        for (int16_t yy = y; v; v >>= 1, yy++)
            if (v & 1u) pixel(x, yy);
    }
}

static bool overlap(int16_t ax, int16_t ay, uint8_t aw, uint8_t ah,
                    int16_t bx, int16_t by, uint8_t bw, uint8_t bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static bool hits_player(int16_t x, int16_t y, uint8_t w, uint8_t h)
{
    return mode == ST_PLAY && overlap(x, y, w, h, PX + 1, py + 1, 8, 5);
}

static int8_t aim(int16_t y)
{
    return y < py - 1 ? 1 : (y > py + 7 ? -1 : 0);
}

static void number(uint32_t value, uint8_t width)
{
    static const uint16_t place[5] = {10000u, 1000u, 100u, 10u, 1u};
    const uint8_t first = (uint8_t)(5u - width);
    for (uint8_t i = 0; i < width; i++) {
        uint8_t digit = 0;
        while (value >= place[first + i]) {
            value -= place[first + i];
            digit++;
        }
        text[i] = (char)('0' + digit);
    }
    text[width] = 0;
}

static bool add_shot(shot_t *pool, uint8_t count, int16_t x, int8_t y,
                     int8_t vx, int8_t vy, uint8_t kind)
{
    for (uint8_t i = 0; i < count; i++) if (!pool[i].live) {
        pool[i].x = x; pool[i].y = y; pool[i].vx = vx; pool[i].vy = vy;
        pool[i].live = kind;
        return true;
    }
    return false;
}

/* Spawn an explosion, `delay` ticks later; recycles the last slot if full. */
static void boom(int16_t x, int8_t y, uint8_t delay)
{
    enemy_t *e = &enemies[N_ENEMY - 1u];
    for (uint8_t i = 0; i < N_ENEMY; i++) if (!enemies[i].state) { e = &enemies[i]; break; }
    e->x = x; e->y = y; e->state = (uint8_t)(BOOM + delay);
}

static void start_level(void)
{
    levelText[6] = (char)('0' + level);
    distance = 0; banner = 50u; spawnCd = 0; boss = false;
}

static void new_game(void)
{
    memset(enemies, 0, sizeof(enemies));
    memset(shots, 0, sizeof(shots));
    memset(hostile, 0, sizeof(hostile));
    py = 30u; lives = 3u; level = 1u; missiles = 3u;
    score = 0; kills = 0; invul = 0; fireCd = 0; hostileCd = 40u;
    paused = false; mode = ST_PLAY;
    start_level();
}

static void player_hit(void)
{
    if (invul || mode != ST_PLAY) return;
    boom(PX + 1, (int8_t)py, 0);
    A->play_tone(200u, 60u);
    memset(hostile, 0, sizeof(hostile));
    if (--lives == 0u) { mode = ST_OVER; waitCd = 30u; return; }
    invul = 60u; py = 30u;
}

static void kill_enemy(enemy_t *e)
{
    e->state = BOOM;
    score += POINTS[e->type];
    if ((++kills & 15u) == 0u && missiles < 9u) missiles++;
}

static void kill_boss(void)
{
    boss = false;
    score += 500u * level;
    for (uint8_t i = 0; i < N_ENEMY; i++) {   /* bursts all over the hull */
        const uint32_t r = rnd();
        boom(bossX + (int16_t)(r & 7u), (int8_t)(bossY + ((r >> 4) & 7u)), (uint8_t)(i * 3u));
    }
    A->play_tone(700u, 150u);
    memset(hostile, 0, sizeof(hostile));
    if (level < 9u) level++;
    if (lives < 5u) lives++;
    if (missiles < 9u) missiles++;
    start_level();
}

static void spawn_wave(void)
{
    const uint32_t r = rnd();
    const uint8_t type = (uint8_t)(r & 3u);
    const int8_t y = (int8_t)(TOP + 2 + ((r >> 8) & 31u) + ((r >> 16) & 15u));
    uint8_t n = type == 2u ? 1u : 3u;
    for (uint8_t i = 0, k = 0; i < N_ENEMY && k < n; i++) {
        enemy_t *e = &enemies[i];
        if (e->state) continue;
        e->x = (int16_t)(W + k * 12u); e->y = e->base = y;
        e->type = type; e->hp = type == 2u ? 3u : 1u;
        e->state = 1u; e->phase = (uint8_t)(k * 10u);
        k++;
    }
}

static bool enemies_alive(void)
{
    for (uint8_t i = 0; i < N_ENEMY; i++) if (enemies[i].state == 1u) return true;
    return false;
}

static void update_enemies(void)
{
    for (uint8_t i = 0; i < N_ENEMY; i++) {
        enemy_t *e = &enemies[i];
        if (!e->state) continue;
        if (e->state > 1u) { if (--e->state == 1u) e->state = 0; continue; }

        const uint8_t t = e->type;
        int16_t y = e->y;
        e->phase++;
        if (t != 2u || (frame & 1u)) e->x -= SPEED[t];   /* saucer: half speed */
        if (t == 0u) {
            const uint8_t p = (e->phase >> 1) & 31u;
            y = e->base - 8 + (int16_t)(p < 16u ? p : 31u - p);   /* triangle wave */
        } else if (t == 3u && (frame & 1u)) {
            y += aim(y);
        }
        if (y < TOP) y = TOP;
        if (y > Y_MAX) y = Y_MAX;
        e->y = (int8_t)y;

        if (e->x < -8) { e->state = 0; continue; }
        if (hits_player(e->x, e->y, 8, 7)) { kill_enemy(e); player_hit(); continue; }
        if (!hostileCd && e->x < 118 && e->x > PX + 24) {
            add_shot(hostile, N_HOSTILE, e->x - 1, (int8_t)(y + 3), level > 4u ? -3 : -2, aim(y), 1u);
            hostileCd = (uint8_t)(38u - level * 3u);
        }
    }
}

static void update_boss(void)
{
    if (!boss) return;
    if (bossX > 108) {
        bossX--;
    } else if ((frame & 1u) || level > 3u) {
        bossY += bossDir;
        if (bossY <= TOP + 1 || bossY >= Y_MAX - 9) bossDir = (int8_t)-bossDir;
    }
    if (!hostileCd && bossX < 116) {
        for (int8_t vy = -1; vy <= 1; vy++)
            add_shot(hostile, N_HOSTILE, bossX, (int8_t)(bossY + 9), -2, vy, 1u);
        hostileCd = (uint8_t)(28u - level * 2u);
    }
    if (hits_player(bossX + 2, bossY, 14, 16)) player_hit();
}

static void update_shots(void)
{
    for (uint8_t i = 0; i < N_SHOT; i++) {
        shot_t *s = &shots[i];
        if (!s->live) continue;
        const bool missile = s->live == 2u;
        s->x += missile ? 3 : 4;
        if (s->x >= W) { s->live = 0; continue; }
        if (boss && overlap(s->x, s->y, 6, 1, bossX + 1, bossY, 15, 16)) {
            const uint8_t dmg = missile ? 6u : 1u;
            s->live = 0;
            if (bossHp > dmg) bossHp -= dmg; else kill_boss();
            continue;
        }
        for (uint8_t j = 0; j < N_ENEMY; j++) {
            enemy_t *e = &enemies[j];
            if (e->state != 1u || !overlap(s->x, s->y, 6, 1, e->x, e->y, 8, 7)) continue;
            if (missile || --e->hp == 0u) kill_enemy(e);   /* missiles pierce */
            if (!missile) { s->live = 0; break; }
        }
    }

    for (uint8_t i = 0; i < N_HOSTILE; i++) {
        shot_t *s = &hostile[i];
        if (!s->live) continue;
        s->x += s->vx; s->y += s->vy;
        if (s->x < -2 || s->y < TOP || s->y > 62) { s->live = 0; continue; }
        if (hits_player(s->x - 1, s->y - 1, 3, 3)) { s->live = 0; player_hit(); }
    }
}

static void tick(void)
{
    frame++;
    if (fireCd) fireCd--;
    if (hostileCd) hostileCd--;
    if (invul) invul--;
    if (banner) banner--;
    if (waitCd) waitCd--;

    if (mode == ST_PLAY && !fireCd && add_shot(shots, N_SHOT, PX + 9, (int8_t)(py + 3), 0, 0, 1u))
        fireCd = 5u;

    if (!boss && !banner) {
        if (distance < LEVEL_LEN) {
            distance++;
            if (spawnCd) spawnCd--;
            else { spawn_wave(); spawnCd = (uint8_t)(52u - level * 3u + (rnd() & 31u)); }
        } else if (!enemies_alive()) {
            boss = true; bossX = W + 4; bossY = 24; bossDir = 1;
            bossHp = bossMax = (uint8_t)(20u + level * 6u);
            hostileCd = 30u;
        }
    }
    update_enemies();
    update_boss();
    update_shots();
}

static void draw_hud(void)
{
    for (uint8_t i = 0; i < lives; i++) blit(1 + i * 6, 1, &ART[SPR_HEART], 5);
    blit(33, 1, &ART[SPR_MISS], 8);
    number(missiles, 1); A->print_tiny(text, 43, 1, true, true);
    number(score > 99999u ? 99999u : score, 5); A->print_tiny(text, 107, 1, true, true);
    for (uint8_t x = 0; x < W; x += 2u) pixel(x, 8);
    if (boss) {
        A->draw_rect((app_fb_t)A->status_line, 49, 1, 90, 5, true);
        for (uint8_t x = 0; x < 38u; x++)
            if ((uint16_t)x * bossMax < 38u * bossHp) pixel(51 + x, 3);
    }
}

/* Three-layer starfield computed from the frame counter (no RAM): 8 far
 * stars at 1/4 px per tick, 4 at 1/2 and 4 near streaks at 1 px. Each star
 * gets a new pseudo-random height every time it wraps around. */
static void draw_stars(void)
{
    for (uint8_t i = 0; i < 16u; i++) {
        const uint8_t sh = (uint8_t)((0x1Au >> ((i & 3u) * 2u)) & 3u);   /* 2,2,1,0 */
        const uint16_t pos = (uint16_t)(i * 53u + (frame >> sh));
        const uint8_t h = (uint8_t)((pos >> 7) * 29u + i * 71u);
        const int16_t x = (int16_t)(127u - (pos & 127u));
        const int16_t y = (int16_t)(TOP + 2 + ((h * 40u) >> 8));
        pixel(x, y);
        if (!sh) pixel(x + 1, y);
    }
}

/* Hilly ground: the ridge is the sum of two triangle waves whose periods
 * divide 256, so it wraps seamlessly with the low byte of frame. Their
 * steps fall on different columns, so a 1-pixel line stays continuous. */
static void draw_ground(void)
{
    for (uint8_t x = 0; x < W; x++) {
        const uint8_t wx = (uint8_t)(x + frame);
        const uint8_t a = wx & 127u, b = (wx + 2u) & 31u;   /* offset: steps never coincide */
        const uint8_t y = (uint8_t)(62u - ((a < 64u ? a : 127u - a) >> 4)
                                        - ((b < 16u ? b : 31u - b) >> 2));
        const uint8_t h = (uint8_t)(wx * 37u);          /* cheap per-column hash */
        pixel(x, y);
        if (wx & 1u) pixel(x, y + 3);                   /* dotted stratum        */
        if (h < 40u) pixel(x, y + 5 + (h & 1u));        /* scattered pebbles     */
    }
}

static void draw(void)
{
    A->display_clear(); A->status_clear();
    draw_stars();
    draw_ground();

    for (uint8_t i = 0; i < N_ENEMY; i++) {
        const enemy_t *e = &enemies[i];
        if (e->state == 1u && !(e->type == 2u && e->hp < 3u && (frame & 2u)))   /* hurt saucer blinks */
            blit(e->x, e->y, &ART[SPR_ENEMY + e->type * 8u], 8);
        else if (e->state > 1u && e->state <= BOOM)
            blit(e->x, e->y, &ART[SPR_BOOM + (e->state > 5u ? 0u : 8u)], 8);
    }
    if (boss) {
        const uint8_t *spr = &ART[SPR_BOSS + ((level & 1u) ? 0u : 32u)];   /* alternate bosses */
        blit(bossX, bossY, spr, 16);
        blit(bossX, bossY + 8, spr + 16, 16);
    }
    if (mode == ST_PLAY && !(invul & 2u)) blit(PX, py, &ART[SPR_SHIP], 10);

    for (uint8_t i = 0; i < N_SHOT; i++) {
        const shot_t *s = &shots[i];
        if (s->live == 2u) blit(s->x, s->y - 2, &ART[SPR_MISS], 8);
        else if (s->live) for (uint8_t k = 0; k < 4u; k++) pixel(s->x + k, s->y);
    }
    for (uint8_t i = 0; i < N_HOSTILE; i++) {
        const shot_t *s = &hostile[i];
        if (!s->live) continue;
        pixel(s->x, s->y - 1); pixel(s->x, s->y + 1);
        pixel(s->x - 1, s->y); pixel(s->x, s->y); pixel(s->x + 1, s->y);
    }

    draw_hud();
    if (mode == ST_OVER) {
        A->print_bold("GAME OVER", 0, 127, 3);
    } else if (paused) {
        A->print_bold("PAUSE", 0, 127, 3);
    } else if (banner > 10u) {
        A->print_bold(levelText, 0, 127, 3);
    }
}

static void move_player(int8_t dy)
{
    int16_t ny = py + dy;
    if (ny < TOP) ny = TOP;
    if (ny > Y_MAX) ny = Y_MAX;
    py = (uint8_t)ny;
}

static void poll_key(void)
{
    uint8_t key = A->get_key();
    if (key == APP_KEY_SAVER) { saver = true; return; }   /* freeze behind the saver */
    if (key == APP_KEY_WAKE || key == APP_KEY_PTT) { saver = false; key = APP_KEY_INVALID; }
    const bool press = key != prevKey;
    const bool action = key == APP_KEY_5 || key == APP_KEY_MENU;
    prevKey = key;
    if (key == APP_KEY_INVALID) return;
    if (press && key == APP_KEY_EXIT) { running = false; return; }

    if (mode != ST_PLAY) {
        if (press && key == APP_KEY_MENU && !waitCd) new_game();
        return;
    }
    if (press && key == APP_KEY_F) { paused = !paused; return; }
    if (paused) return;
    if (action) {
        if (press && missiles && add_shot(shots, N_SHOT, PX + 4, (int8_t)(py + 3), 0, 0, 2u))
            missiles--;
    } else if (key == APP_KEY_2) {
        move_player(-2);
    } else if (key == APP_KEY_8) {
        move_player(2);
    } else {
        move_player((int8_t)(-2 * A->nav_dir(key)));   /* 0 for any other key */
    }
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    randomState = ((uint32_t)A->bk_read(0x67u) << 16) ^ A->rx_freq();
    running = true; paused = false;
    prevKey = A->get_key();
    A->led(false); A->backlight_on();
    new_game();

    while (running) {
        poll_key();
        if (!running) break;
        if (saver) { A->backlight_update(); A->delay_ms(TICK_MS); continue; }
        if (!paused) tick();
        draw(); A->blit_status(); A->blit_full(); A->backlight_update();
        A->delay_ms(TICK_MS);
    }

    A->led(false);
}
