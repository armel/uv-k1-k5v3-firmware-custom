/* SPDX-License-Identifier: Apache-2.0 */
#include "afsk_hw.h"
#include "afsk_tx.h"
#ifdef AFSK_TX_HW_HOST_TEST
#include "afsk_hw_mock.h"
#else
#include "py32f0xx.h"
#include "py32f071_ll_rcc.h"
#include "driver/bk4819.h"
#include "driver/bk4819-regs.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "radio.h"
#include "settings.h"
#include "functions.h"
#include "audio.h"
#include "helper/battery.h"
#include "misc.h"
#endif
#include <string.h>

/* The supported tree uses TIM6 for voice and TIM7 for backlight; TIM2 is reserved
 * exclusively by this optional service. PY32F071 TIM2 is a 16-bit timer. */
static uint32_t timer_epoch, previous_priority;
static bool timer_owned, bcl_busy;
static struct {
    const VFO_Info_t *vfo;
    uint32_t tx, rx;
    uint8_t modulation, power, bandwidth, bias, lock, bcl;
} channel_snapshot;
static uint32_t channel_token;

static uint8_t permission(const VFO_Info_t *vfo)
{
    switch (RADIO_CheckTXPermission(vfo)) {
    case VFO_STATE_NORMAL: return APP_AFSK_OK;
    case VFO_STATE_BUSY: return APP_AFSK_BUSY;
    case VFO_STATE_BAT_LOW: return APP_AFSK_LOW_BATTERY;
    case VFO_STATE_VOLTAGE_HIGH: return APP_AFSK_HIGH_VOLTAGE;
    default: return APP_AFSK_TX_DENIED;
    }
}

uint8_t AFSK_HW_Info(app_tx_info_t *out)
{
    const VFO_Info_t *vfo = gTxVfo;
    if (!vfo || !vfo->pTX || !vfo->pRX)
        return APP_AFSK_INTERNAL;
    uint8_t bandwidth = vfo->CHANNEL_BANDWIDTH;
#ifdef ENABLE_FEAT_F4HWN_NARROWER
    if (bandwidth == BK4819_FILTER_BW_NARROW && gSetting_set_nfm == 1)
        bandwidth = BK4819_FILTER_BW_NARROWER;
#endif
    /* Explicit field comparisons avoid padding and unstable struct hashing. */
    if (channel_snapshot.vfo != vfo || channel_snapshot.tx != vfo->pTX->Frequency
        || channel_snapshot.rx != vfo->pRX->Frequency
        || channel_snapshot.modulation != vfo->Modulation
        || channel_snapshot.power != vfo->OUTPUT_POWER
        || channel_snapshot.bandwidth != bandwidth
        || channel_snapshot.bias != vfo->TXP_CalculatedSetting
        || channel_snapshot.lock != vfo->TX_LOCK
        || channel_snapshot.bcl != vfo->BUSY_CHANNEL_LOCK) {
        channel_snapshot.vfo = vfo;
        channel_snapshot.tx = vfo->pTX->Frequency;
        channel_snapshot.rx = vfo->pRX->Frequency;
        channel_snapshot.modulation = vfo->Modulation;
        channel_snapshot.power = vfo->OUTPUT_POWER;
        channel_snapshot.bandwidth = bandwidth;
        channel_snapshot.bias = vfo->TXP_CalculatedSetting;
        channel_snapshot.lock = vfo->TX_LOCK;
        channel_snapshot.bcl = vfo->BUSY_CHANNEL_LOCK;
        if (++channel_token == 0u)
            ++channel_token;
    }
    *out = (app_tx_info_t){ .size = sizeof(*out), .channel_token = channel_token,
        .tx_freq_10hz = channel_snapshot.tx, .rx_freq_10hz = channel_snapshot.rx,
        .modulation = vfo->Modulation == MODULATION_FM ? APP_TX_MOD_FM :
                      vfo->Modulation == MODULATION_AM ? APP_TX_MOD_AM :
                      vfo->Modulation == MODULATION_USB ? APP_TX_MOD_USB : APP_TX_MOD_OTHER,
        .power = vfo->OUTPUT_POWER <= OUTPUT_POWER_HIGH ? vfo->OUTPUT_POWER : 255,
        .bandwidth = bandwidth <= BK4819_FILTER_BW_NARROWER ? bandwidth : APP_TX_BW_OTHER };
    if (out->tx_freq_10hz == out->rx_freq_10hz)
        out->flags = APP_TX_INFO_SIMPLEX;
    uint8_t denial = permission(vfo);
    if (!AFSK_TX_OwnsRF() &&
        (gCurrentFunction == FUNCTION_TRANSMIT ||
         BK4819_IsGpioOutSet(BK4819_GPIO1_PIN29_PA_ENABLE)))
        denial = APP_AFSK_BUSY;
    /* Foreground receive bookkeeping is suspended during modal overlays. Keep
     * BCL conservative using the selected channel's live RSSI before RX stops.
     * This first implementation accepts simplex FM only. */
    if (!AFSK_TX_OwnsRF())
        bcl_busy = vfo->BUSY_CHANNEL_LOCK &&
            (BK4819_GetRSSI() >= vfo->SquelchOpenRSSIThresh);
    if (denial == APP_AFSK_OK && bcl_busy)
        denial = APP_AFSK_BUSY;
    if (denial == APP_AFSK_OK && !(out->flags & APP_TX_INFO_SIMPLEX))
        denial = APP_AFSK_TX_DENIED;
    if (denial == APP_AFSK_BUSY)
        out->flags |= APP_TX_INFO_BUSY;
    out->denial = denial;
    return denial;
}

