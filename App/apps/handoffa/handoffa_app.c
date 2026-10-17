/* Hand-off test chain (API level 3): Handoff A -> Handoff B -> Handoff A.
 *
 * One source, two apps: handoffb_app.c builds it with HANDOFF_B.  Install both,
 * open Handoff A from the Apps menu, then:
 *
 *   MENU  A: count++, save it (cfg_save), hand off a 40-byte PING to B.
 *         B: checks the PING (sender, clamp to APP_HANDOFF_MAX, pattern), shows it,
 *            MENU hands the count back to A in a PONG.
 *         A: checks that the PONG count equals the count it read back with
 *            cfg_load, which proves its config was committed before B ran.
 *   1     hand off to an app that is not installed: must be refused.
 *   2     hand off to the peer, then cancel (a missing name) and exit: the
 *         Apps menu must come back.
 *   EXIT  leave normally (Apps menu).
 *
 * Every launch also checks that a second handoff_read() returns 0 (read once).
 * Launched from the menu, an app shows "RX none".
 */
#include <stddef.h>
#include "../app_api.h"

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *p = d;
    const uint8_t *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}

#ifdef HANDOFF_B
static const char SELF[APP_NAME_LEN] = "Handoff B";
static const char PEER[APP_NAME_LEN] = "Handoff A";
#else
static const char SELF[APP_NAME_LEN] = "Handoff A";
static const char PEER[APP_NAME_LEN] = "Handoff B";
#endif
static const char MISSING[APP_NAME_LEN] = "No Such App";

enum { MSG_PING = 'P', MSG_PONG = 'R' };
#define PING_LEN 40u   /* longer than APP_HANDOFF_MAX on purpose: must arrive clamped */

static const app_api_t *A;

static uint8_t pattern(uint8_t i) { return (uint8_t)(i * 7u + 0x5Au); }

static bool same_name(const char *a, const char *b)
{
    for (uint8_t i = 0; i < APP_NAME_LEN; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

/* "<prefix><n>" with n in decimal, into a 20-byte line. */
static void line_num(char *out, const char *prefix, uint8_t n)
{
    uint8_t i = 0;
    while (*prefix)
        out[i++] = *prefix++;
    if (n >= 100) out[i++] = (char)('0' + n / 100);
    if (n >= 10)  out[i++] = (char)('0' + n / 10 % 10);
    out[i++] = (char)('0' + n % 10);
    out[i] = '\0';
}

static void show(uint8_t line, const char *s)
{
    for (uint8_t x = 0; x < 128; x++)
        A->fb[line][x] = 0;
    A->print_normal(s, 0, 127, line);
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    uint8_t msg[PING_LEN];
    char from[APP_NAME_LEN + 1];
    char text[24];

    const uint8_t n = A->handoff_read(msg, sizeof(msg), from);
    from[APP_NAME_LEN] = '\0';
    char again[APP_NAME_LEN + 1];
    const bool once = A->handoff_read(msg, sizeof(msg), again) == 0;

    uint8_t count = 0;
#ifndef HANDOFF_B
    A->cfg_load(&count, 1);
#endif

    A->backlight_on();
    A->display_clear();
    A->status_clear();
    A->print_tiny("HANDOFF TEST V" APP_VERSION, 2, 1, true, true);
    A->print_bold(SELF, 0, 127, 0);

    if (n == 0) {
        show(1, "RX none");
    } else {
        line_num(text, "RX ", n);
        show(1, text);
        show(2, from);
        bool ok = same_name(from, PEER);
#ifdef HANDOFF_B
        ok = ok && n == APP_HANDOFF_MAX && msg[0] == MSG_PING;
        for (uint8_t i = 2; i < n; i++)
            ok = ok && msg[i] == pattern(i);
        count = msg[1];
        line_num(text, ok ? "PING OK #" : "PING BAD #", count);
#else
        ok = ok && n == 2u && msg[0] == MSG_PONG && msg[1] == count;
        line_num(text, ok ? "PONG+CFG OK #" : "PONG BAD #", msg[1]);
#endif
        show(3, text);
    }
    show(4, once ? "READ ONCE OK" : "READ TWICE BAD");
    A->print_tiny("MENU PEER  1 MISSING  2 CANCEL", 2, 50, false, true);
    A->blit_status();
    A->blit_full();

    /* Wait for the key that launched us to be released. */
    while (A->get_key() != APP_KEY_INVALID)
        A->delay_ms(10);

    for (;;) {
        A->delay_ms(20);
        A->backlight_update();
        const uint8_t key = A->get_key();
        if (key == APP_KEY_SAVER || key == APP_KEY_WAKE || key == APP_KEY_INVALID)
            continue;
        while (A->get_key() != APP_KEY_INVALID)   /* act on release */
            A->delay_ms(10);

        if (key == APP_KEY_EXIT)
            return;

        if (key == APP_KEY_MENU) {
#ifdef HANDOFF_B
            msg[0] = MSG_PONG;
            msg[1] = count;
            if (A->handoff(PEER, msg, 2))
                return;
#else
            count++;
            A->cfg_save(&count, 1);
            msg[0] = MSG_PING;
            msg[1] = count;
            for (uint8_t i = 2; i < PING_LEN; i++)
                msg[i] = pattern(i);
            if (A->handoff(PEER, msg, PING_LEN))
                return;
#endif
            show(5, "PEER MISSING");
        } else if (key == APP_KEY_1) {
            show(5, A->handoff(MISSING, NULL, 0) ? "MISS ACCEPTED BAD" : "MISS REFUSED OK");
        } else if (key == APP_KEY_2) {
            A->handoff(PEER, NULL, 0);
            A->handoff(MISSING, NULL, 0);
            return;   /* expect the Apps menu, not the peer */
        }
        A->blit_full();
    }
}
