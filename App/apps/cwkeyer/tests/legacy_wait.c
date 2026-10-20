/* Copyright 2026 Armel F4HWN and Johan Denoyer F4WAT.
 * SPDX-License-Identifier: Apache-2.0
 * Reference waits from CW Keyer 0.5, commit d0f30a67.
 * Included by the host regression harness only. */
static bool keyerWait(uint16_t ms)
{
    uint32_t left = ms * g.ticksPerMs;
    ticksSince();
    for (;;) {
        const uint8_t key = A->get_key();
        if (key == APP_KEY_EXIT) {
            g.running = false;
            return false;
        }
        const uint32_t spent = ticksSince();
        if (spent >= left)
            return g.running;
        left -= spent;
        /* Poll without sleeping for the last millisecond: overshoot stays
         * below one keypad scan instead of a whole 1 ms step. */
        if (left > g.ticksPerMs)
            serviceOneMs();
    }
}
static void runPaddle(uint8_t p)
{
    uint8_t last = 0u;
    while (p) {
        const uint8_t el = p != (P_DIT | P_DAH) ? p : last == P_DIT ? P_DAH : P_DIT;
        if (!keyOn())
            return;
        const uint16_t dot = dotMs();
        /* The PA bias ramps take RAMP_MS at each edge and cross half drive
         * about one millisecond off centre, so the waits are trimmed to give a
         * 1:1 mark/space at the half-drive points and an exact 2-dot period. */
        const bool done = keyerWait((uint16_t)((el == P_DAH ? 3u * dot : dot) - RAMP_MS - 1u));
        keyOff();
        if (!done || !keyerWait((uint16_t)(dot - RAMP_MS + 1u)))
            return;
        last = el;
        p = paddles(A->get_key());
    }
}
