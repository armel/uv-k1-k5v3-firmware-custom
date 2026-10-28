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

/* QR Code encoder, one fixed size: version 3 (29 x 29 modules), error
 * correction level L, byte mode, one block of 55 data and 15 ECC codewords.
 * Up to 53 bytes: a "https://..." map link with two coordinates fits.
 *
 * Pure C, no API call: the host test builds it as it is (test/host_qr.c). */
#ifndef QRMAP_QR_H
#define QRMAP_QR_H

#include <stdint.h>

#define QR_N   29u   /* modules per side                        */
#define QR_MAX 53u   /* bytes that fit at level L               */

/* Encode len bytes (QR_MAX at most) of s. Row y of the symbol is m[y]:
 * bit x set = dark module, x = 0 on the left. The quiet zone is not
 * included. Returns the mask chosen (0-7), the one with the lowest
 * penalty score of ISO/IEC 18004 section 7.8.3. */
unsigned qr_encode(const uint8_t *s, unsigned len, uint32_t m[QR_N]);

#endif /* QRMAP_QR_H */
