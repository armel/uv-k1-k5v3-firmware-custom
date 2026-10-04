/* SPDX-License-Identifier: Apache-2.0 */
#include "afsk_core.h"

bool AFSK_ValidateFrame(const uint8_t *frame, uint16_t length)
{
    if (!frame || length < 18u || length > APP_AFSK_FRAME_MAX)
        return false;
    uint16_t crc = 0xffffu;
    for (uint16_t n = 0; n < length; ++n) {
        crc ^= frame[n];
        for (uint8_t i = 0; i < 8u; ++i)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0x8408u : 0u);
    }
    return crc == 0xf0b8u; /* reflected X.25 residue including little-endian FCS */
}

bool AFSK_ValidateOptions(const app_afsk_opts_t *opts)
{
    return opts && opts->size == sizeof(*opts)
        && opts->flags == APP_AFSK_REQUIRE_PHYSICAL_PTT
        && !opts->reserved && opts->preamble_flags >= 1u
        && opts->preamble_flags <= 120u && opts->tail_flags >= 1u
        && opts->tail_flags <= 10u && opts->tone_gain >= 1u
        && opts->tone_gain <= 127u && opts->max_on_ms >= 100u
        && opts->max_on_ms <= APP_AFSK_MAX_ON_MS;
}

void AFSK_HDLC_Init(afsk_hdlc_t *s, const app_afsk_opts_t *opts)
{
    *s = (afsk_hdlc_t){ .flags_left = opts->preamble_flags };
}

bool AFSK_HDLC_Next(afsk_hdlc_t *s, const uint8_t *frame, uint16_t length,
                    uint8_t tail_flags, uint8_t *bit)
{
    /* Enter tail in-place, with no recursion or extra ISR stack frame. */
    if (s->stage == 1u && !s->stuffed_zero && s->byte == length) {
        s->stage = 2;
        s->flags_left = tail_flags;
    }
    if (s->stage == 0u || s->stage == 2u) {
        *bit = (0x7eu >> s->bit) & 1u;
        if (++s->bit == 8u) {
            s->bit = 0;
            if (--s->flags_left == 0u) {
                ++s->stage;
                s->ones = 0;
            }
        }
        return true;
    }
    if (s->stage == 3u)
        return false;
    if (s->stuffed_zero) {
        s->stuffed_zero = false;
        s->ones = 0;
        *bit = 0;
        return true;
    }
    *bit = (frame[s->byte] >> s->bit) & 1u;
    if (++s->bit == 8u) {
        s->bit = 0;
        ++s->byte;
    }
    s->ones = *bit ? s->ones + 1u : 0;
    s->stuffed_zero = s->ones == 5u;
    return true;
}
