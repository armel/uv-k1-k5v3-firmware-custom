/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app/afsk_hw.h"
#include "afsk_hw_mock.h"
RCC_mock_t mock_rcc;
TIM_mock_t mock_tim;
static frequency_t frequency={14480000};
static VFO_Info_t vfo={.pTX=&frequency,.pRX=&frequency,.TXP_CalculatedSetting=87,.TX_LOCK=1};
VFO_Info_t *gTxVfo=&vfo;
uint8_t gCurrentFunction;
bool gEnableSpeaker;
static uint32_t mask,clock_hz=48000000,apb_div,irq_enable,priority=2;
static bool ptt=true,pa,rf_lease;
static uint16_t registers[128],rssi;
static uint8_t permission_state;
static uint32_t irq_time;
static unsigned prepare_ops,delay_count;
uint32_t __get_PRIMASK(void){return mask;}
void __disable_irq(void){mask=1;}
void __set_PRIMASK(uint32_t m){mask=m;}
bool AFSK_TX_OwnsRF(void){return rf_lease;}
void AFSK_TX_Tick(uint32_t now){irq_time=now;}
uint8_t RADIO_CheckTXPermission(const VFO_Info_t *v){assert(v==&vfo);return permission_state;}
uint32_t NVIC_GetEnableIRQ(unsigned irq){(void)irq;return irq_enable;}
uint32_t NVIC_GetPriority(unsigned irq){(void)irq;return priority;}
void NVIC_SetPriority(unsigned irq,uint32_t p){(void)irq;priority=p;}
void NVIC_ClearPendingIRQ(unsigned irq){(void)irq;}
void NVIC_EnableIRQ(unsigned irq){(void)irq;irq_enable=1;}
void NVIC_DisableIRQ(unsigned irq){(void)irq;irq_enable=0;}
void LL_RCC_GetSystemClocksFreq(LL_RCC_ClocksTypeDef *c){c->HCLK_Frequency=clock_hz;c->PCLK1_Frequency=clock_hz>>(apb_div!=0);}
uint32_t LL_RCC_GetAPB1Prescaler(void){return apb_div;}
bool GPIO_IsPttPressed(void){return ptt;}
bool BK4819_IsGpioOutSet(unsigned pin){(void)pin;return pa;}
uint16_t BK4819_GetRSSI(void){return rssi;}
void BK4819_ToggleGpioOut(unsigned pin,bool on){if(pin==BK4819_GPIO1_PIN29_PA_ENABLE)pa=on;}
void BK4819_WriteRegister(unsigned reg,uint16_t value){assert(!pa);registers[reg]=value;++prepare_ops;}
void BK4819_EnterTxMute(void){}
void BK4819_ExitTxMute(void){}
void BK4819_ExitBypass(void){}
void BK4819_SetFilterBandwidth(BK4819_FilterBandwidth_t bw,bool weak){assert(!pa);(void)bw;(void)weak;}
void BK4819_SetFrequency(uint32_t f){assert(!pa&&f==frequency.Frequency);}
void BK4819_PickRXFilterPathBasedOnFrequency(uint32_t f){assert(!pa&&f==frequency.Frequency);}
void BK4819_SetupPowerAmplifier(uint8_t bias,uint32_t f){assert(!pa&&f==frequency.Frequency);registers[0x36]=bias;}
void BK4819_DisableScramble(void){assert(!pa);}
void BK4819_DisableVox(void){assert(!pa);}
void BK4819_SetCompander(unsigned mode){assert(!pa&&!mode);}
void BK4819_DisableDTMF(void){registers[0x24]=0;}
void BK4819_ExitSubAu(void){assert(!pa);}
void BK4819_SetAF(unsigned af){assert(!pa&&!af);}
void BK4819_EnableTXLink(void){assert(!pa);registers[0x30]=0xc3fa;}
void SYSTEM_DelayMs(uint32_t ms){assert(!pa);mock_tim.CNT+=ms*120;mock_tim.SR=0;++delay_count;}
void AUDIO_AudioPathOff(void){assert(!pa);}
void RADIO_SetupRegisters(bool foreground){assert(!pa&&foreground);}
extern void TIM2_IRQHandler(void);
int main(void)
{
    assert(AFSK_HW_AcquireTimer());
    assert(mock_tim.PSC==399&&mock_tim.ARR==65535&&priority==0);
    AFSK_HW_ReleaseTimer(); assert(priority==2&&!irq_enable&&!mock_rcc.APBENR1);
    clock_hz=24000000; assert(!AFSK_HW_AcquireTimer()); clock_hz=48000000;
    mock_rcc.APBENR1=1; assert(!AFSK_HW_AcquireTimer()); mock_rcc.APBENR1=0;
    irq_enable=1; assert(!AFSK_HW_AcquireTimer()); irq_enable=0;
    apb_div=1; assert(AFSK_HW_AcquireTimer()); apb_div=0;
    app_tx_info_t info={.size=sizeof(info)};
    assert(AFSK_HW_Info(&info)==0&&info.tx_freq_10hz==14480000&&info.channel_token);
    uint32_t token=info.channel_token;
    assert(AFSK_HW_Info(&info)==0&&info.channel_token==token);
    vfo.TXP_CalculatedSetting=88; assert(AFSK_HW_Info(&info)==0&&info.channel_token!=token);
    vfo.BUSY_CHANNEL_LOCK=1; vfo.SquelchOpenRSSIThresh=100;rssi=101;
    assert(AFSK_HW_Info(&info)==APP_AFSK_BUSY);rssi=0;
    assert(AFSK_HW_Info(&info)==0);rf_lease=true;
    mock_tim.SR=0;mock_tim.CNT=0;
    assert(AFSK_HW_Prepare(&info,66)==0);
    assert(!pa&&prepare_ops&&delay_count==50&&registers[0x36]==88);
    assert(registers[0x70]==0xc200&&registers[0x71]==0x3065);
    assert((registers[0x30]&(1u<<2))==0); /* MIC ADC disabled */
    AFSK_HW_Restore();assert(!registers[0x70]);
    ptt=false;mock_tim.CNT=0;mock_tim.SR=0;
    assert(AFSK_HW_Prepare(&info,66)==APP_AFSK_PTT_RELEASED&& !pa);ptt=true;
    mock_tim.SR=TIM_SR_UIF;mock_tim.CNT=7;
    assert(AFSK_HW_Now()==65543&&mask==0);
    mock_tim.SR=0;mock_tim.CNT=20;mask=1;
    assert(AFSK_HW_Now()==65556&&mask==1);mask=0;
    AFSK_HW_Arm(65560);assert(mock_tim.CCR1==24&&irq_enable);
    mock_tim.CNT=24;mock_tim.SR=TIM_SR_CC1IF;TIM2_IRQHandler();
    assert(irq_time==65560);
    AFSK_HW_ReleaseTimer();
    puts("AFSK hardware: clock/ownership, quiet setup, calibrated PA, PTT, counter wrap, IRQ deadline and mask preservation passed");
}
