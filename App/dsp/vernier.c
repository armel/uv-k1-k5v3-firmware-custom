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

#include "dsp/vernier.h"

#define MAX_XTAL_TRIM  600
#define PLL_STEP_MHZ   10000   // 10 Hz expressed in mHz

VernierResult_t VERNIER_Solve(int32_t delta_f_mhz, uint32_t alpha_mhz)
{
    VernierResult_t best;
    best.xtal_trim = 0;
    best.pll_comp  = 0;
    best.error_mhz = 32767;

    if (alpha_mhz == 0)
        return best;

    for (uint16_t xt = 0; xt <= MAX_XTAL_TRIM; xt++)
    {
        int32_t xtal_effect = (int32_t)xt * (int32_t)alpha_mhz;

        int32_t pc = (xtal_effect - delta_f_mhz + (PLL_STEP_MHZ / 2)) / PLL_STEP_MHZ;
        if (pc < 0)
            continue;

        int32_t actual = xtal_effect - pc * PLL_STEP_MHZ;
        int32_t err = actual - delta_f_mhz;
        if (err < 0)
            err = -err;

        if (err < best.error_mhz)
        {
            best.xtal_trim = xt;
            best.pll_comp  = (uint16_t)pc;
            best.error_mhz = (int16_t)err;
            if (err == 0)
                break;
        }
    }

    return best;
}

uint32_t VERNIER_ComputeAlpha(uint32_t f_carrier_hz)
{
    // alpha = 5000 * f / 26000000 (mHz per LSB of REG_3B)
    // Use 64-bit intermediate to avoid overflow
    return (uint32_t)((5000ULL * (uint64_t)f_carrier_hz) / 26000000ULL);
}
