#ifndef AFSK_HW_MOCK_H
#define AFSK_HW_MOCK_H
#include "afsk_mock.h"
typedef struct {uint32_t Frequency;} frequency_t;
typedef struct {
    frequency_t *pTX,*pRX;
    uint8_t CHANNEL_BANDWIDTH,Modulation,OUTPUT_POWER,TXP_CalculatedSetting,
            TX_LOCK,BUSY_CHANNEL_LOCK,SquelchOpenRSSIThresh;
} VFO_Info_t;
typedef uint8_t BK4819_FilterBandwidth_t;
enum { MODULATION_FM=0, MODULATION_AM=1, MODULATION_USB=2,
       OUTPUT_POWER_HIGH=7,BK4819_FILTER_BW_NARROWER=2,
       VFO_STATE_NORMAL=0,VFO_STATE_BUSY=1,VFO_STATE_BAT_LOW=2,
       VFO_STATE_VOLTAGE_HIGH=3,FUNCTION_TRANSMIT=4,
       BK4819_GPIO1_PIN29_PA_ENABLE=1,BK4819_GPIO0_PIN28_RX_ENABLE=0,
       BK4819_REG_24=0x24,BK4819_REG_30=0x30,BK4819_REG_36=0x36,
       BK4819_REG_37=0x37,BK4819_REG_52=0x52,BK4819_REG_70=0x70,
       BK4819_REG_71=0x71,BK4819_AF_MUTE=0,TIM2_IRQn=15,
       RCC_APBENR1_TIM2EN=1,RCC_APBRSTR1_TIM2RST=1,LL_RCC_APB1_DIV_1=0,
       TIM_EGR_UG=1,TIM_CR1_CEN=1,TIM_SR_UIF=1,TIM_SR_CC1IF=2,
       TIM_DIER_UIE=1,TIM_DIER_CC1IE=2 };
typedef struct { uint32_t APBENR1,APBRSTR1; } RCC_mock_t;
typedef struct {uint32_t PSC,ARR,EGR,SR,CNT,CR1,CCR1,DIER;} TIM_mock_t;
typedef struct {uint32_t PCLK1_Frequency,HCLK_Frequency;} LL_RCC_ClocksTypeDef;
extern RCC_mock_t mock_rcc;
extern TIM_mock_t mock_tim;
#define RCC (&mock_rcc)
#define TIM2 (&mock_tim)
extern VFO_Info_t *gTxVfo;
extern uint8_t gCurrentFunction;
extern bool gEnableSpeaker;
uint8_t RADIO_CheckTXPermission(const VFO_Info_t *vfo);
uint32_t NVIC_GetEnableIRQ(unsigned irq);
uint32_t NVIC_GetPriority(unsigned irq);
void NVIC_SetPriority(unsigned irq,uint32_t priority);
void NVIC_ClearPendingIRQ(unsigned irq);
void NVIC_EnableIRQ(unsigned irq);
void NVIC_DisableIRQ(unsigned irq);
void LL_RCC_GetSystemClocksFreq(LL_RCC_ClocksTypeDef *clocks);
uint32_t LL_RCC_GetAPB1Prescaler(void);
bool GPIO_IsPttPressed(void);
bool BK4819_IsGpioOutSet(unsigned pin);
uint16_t BK4819_GetRSSI(void);
void BK4819_ToggleGpioOut(unsigned pin,bool on);
void BK4819_WriteRegister(unsigned reg,uint16_t value);
void BK4819_EnterTxMute(void);
void BK4819_ExitTxMute(void);
void BK4819_ExitBypass(void);
void BK4819_SetFilterBandwidth(BK4819_FilterBandwidth_t bw,bool weak);
void BK4819_SetFrequency(uint32_t freq);
void BK4819_PickRXFilterPathBasedOnFrequency(uint32_t freq);
void BK4819_SetupPowerAmplifier(uint8_t bias,uint32_t freq);
void BK4819_DisableScramble(void);
void BK4819_DisableVox(void);
void BK4819_SetCompander(unsigned mode);
void BK4819_DisableDTMF(void);
void BK4819_ExitSubAu(void);
void BK4819_SetAF(unsigned af);
void BK4819_EnableTXLink(void);
void SYSTEM_DelayMs(uint32_t ms);
void AUDIO_AudioPathOff(void);
void RADIO_SetupRegisters(bool foreground);
#endif