bool AFSK_HW_AcquireTimer(void)
{
    if (timer_owned || (RCC->APBENR1 & RCC_APBENR1_TIM2EN)
        || NVIC_GetEnableIRQ(TIM2_IRQn))
        return false;
    LL_RCC_ClocksTypeDef clocks;
    LL_RCC_GetSystemClocksFreq(&clocks);
    uint32_t input_hz = clocks.PCLK1_Frequency;
    if (LL_RCC_GetAPB1Prescaler() != LL_RCC_APB1_DIV_1)
        input_hz *= 2u;
    if (input_hz != 48000000u || clocks.HCLK_Frequency != 48000000u)
        return false;
    previous_priority = NVIC_GetPriority(TIM2_IRQn);
    RCC->APBENR1 |= RCC_APBENR1_TIM2EN;
    (void)RCC->APBENR1;
    RCC->APBRSTR1 |= RCC_APBRSTR1_TIM2RST;
    RCC->APBRSTR1 &= ~RCC_APBRSTR1_TIM2RST;
    TIM2->PSC = 399u;
    TIM2->ARR = 0xffffu;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->CNT = 0;
    TIM2->CR1 = TIM_CR1_CEN;
    timer_epoch = 0;
    timer_owned = true;
    /* Same priority as SysTick: neither handler preempts the other's BK access;
     * SysTick contains only counters/GPIO, no BK SPI transactions. */
    NVIC_SetPriority(TIM2_IRQn, 0);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    return true;
}

uint32_t AFSK_HW_Now(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint32_t count = TIM2->CNT;
    if (TIM2->SR & TIM_SR_UIF) {
        timer_epoch += 0x10000u;
        TIM2->SR = ~TIM_SR_UIF;
        count = TIM2->CNT;
    }
    uint32_t now = timer_epoch + count;
    __set_PRIMASK(mask);
    return now;
}

void AFSK_HW_Arm(uint32_t deadline)
{
    TIM2->CCR1 = deadline & 0xffffu;
    TIM2->SR = ~TIM_SR_CC1IF;
    TIM2->DIER = TIM_DIER_CC1IE | TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM2_IRQn);
}

void AFSK_HW_Stop(void)
{
    TIM2->DIER = 0;
    NVIC_DisableIRQ(TIM2_IRQn);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    /* Keep the free-running counter for the final PA-off duration snapshot. */
}

void AFSK_HW_ReleaseTimer(void)
{
    if (!timer_owned)
        return;
    AFSK_HW_Stop();
    TIM2->CR1 = 0;
    RCC->APBENR1 &= ~RCC_APBENR1_TIM2EN;
    NVIC_SetPriority(TIM2_IRQn, previous_priority);
    timer_owned = false;
}

void TIM2_IRQHandler(void)
{
    uint32_t sources = TIM2->SR;
    uint32_t now = AFSK_HW_Now();
    if (sources & TIM_SR_CC1IF) {
        TIM2->SR = ~TIM_SR_CC1IF;
        AFSK_TX_Tick(now);
    }
}

bool AFSK_HW_PTT(void) { return GPIO_IsPttPressed(); }
void AFSK_HW_PA(bool on)
{
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, on);
}
void AFSK_HW_Tone(bool space)
{
    BK4819_WriteRegister(BK4819_REG_71, space ? 0x58bau : 0x3065u);
}

uint8_t AFSK_HW_Prepare(const app_tx_info_t *info, uint8_t gain)
{
    const uint32_t start = AFSK_HW_Now();
    AFSK_HW_PA(false);
    AUDIO_AudioPathOff();
    gEnableSpeaker = false;
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);
    BK4819_EnterTxMute();
    BK4819_ExitBypass();
    BK4819_SetFilterBandwidth((BK4819_FilterBandwidth_t)info->bandwidth, false);
    BK4819_SetFrequency(info->tx_freq_10hz);
    BK4819_PickRXFilterPathBasedOnFrequency(info->tx_freq_10hz);
    BK4819_WriteRegister(BK4819_REG_36, 0);
    BK4819_WriteRegister(BK4819_REG_37, 0x9d1f);
    BK4819_WriteRegister(BK4819_REG_52, 0x028f);
    BK4819_DisableScramble();
    BK4819_DisableVox();
    BK4819_SetCompander(0);
    BK4819_DisableDTMF();
    BK4819_ExitSubAu();
    BK4819_WriteRegister(BK4819_REG_70, 0x8000u | ((uint16_t)gain << 8));
    AFSK_HW_Tone(false);
    BK4819_SetAF(BK4819_AF_MUTE);
    BK4819_EnableTXLink(); /* driver explicitly disables MIC ADC */
    BK4819_SetupPowerAmplifier(channel_snapshot.bias, info->tx_freq_10hz);
    /* Match the driver's 50 ms tone settle while PA remains off; check release
     * at every millisecond. No voice PrepareTX/PTT-id/DTMF/roger path is used. */
    const uint32_t settle_start = AFSK_HW_Now();
    while (AFSK_HW_Now() - settle_start < 50u * 120u) {
        if (!AFSK_HW_PTT())
            return APP_AFSK_PTT_RELEASED;
        if (AFSK_HW_Now() - start >= 100u * 120u)
            return APP_AFSK_TIMING;
        SYSTEM_DelayMs(1);
    }
    if (AFSK_HW_Now() - start >= 100u * 120u)
        return APP_AFSK_TIMING;
    BK4819_ExitTxMute();
    return APP_AFSK_OK;
}

void AFSK_HW_Restore(void)
{
    BK4819_EnterTxMute();
    BK4819_WriteRegister(BK4819_REG_70, 0);
    RADIO_SetupRegisters(true);
}
