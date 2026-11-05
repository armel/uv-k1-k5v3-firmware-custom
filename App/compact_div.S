/* Copyright 2024 Armel F4HWN
 * https://github.com/armel
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
 * Signed 32-bit division for the Cortex-M0+ (no hardware divider).
 *
 * libgcc's _divsi3.o (468 B) carries its own copy of the unrolled division
 * loop that _udivsi3.o (276 B) already brings for unsigned divisions. These
 * entry points only fold the operand signs around __aeabi_uidivmod, which
 * returns the quotient in r0 and the remainder in r1 (~30 extra cycles per
 * signed division).
 *
 * Signs are kept as masks m (0 or -1): (x ^ m) - m negates x when m is -1.
 * Results match libgcc, including the undefined cases:
 *   - quotient truncated toward zero, remainder has the sign of the dividend;
 *   - INT_MIN / -1 = INT_MIN, remainder 0 (|INT_MIN| is 0x80000000 unsigned);
 *   - x / 0 = 0, remainder x (libgcc's __aeabi_uidivmod returns 0 and |x|).
 */

.syntax unified
.thumb

.section .text.__aeabi_idiv, "ax", %progbits
.align 2

.global __aeabi_idiv
.global __aeabi_idivmod
.global __divsi3
.type __aeabi_idiv, %function
.type __aeabi_idivmod, %function
.type __divsi3, %function
.thumb_func
__aeabi_idiv:
.thumb_func
__aeabi_idivmod:
.thumb_func
__divsi3:
    push    {r3, r4, r5, lr}    /* r3 only keeps the stack 8-byte aligned */
    asrs    r4, r0, #31         /* r4 = dividend sign mask */
    eors    r0, r4
    subs    r0, r0, r4          /* r0 = |dividend| */
    asrs    r5, r1, #31         /* r5 = divisor sign mask */
    eors    r1, r5
    subs    r1, r1, r5          /* r1 = |divisor| */
    eors    r5, r4              /* r5 = quotient sign mask */
    bl      __aeabi_uidivmod    /* r0 = quotient, r1 = remainder */
    eors    r0, r5
    subs    r0, r0, r5          /* quotient is negative when the signs differ */
    eors    r1, r4
    subs    r1, r1, r4          /* remainder takes the dividend sign */
    pop     {r3, r4, r5, pc}

.size __aeabi_idiv, . - __aeabi_idiv
.size __aeabi_idivmod, . - __aeabi_idivmod
.size __divsi3, . - __divsi3
