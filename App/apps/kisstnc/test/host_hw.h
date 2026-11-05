/* Host build of kisstnc_app.c (test_tnc.py): the MCU registers become plain
 * variables. SysTick is a 48 MHz down-counter that advances 1250 cycles at each
 * read, ADC_DR returns the next sample of the scripted audio. */
#include <stdint.h>
extern uint32_t host_reg[16];
uint32_t host_systval(void);
uint32_t host_adc(void);
#define SYST_LOAD   479999u
#define SYST_VAL    host_systval()
#define ADC_SR      2u                 /* end of conversion, always */
#define ADC_CR2     host_reg[0]
#define ADC_SMPR3   host_reg[1]
#define ADC_SQR3    host_reg[2]
#define ADC_DR      host_adc()
#define GPIOA_MODER host_reg[3]
#define DAC_CR      host_reg[4]
#define DAC_SWTRIGR host_reg[5]
#define DAC_DHR12R1 host_reg[6]
#define RCC_APBENR1 host_reg[7]
