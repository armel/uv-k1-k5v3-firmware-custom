/* Copyright 2026 Armel F4HWN
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef STACK_USAGE_H
#define STACK_USAGE_H

#include <stdint.h>

void STACK_WatermarkInit(void);
uint32_t STACK_FreeNow(void);
uint32_t STACK_FreeMinimum(void);

#endif
