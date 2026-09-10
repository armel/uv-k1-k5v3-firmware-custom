/* Copyright 2025 bg7nzl
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

#ifndef DSP_VERNIER_H
#define DSP_VERNIER_H

#include <stdint.h>

typedef struct {
    uint16_t xtal_trim;  // REG_3B offset (crystal oscillator trim)
    uint16_t pll_comp;   // REG_38/39 offset (PLL compensation steps)
    int16_t  error_mhz;  // residual error in mHz
} VernierResult_t;

/*
 * Solve for the best (xtal_trim, pll_comp) pair that produces the desired
 * sub-10-Hz frequency offset using the Vernier technique.
 *
 * delta_f_mhz : desired offset in millihertz (0 .. 9999)
 * alpha_mhz   : xtal trim sensitivity in mHz/LSB,
 *               pre-computed as (5000 * f_carrier_hz) / 26000000
 *
 * The Vernier effect:
 *   actual offset = xtal_trim * alpha_mhz - pll_comp * 10000  (mHz)
 * We search xtal_trim in [0..600] and pick the best integer pll_comp.
 */
VernierResult_t VERNIER_Solve(int32_t delta_f_mhz, uint32_t alpha_mhz);

/*
 * Pre-compute alpha_mhz for a given carrier frequency.
 * alpha_mhz = 5000 * f_carrier_hz / 26000000
 */
uint32_t VERNIER_ComputeAlpha(uint32_t f_carrier_hz);

#endif
