#ifndef AFSK_TX_MOCK_H
#define AFSK_TX_MOCK_H
#include <stdint.h>
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
#endif
