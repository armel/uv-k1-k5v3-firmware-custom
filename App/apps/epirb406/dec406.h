/* Copyright 2026 Johan Denoyer F4WAT
 * https://github.com/jdenoy
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
 * dec406: streaming decoder for first-generation Cospas-Sarsat 406 MHz beacon
 * messages, fed with FM-discriminator audio samples (the BK4829 RAW RX audio as
 * read on PA4). Freestanding C (no libc), no division in the per-sample path, so
 * the same code runs in the overlay app and in the host test harness.
 *
 * Signal: 400 bps biphase-L, +/-1.1 rad phase modulation. The discriminator gives
 * dphi/dt (a pulse per phase flip); a leaky integrator rebuilds the phase
 * waveform, a DPLL on its zero crossings tracks the half-bit clock, and the
 * half-bit signs are matched against 13 preamble ones + frame sync, in both
 * polarities (the receive chain may invert). Bit n of the message (1-based, as in
 * C/S T.001) for n = 25..144 is then stored MSB first in bits[].
 */

#ifndef DEC406_H
#define DEC406_H

#include <stdint.h>
#include <stdbool.h>

#define DEC406_FS     9600u   /* sample rate expected by the decoder (Hz)     */
#define DEC406_HALF   12u     /* samples per half-bit at 400 bps               */

enum { DEC406_SEARCH = 0, DEC406_DATA = 1, DEC406_DONE = 2 };

typedef struct {
    int32_t  meanQ8;          /* input DC estimate, x256                        */
    int32_t  lvl;             /* rebuilt phase waveform                         */
    int32_t  acc;             /* sum of lvl over the current half-bit slot      */
    int32_t  h1;              /* first half of the bit being received           */
    int32_t  ph;              /* position in the half-bit slot, Q8              */
    uint32_t hsrHi, hsrLo;    /* last 64 half-bit signs, newest in hsrLo bit 0  */
    uint8_t  state, half, inv, selftest, integrate, sign, nbits, total;
    uint8_t  bits[15];        /* message bits 25..144, MSB first                */
} dec406_t;

typedef struct {
    uint8_t  longMsg;         /* bit 25                                         */
    uint8_t  selftest;        /* self-test frame sync                           */
    uint8_t  bch1, bch2;      /* 1 = BCH ok (bch2 is 1 on short messages)       */
    uint8_t  userProto;       /* bit 26: 1 = user / user-location protocols     */
    uint8_t  proto;           /* bits 37-40 (location), 16 + bits 37-39 (user) */
    uint8_t  stdLoc;          /* standard location protocol (host only)         */
    uint8_t  hasPos;          /* position present (not the default pattern)     */
    uint8_t  hasFine;         /* PDF-2 offsets applied                          */
    uint8_t  internalPos;     /* bit 1: flag present, bit 0: internal device   */
    uint8_t  homing;          /* bit 112: 121.5 MHz homing                      */
    uint8_t  idRaw;           /* 1 = ID is bits 26-85 as sent (spare location code),
                                 2 = ELT(DT) cancellation message (A3.3.8.5)     */
    uint16_t country;         /* bits 27-36                                     */
    uint32_t idData;          /* std location: bits 41-64 (host only)           */
    int32_t  latS, lonS;      /* position in arc seconds, N and E positive      */
    char     id[16];          /* 15-hex beacon ID, NUL terminated               */
} dec406_info_t;

/* integrate: 1 = input is discriminator output (dphi/dt), rebuild the phase;
 *            0 = input is already phase-like (use it directly).             */
void dec406_init(dec406_t *d, bool integrate);

/* Re-arm for the next burst (keeps the DC estimate). */
void dec406_rearm(dec406_t *d);

/* Feed one sample (any unsigned ADC scale). Returns true when a full message
 * has been received (state DEC406_DONE); call dec406_parse, then dec406_rearm. */
bool dec406_push(dec406_t *d, uint16_t sample);

/* BCH checks and field extraction. The PDF-1 position bits of d are left at
 * their default value (they make the 15-hex ID): parse a message only once. */
void dec406_parse(dec406_t *d, dec406_info_t *out);

/* Short protocol name for display. */
const char *dec406_proto_name(const dec406_info_t *in);

#endif /* DEC406_H */
