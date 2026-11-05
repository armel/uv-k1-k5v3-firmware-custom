/* Copyright 2026 Armel F4HWN
 * Licensed under the Apache License, Version 2.0.
 */

#include <stdint.h>
#include "py32f0xx.h"
#include "stack_usage.h"

#define STACK_WATERMARK 0xA5A5A5A5u

extern uint32_t _ebss;
extern uint32_t _estack;

void STACK_WatermarkInit(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* The active frame starts at MSP. Only paint the unused gap below it. */
    volatile uint32_t *cursor = &_ebss;
    uintptr_t stack_pointer = (uintptr_t)__get_MSP() & ~(uintptr_t)3u;
    if (stack_pointer > (uintptr_t)&_estack)
        stack_pointer = (uintptr_t)&_estack;
    while ((uintptr_t)cursor < stack_pointer)
        *cursor++ = STACK_WATERMARK;

    __set_PRIMASK(primask);
}

uint32_t STACK_FreeNow(void)
{
    const uintptr_t bottom = (uintptr_t)&_ebss;
    const uintptr_t stack_pointer = (uintptr_t)__get_MSP();
    return stack_pointer > bottom ? (uint32_t)(stack_pointer - bottom) : 0u;
}

uint32_t STACK_FreeMinimum(void)
{
    /* The first changed word is the lowest stack address observed since init. */
    const volatile uint32_t *cursor = &_ebss;
    const volatile uint32_t *const end = &_estack;
    while (cursor < end && *cursor == STACK_WATERMARK)
        cursor++;
    return (uint32_t)((uintptr_t)cursor - (uintptr_t)&_ebss);
}
