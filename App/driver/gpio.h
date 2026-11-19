/* Copyright 2025 muzkr https://github.com/muzkr
 * Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
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

#ifndef DRIVER_GPIO_H
#define DRIVER_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "py32f071_ll_gpio.h"

#define GPIO_MAKE_PIN(Port, PinMask)    ((uint32_t)((((uint32_t)(Port)) << 16) | (0xffff & (PinMask))))
#define GPIO_PORT(Pin)                  ((GPIO_TypeDef *)(IOPORT_BASE + ((Pin) >> 16)))
#define GPIO_PIN_MASK(Pin)              (0xffff & (Pin))

enum GPIO_PINS
{
    GPIO_PIN_PTT            = GPIO_MAKE_PIN(GPIOB, LL_GPIO_PIN_10),
    GPIO_PIN_BACKLIGHT      = GPIO_MAKE_PIN(GPIOF, LL_GPIO_PIN_8),
    GPIO_PIN_FLASHLIGHT     = GPIO_MAKE_PIN(GPIOC, LL_GPIO_PIN_13),
    GPIO_PIN_AUDIO_PATH     = GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_8),
};

static inline void GPIO_SetOutputPin(uint32_t Pin)
{
    LL_GPIO_SetOutputPin(GPIO_PORT(Pin), GPIO_PIN_MASK(Pin));
}

static inline void GPIO_ResetOutputPin(uint32_t Pin)
{
    LL_GPIO_ResetOutputPin(GPIO_PORT(Pin), GPIO_PIN_MASK(Pin));
}

static inline void GPIO_TogglePin(uint32_t Pin)
{
    LL_GPIO_TogglePin(GPIO_PORT(Pin), GPIO_PIN_MASK(Pin));
}

static inline bool GPIO_IsInputPinSet(uint32_t Pin)
{
    return !!LL_GPIO_IsInputPinSet(GPIO_PORT(Pin), GPIO_PIN_MASK(Pin));
}

static inline bool GPIO_IsOutputPinSet(uint32_t Pin)
{
    return !!LL_GPIO_IsOutputPinSet(GPIO_PORT(Pin), GPIO_PIN_MASK(Pin));
}

static inline void GPIO_EnableAudioPath()
{
    GPIO_SetOutputPin(GPIO_PIN_AUDIO_PATH);
}

static inline void GPIO_DisableAudioPath()
{
    GPIO_ResetOutputPin(GPIO_PIN_AUDIO_PATH);
}

static inline void GPIO_TurnOnBacklight()
{
    GPIO_SetOutputPin(GPIO_PIN_BACKLIGHT);
}

static inline void GPIO_TurnOffBacklight()
{
    GPIO_ResetOutputPin(GPIO_PIN_BACKLIGHT);
}

static inline bool GPIO_IsPttPressed()
{
    return !GPIO_IsInputPinSet(GPIO_PIN_PTT);
}

#endif
