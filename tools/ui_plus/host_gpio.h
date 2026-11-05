// Host rendering tests must never read the radio's memory-mapped PTT register.
#ifndef DRIVER_GPIO_H
#define DRIVER_GPIO_H
#include <stdbool.h>
static inline bool GPIO_IsPttPressed(void) { return true; }
#endif
