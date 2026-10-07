/* Copyright 2026 Armel F4HWN
 * Licensed under the Apache License, Version 2.0.
 *
 * Labs system information overlay. Static UI strings live in the app assets;
 * enumeration and aggregation stay in the overlay to preserve firmware flash.
 */

#include <stdint.h>
#include <stdbool.h>
#include "../app_api.h"
#include "sysinfo_assets.h"

#define VISIBLE_ROWS      7u
#define ROW_HEIGHT        8u
#define SCROLL_STEP       8u
#define SCROLL_FRAME_STEP 2u
#define INPUT_TICK_MS     15u
#define NAV_REPEAT_DELAY_MS 300u
#define NAV_REPEAT_MS     120u
#define VALUE_X          54u
#define CHANNEL_ATTR_BASE 0x00008000u
#define UID_ADDRESS       0x1FFF3000u
#define CPUID_ADDRESS     0xE000ED00u
#define ADC1_ADDRESS      0x40012400u
#define DBGMCU_ID_ADDRESS 0x40015800u
#define RCC_CFGR_ADDRESS  0x40021008u
#define RCC_CSR_ADDRESS   0x40021060u
#define SYSCFG_CFGR1_ADDRESS 0x40010000u
#define FLASH_ACR_ADDRESS 0x40022000u
#define FLASH_OPTR_ADDRESS 0x40022020u
#define FLASH_BORCR_ADDRESS 0x40022024u
#define ADC_SR_EOC        (1u << 1)
#define ADC_CR2_EXTTRIG   (1u << 20)
#define ADC_CR2_SWSTART   (1u << 22)
#define ADC_CR2_TSVREFE   (1u << 23)
#define ADC_CHANNEL_MASK  0x1Fu
#define ADC_CHANNEL_TEMP  16u
#define ADC_CHANNEL_VREF  17u
#define ADC_SAMPLE_16_17_MASK (0x3Fu << 18)
#define ADC_SAMPLE_16_17_LONG (0x3Fu << 18)
#define RESET_OPTION_FLAG (1u << 25)
#define RESET_PIN_FLAG    (1u << 26)
#define RESET_POWER_FLAG  (1u << 27)
#define RESET_SOFT_FLAG   (1u << 28)
#define RESET_IWDG_FLAG   (1u << 29)
#define RESET_WWDG_FLAG   (1u << 30)
#define ROW_BLANK         0x7Fu
#define ROW_SECTION(n)    (0x80u | (n))
#define ROW_QR(wiki, page) (0x40u | ((wiki) << 3) | (page))
#define QR_WIDTH          33u
#define QR_X              47u
#define UPTIME_ROW        14u
#define RAM_FREE_FIRST_ROW 19u
#define RAM_FREE_LAST_ROW  21u
#define BATTERY_LEVEL_ROW  39u

static const uint8_t rows[] = {
    ROW_BLANK, ROW_SECTION(0), ROW_BLANK, 0, 1, 2, 3, 4, ROW_BLANK,
    ROW_SECTION(1), ROW_BLANK, 27, 7, 8, 21, 25, 26, 5, 6, 42, 43, 44,
    28, 29, 30, 31,
    32, 33, 34, 41, 35, 36, 37, 38, 39, 40,
    ROW_BLANK,
    ROW_SECTION(2), ROW_BLANK, 9, 10, ROW_BLANK,
    ROW_SECTION(3), ROW_BLANK, 11, 12, 13, 14, 15, 16, 17, 22, 23, 24,
    ROW_BLANK,
    ROW_SECTION(4), ROW_BLANK, 18, 19, 20, ROW_BLANK,
    ROW_SECTION(5), ROW_BLANK,
    ROW_QR(0, 0), ROW_QR(0, 1), ROW_QR(0, 2), ROW_QR(0, 3), ROW_QR(0, 4),
    ROW_BLANK,
    ROW_SECTION(6), ROW_BLANK,
    ROW_QR(1, 0), ROW_QR(1, 1), ROW_QR(1, 2), ROW_QR(1, 3), ROW_QR(1, 4),
};
#define ROW_COUNT ((uint16_t)sizeof(rows))

struct system_core {
    uint32_t flash_used;
    uint32_t flash_total;
    uint32_t ram_used;
    uint32_t ram_total;
    uint32_t ram_free_now;
    uint32_t ram_free_min;
    uint32_t stack_used_max;
    uint32_t uid[3];
    uint32_t cpuid;
    uint32_t silicon_rev;
    const char *edition;
    const char *version;
    const char *build_date;
    const char *build_time;
    const char *build_commit;
    uint16_t battery_voltage;
    uint16_t vdd_mv;
    int16_t temperature;
    bool temperature_valid;
    bool vdd_valid;
    uint8_t battery_percent;
    uint8_t battery_type;
    uint8_t cpu_mhz;
    uint8_t reset_source;
    uint8_t clock_source;
    uint8_t rdp_state;
    uint8_t bor_state;
    uint8_t boot_mode;
    uint8_t option_modes;
    uint8_t flash_wait_states;
};

struct globals {
    const app_api_t *api;
    struct system_core core;
    uint16_t channels_used;
    uint16_t channels_hf;
    uint16_t channels_vhf;
    uint16_t channels_uhf;
    uint16_t channels_air;
    uint16_t channels_other;
    uint16_t channels_am;
    uint16_t channels_fm;
    uint16_t channels_wide;
    uint16_t channels_narrow;
    uint16_t channels_ctcss;
    uint16_t channels_dcs;
    uint8_t lists_used;
    uint16_t scroll;
    uint16_t target;
    uint16_t row_count;
    bool running;
    bool dirty;
    bool suspended;
};
static struct globals g;
#define A (g.api)

static uint64_t divide(uint32_t n, uint32_t d)
{
    return A->uidivmod(n, d);
}

static char *put_u32(char *out, uint32_t value)
{
    char reverse[10];
    uint8_t count = 0;
    do {
        const uint64_t qr = divide(value, 10u);
        reverse[count++] = (char)('0' + (uint32_t)(qr >> 32));
        value = (uint32_t)qr;
    } while (value != 0u);
    while (count != 0u)
        *out++ = reverse[--count];
    *out = '\0';
    return out;
}

static char *put_char(char *out, char value)
{
    *out++ = value;
    *out = '\0';
    return out;
}

static char *put_2digits(char *out, uint32_t value)
{
    if (value < 10u)
        out = put_char(out, '0');
    return put_u32(out, value);
}

static char *put_text(char *out, const char *text)
{
    uint8_t length = 0u;
    while (*text && length++ < 10u)
        *out++ = *text++;
    *out = '\0';
    return out;
}

static char *put_hex32(char *out, uint32_t value)
{
    for (int8_t shift = 28; shift >= 0; shift -= 4) {
        const uint8_t digit = (uint8_t)((value >> shift) & 0x0Fu);
        *out++ = (char)(digit < 10u ? '0' + digit : 'A' + digit - 10);
    }
    *out = '\0';
    return out;
}

static uint8_t text_length(const char *text)
{
    uint8_t length = 0;
    while (text[length])
        length++;
    return length;
}

static void refresh_power(void)
{
    A->battery_sample();
    g.core.battery_voltage = *A->sys_battery_voltage;
    g.core.battery_percent = (uint8_t)A->sys_battery_percent(g.core.battery_voltage);
    g.core.battery_type = *(const uint8_t *)A->sys_battery_type;
}

static void refresh_memory(void)
{
    const uint32_t capacity = g.core.ram_total - g.core.ram_used;
    uint32_t free_now = A->sys_stack_free_now();
    uint32_t free_min = A->sys_stack_free_min();

    if (free_now > capacity)
        free_now = capacity;
    if (free_min > capacity)
        free_min = capacity;
    g.core.ram_free_now = free_now;
    g.core.ram_free_min = free_min;
    g.core.stack_used_max = capacity - free_min;
}

static bool adc_read_channel(volatile uint32_t *adc, uint8_t channel,
                             uint16_t *sample)
{
    adc[14] = (adc[14] & ~ADC_CHANNEL_MASK) | channel;
    adc[0] = ~ADC_SR_EOC;
    adc[2] |= ADC_CR2_SWSTART | ADC_CR2_EXTTRIG;

    uint16_t timeout = 0xFFFFu;
    while ((adc[0] & ADC_SR_EOC) == 0u && --timeout != 0u)
        ;
    if (timeout == 0u)
        return false;

    *sample = (uint16_t)(adc[20] & 0x0FFFu);
    return true;
}

static void refresh_temperature(void)
{
    volatile uint32_t *const adc = (volatile uint32_t *)ADC1_ADDRESS;
    const uint32_t saved_cr2 = adc[2];
    const uint32_t saved_smpr2 = adc[4];
    const uint32_t saved_sqr3 = adc[14];
    uint16_t temp_sample = 0u;
    uint16_t vref_sample = 0u;

    adc[2] = saved_cr2 | ADC_CR2_TSVREFE;
    adc[4] = (saved_smpr2 & ~ADC_SAMPLE_16_17_MASK) |
             ADC_SAMPLE_16_17_LONG;
    A->delay_ms(1u);

    const bool temp_ok = adc_read_channel(adc, ADC_CHANNEL_TEMP, &temp_sample);
    const bool vref_ok = adc_read_channel(adc, ADC_CHANNEL_VREF, &vref_sample);

    adc[14] = saved_sqr3;
    adc[4] = saved_smpr2;
    adc[2] = saved_cr2;

    if (!vref_ok || vref_sample == 0u) {
        g.core.vdd_valid = false;
        g.core.temperature_valid = false;
        return;
    }

    g.core.vdd_mv = (uint16_t)divide(4095u * 1200u, vref_sample);
    g.core.vdd_valid = true;
    if (!temp_ok) {
        g.core.temperature_valid = false;
        return;
    }

    const uint32_t sensor_mv =
        (uint32_t)divide((uint32_t)temp_sample * 1200u, vref_sample);
    if (sensor_mv >= 760u)
        g.core.temperature = (int16_t)(30u +
            (uint32_t)divide((sensor_mv - 760u) * 10u, 25u));
    else
        g.core.temperature = (int16_t)(30 - (int32_t)
            (uint32_t)divide((760u - sensor_mv) * 10u, 25u));
    g.core.temperature_valid = true;
}

static void load_system_info(void)
{
    uint32_t list_mask = 0u;
    const uint32_t reset_flags =
        *(const volatile uint32_t *)RCC_CSR_ADDRESS;

    g.core.flash_used = (uint32_t)(uintptr_t)A->sys_flash_end - 0x08002800u;
    g.core.flash_total = 118u * 1024u;
    g.core.ram_used = (uint32_t)(uintptr_t)A->sys_ram_end - 0x20000000u;
    g.core.ram_total = 16u * 1024u;
    refresh_memory();
    g.core.uid[0] = *(const uint32_t *)(UID_ADDRESS + 0u);
    g.core.uid[1] = *(const uint32_t *)(UID_ADDRESS + 4u);
    g.core.uid[2] = *(const uint32_t *)(UID_ADDRESS + 8u);
    g.core.cpuid = *(const volatile uint32_t *)CPUID_ADDRESS;
    g.core.silicon_rev = *(const volatile uint32_t *)DBGMCU_ID_ADDRESS;
    g.core.clock_source = (uint8_t)
        ((*(const volatile uint32_t *)RCC_CFGR_ADDRESS >> 3) & 7u);
    if (g.core.clock_source > 4u)
        g.core.clock_source = 5u;
    const uint32_t optr =
        *(const volatile uint32_t *)FLASH_OPTR_ADDRESS;
    const uint8_t rdp = (uint8_t)optr;
    g.core.rdp_state = rdp == 0xAAu ? 0u : rdp == 0x55u ? 1u : 2u;
    g.core.option_modes = (uint8_t)(optr >> 11);
    g.core.boot_mode = (uint8_t)
        (*(const volatile uint32_t *)SYSCFG_CFGR1_ADDRESS & 3u);
    if (g.core.boot_mode == 2u)
        g.core.boot_mode = 3u;
    else if (g.core.boot_mode == 3u)
        g.core.boot_mode = 2u;
    g.core.flash_wait_states = (uint8_t)
        (*(const volatile uint32_t *)FLASH_ACR_ADDRESS & 3u);
    const uint32_t bor = *(const volatile uint32_t *)FLASH_BORCR_ADDRESS;
    g.core.bor_state = (bor & (1u << 5)) != 0u
        ? (uint8_t)(((bor >> 13) & 7u) + 1u) : 0u;
    if (reset_flags & RESET_IWDG_FLAG)
        g.core.reset_source = 3u;
    else if (reset_flags & RESET_WWDG_FLAG)
        g.core.reset_source = 4u;
    else if (reset_flags & RESET_SOFT_FLAG)
        g.core.reset_source = 2u;
    else if (reset_flags & RESET_POWER_FLAG)
        g.core.reset_source = 0u;
    else if (reset_flags & RESET_OPTION_FLAG)
        g.core.reset_source = 5u;
    else if (reset_flags & RESET_PIN_FLAG)
        g.core.reset_source = 1u;
    else
        g.core.reset_source = 6u;
    g.core.edition = A->sys_edition;
    g.core.version = A->sys_version;
    g.core.build_date = A->sys_build_date;
    g.core.build_time = A->sys_build_time;
    g.core.build_commit = A->sys_build_commit;
    g.core.cpu_mhz = 48u;
    refresh_power();
    refresh_temperature();

    for (uint16_t index = 0u; index < 1024u; index++) {
        uint16_t attributes;
        A->sys_storage_read(CHANNEL_ATTR_BASE + (uint32_t)index * 2u,
                            &attributes, sizeof(attributes));
        const uint8_t band = (uint8_t)(attributes & 0x0007u);
        if (attributes == 0xFFFFu || band > 6u)
            continue;

        struct {
            uint32_t frequency;
            uint32_t offset;
            uint8_t data[8];
        } channel;
        A->sys_storage_read((uint32_t)index * 16u, &channel, sizeof(channel));
        if (channel.frequency == 0u || channel.frequency == 0xFFFFFFFFu)
            continue;

        g.channels_used++;
        if (band == 1u) {
            g.channels_air++;
        } else if (band == 0u) {
            if (channel.frequency >= 300000u && channel.frequency < 3000000u)
                g.channels_hf++;
            else if (channel.frequency >= 3000000u)
                g.channels_vhf++;
            else
                g.channels_other++;
        } else if (band <= 2u ||
                   (band == 3u && channel.frequency < 30000000u)) {
            g.channels_vhf++;
        } else if (channel.frequency < 300000000u) {
            g.channels_uhf++;
        } else {
            g.channels_other++;
        }

        const uint8_t modulation = channel.data[3] >> 4;
        if (modulation == 1u)
            g.channels_am++;
        else if (modulation == 0u || modulation >= 3u)
            g.channels_fm++;

        const uint8_t shape = channel.data[4];
        if (shape == 0xFFu) {
            g.channels_wide++;
        } else {
            if (shape & 0x02u)
                g.channels_narrow++;
            else
                g.channels_wide++;
        }

        const uint8_t rx_code = channel.data[2] & 0x0Fu;
        const uint8_t tx_code = channel.data[2] >> 4;
        if (rx_code == 1u || tx_code == 1u)
            g.channels_ctcss++;
        if (rx_code == 2u || rx_code == 3u ||
            tx_code == 2u || tx_code == 3u)
            g.channels_dcs++;

        if (attributes & 0x0080u)
            continue;
        const uint8_t scanlist = (uint8_t)(attributes >> 8);
        if (scanlist >= 1u && scanlist <= 24u)
            list_mask |= 1u << (scanlist - 1u);
        else if (scanlist == 25u)
            list_mask = 0x00FFFFFFu;
    }

    while (list_mask != 0u) {
        g.lists_used += (uint8_t)(list_mask & 1u);
        list_mask >>= 1;
    }
}

__attribute__((noinline, noclone))
static void format_size(char *out, uint32_t bytes, uint32_t total)
{
    uint64_t qr = divide(bytes, 1024u);
    out = put_u32(out, (uint32_t)qr);
    out = put_char(out, '.');
    out = put_u32(out, (uint32_t)divide((uint32_t)(qr >> 32) * 10u, 1024u));
    if (total != 0u) {
        out = put_char(out, '/');
        out = put_u32(out, (uint32_t)divide(total, 1024u));
    }
    put_char(out, 'K');
}

static char *format_pair(char *out, uint16_t value, uint16_t total)
{
    out = put_u32(out, value);
    out = put_char(out, '/');
    return put_u32(out, total);
}

static void format_voltage(char *out, uint16_t millivolts)
{
    const uint64_t qr = divide(millivolts, 1000u);
    out = put_u32(out, (uint32_t)qr);
    out = put_char(out, '.');
    const uint32_t hundredths =
        (uint32_t)divide((uint32_t)(qr >> 32), 10u);
    if (hundredths < 10u)
        out = put_char(out, '0');
    out = put_u32(out, hundredths);
    put_char(out, 'V');
}

static void format_uptime(char *out)
{
    uint32_t seconds = (uint32_t)divide(A->ticks_ms(), 1000u);
    uint64_t qr = divide(seconds, 60u);
    const uint32_t second = (uint32_t)(qr >> 32);
    qr = divide((uint32_t)qr, 60u);
    const uint32_t minute = (uint32_t)(qr >> 32);
    const uint32_t hour = (uint32_t)qr;

    out = put_u32(out, hour);
    out = put_char(out, ':');
    out = put_2digits(out, minute);
    out = put_char(out, ':');
    put_2digits(out, second);
}

static char *put_build_date(char *out, const char *date)
{
    *out++ = date[4] == ' ' ? '0' : date[4];
    *out++ = date[5];
    *out++ = ' ';
    *out++ = date[0];
    *out++ = date[1];
    *out++ = date[2];
    *out++ = ' ';
    *out++ = date[9];
    *out++ = date[10];
    *out = '\0';
    return out;
}

static void format_item_value(uint8_t item, char *out)
{
    switch (item) {
    case 0:  put_text(out, g.core.edition); break;
    case 1: {
        const char *version = g.core.version;
        while (*version && *version != ' ')
            version++;
        if (*version == ' ')
            version++;
        put_text(out, version);
        break;
    }
    case 2:  put_build_date(out, g.core.build_date); break;
    case 3:  put_text(out, g.core.build_time); break;
    case 4:  put_text(out, g.core.build_commit); break;
    case 5:  format_size(out, g.core.flash_used, g.core.flash_total); break;
    case 6:  format_size(out, g.core.ram_used, g.core.ram_total); break;
    case 7:
        out = put_u32(out, g.core.cpu_mhz);
        out = put_char(out, 'M');
        out = put_char(out, 'H');
        put_char(out, 'z');
        break;
    case 8:
        out = put_char(out, 'A');
        out = put_char(out, 'B');
        out = put_char(out, 'I');
        out = put_u32(out, A->abi_major);
        out = put_char(out, '/');
        out = put_char(out, 'A');
        out = put_char(out, 'P');
        out = put_char(out, 'I');
        put_u32(out, A->api_level);
        break;
    case 9: {
        const uint64_t qr = divide(g.core.battery_voltage, 100u);
        out = put_u32(out, (uint32_t)qr);
        out = put_char(out, '.');
        const uint8_t decimals = (uint8_t)(qr >> 32);
        if (decimals < 10u) out = put_char(out, '0');
        out = put_u32(out, decimals);
        out = put_char(out, 'V');
        out = put_char(out, ' ');
        out = put_u32(out, g.core.battery_percent);
        put_char(out, '%');
        break;
    }
    case 10: {
        const uint8_t type = g.core.battery_type < 6u ? g.core.battery_type : 5u;
        A->asset_read(T_BATTERY + type * T_BATTERY_STRIDE, out,
                      T_BATTERY_STRIDE);
        break;
    }
    case 11: format_pair(out, g.channels_used, 1024u); break;
    case 12: format_pair(out, g.lists_used, 24u); break;
    case 13: put_u32(out, g.channels_hf); break;
    case 14: put_u32(out, g.channels_vhf); break;
    case 15: put_u32(out, g.channels_uhf); break;
    case 16: put_u32(out, g.channels_air); break;
    case 17: put_u32(out, g.channels_other); break;
    case 18: put_hex32(out, g.core.uid[0]); break;
    case 19: put_hex32(out, g.core.uid[1]); break;
    case 20: put_hex32(out, g.core.uid[2]); break;
    case 21: format_uptime(out); break;
    case 22: format_pair(out, g.channels_am, g.channels_fm); break;
    case 23: format_pair(out, g.channels_wide, g.channels_narrow); break;
    case 24: format_pair(out, g.channels_ctcss, g.channels_dcs); break;
    case 25: put_hex32(out, g.core.cpuid); break;
    case 26:
        if (!g.core.temperature_valid) {
            out = put_char(out, '-');
            out = put_char(out, '-');
        } else if (g.core.temperature < 0) {
            out = put_char(out, '-');
            out = put_u32(out, (uint32_t)-g.core.temperature);
        } else {
            out = put_u32(out, (uint32_t)g.core.temperature);
        }
        put_char(out, 'C');
        break;
    case 27: A->asset_read(T_CPU, out, T_CPU_LEN); break;
    case 28:
        A->asset_read(T_RESET + g.core.reset_source * T_RESET_STRIDE, out,
                      T_RESET_STRIDE);
        break;
    case 29: put_hex32(out, g.core.silicon_rev); break;
    case 30:
        A->asset_read(T_CLOCK + g.core.clock_source * T_CLOCK_STRIDE, out,
                      T_CLOCK_STRIDE);
        break;
    case 31:
        A->asset_read(T_RDP + g.core.rdp_state * T_RDP_STRIDE, out,
                      T_RDP_STRIDE);
        break;
    case 32:
        if (g.core.vdd_valid)
            format_voltage(out, g.core.vdd_mv);
        else {
            out = put_char(out, '-');
            put_char(out, '-');
        }
        break;
    case 33:
        A->asset_read(T_BOR + g.core.bor_state * T_BOR_STRIDE, out,
                      T_BOR_STRIDE);
        break;
    case 34: A->asset_read(T_HW_FLASH, out, T_HW_FLASH_LEN); break;
    case 35:
        A->asset_read(T_BOOT + g.core.boot_mode * T_BOOT_STRIDE, out,
                      T_BOOT_STRIDE);
        break;
    case 36:
        A->asset_read(T_WDG + (g.core.option_modes & 1u) * T_WDG_STRIDE, out,
                      T_WDG_STRIDE);
        break;
    case 37:
        if (g.core.flash_wait_states <= 2u) {
            out = put_u32(out, g.core.flash_wait_states);
            out = put_char(out, ' ');
            out = put_char(out, 'W');
            put_char(out, 'S');
        } else {
            out = put_char(out, '-');
            put_char(out, '-');
        }
        break;
    case 38:
        A->asset_read(T_WDG + ((g.core.option_modes >> 1) & 1u) * T_WDG_STRIDE,
                      out, T_WDG_STRIDE);
        break;
    case 39:
        A->asset_read(T_NRST + ((g.core.option_modes >> 2) & 1u) * T_NRST_STRIDE,
                      out, T_NRST_STRIDE);
        break;
    case 40:
        A->asset_read(T_IWDG_STOP + ((g.core.option_modes >> 4) & 1u) *
                      T_IWDG_STOP_STRIDE, out, T_IWDG_STOP_STRIDE);
        break;
    case 41: A->asset_read(T_SPI_FLASH, out, T_SPI_FLASH_LEN); break;
    case 42: format_size(out, g.core.ram_free_now, 0u); break;
    case 43: format_size(out, g.core.ram_free_min, 0u); break;
    case 44: format_size(out, g.core.stack_used_max, 0u); break;
    default: *out = '\0'; break;
    }
    out[10] = '\0';
}

static void draw_tiny_capsule(const char *text, uint8_t line, bool status)
{
    const uint8_t end = (uint8_t)(2u + text_length(text) * 4u);
    A->print_inverse(text, 2u, line, status, true, end);
}

static void draw_section_capsule(const char *text, uint8_t line, bool close_top)
{
    const uint8_t start = 2u;
    const uint8_t end = (uint8_t)(start + text_length(text) * 7u + 1u);

    A->print_normal(text, start, 0u, line);
    A->fb[line][start - 1u] ^= 0x7Fu;
    for (uint8_t x = start; x < end; x++)
        A->fb[line][x] ^= 0xFFu;

    if (line != 0u) {
        for (uint8_t x = 1u; x < 127u; x += 2u)
            A->fb[line - 1u][x] |= 0x08u;
        for (uint8_t x = start; x < end; x++)
            A->fb[line - 1u][x] ^= 0x80u;
    }
    A->fb[line][end] ^= 0x7Fu;

    if (line == 0u && close_top) {
        /* Move the complete nine-pixel capsule into the visible area. Every
         * section is followed by a blank row, which receives its last pixel. */
        for (uint8_t x = start - 1u; x <= end; x++) {
            const uint8_t body = A->fb[0][x];
            A->fb[0][x] = (uint8_t)(body << 1);
            A->fb[1][x] |= body >> 7;
        }
        for (uint8_t x = start; x < end; x++)
            A->fb[0][x] |= 0x01u;
    }
}

static void draw_row(uint16_t row, uint8_t line, char *text, bool close_top)
{
    const uint8_t definition = rows[row];
    if (definition == ROW_BLANK)
        return;

    if (definition & 0x80u) {
        const uint8_t section = definition & 0x7Fu;
        A->asset_read(T_SECTION + section * T_SECTION_STRIDE, text,
                      T_SECTION_STRIDE);
        draw_section_capsule(text, line, close_top);
        return;
    }

    if ((definition & 0xF0u) == 0x40u) {
        const uint16_t qr = (definition & 0x08u) ? QR_WIKI : QR_CODE;
        const uint8_t page = definition & 0x07u;
        A->asset_read(qr + (uint16_t)page * QR_WIDTH,
                      &A->fb[line][QR_X], QR_WIDTH);
        return;
    }

    char *value = text + 12u;
    A->asset_read(T_KEY + definition * T_KEY_STRIDE, text, T_KEY_STRIDE);
    format_item_value(definition, value);
    draw_tiny_capsule(text, line, false);
    A->print_normal(value, VALUE_X, 0u, line);
}

static void shift_frame(uint8_t pixels, const uint8_t *next_row)
{
    for (uint8_t line = 0u; line < VISIBLE_ROWS; line++) {
        for (uint8_t x = 0u; x < 128u; x++) {
            const uint8_t next = line + 1u < VISIBLE_ROWS
                ? A->fb[line + 1u][x] : next_row[x];
            A->fb[line][x] = (uint8_t)((A->fb[line][x] >> pixels) |
                                      (next << (ROW_HEIGHT - pixels)));
        }
    }
}

static void draw_status(void)
{
    char title[T_TITLE_LEN];

    A->status_clear();
    A->asset_read(T_TITLE, title, T_TITLE_LEN);
    draw_tiny_capsule(title, 0u, true);
    A->draw_battery();
    A->blit_status();
}

static void draw(void)
{
    char text[32];
    uint8_t next_row[128];
    const uint16_t first = g.scroll / ROW_HEIGHT;
    const uint8_t phase = (uint8_t)(g.scroll & (ROW_HEIGHT - 1u));

    if (first <= RAM_FREE_LAST_ROW &&
        first + VISIBLE_ROWS > RAM_FREE_FIRST_ROW)
        refresh_memory();

    draw_status();

    if (phase != 0u) {
        A->display_clear();
        const uint16_t next = first + VISIBLE_ROWS;
        if (next < g.row_count)
            draw_row(next, 0u, text, false);
        for (uint8_t x = 0u; x < 128u; x++)
            next_row[x] = A->fb[0][x];
    }

    A->display_clear();
    for (uint8_t visible = 0; visible < VISIBLE_ROWS; visible++) {
        const uint16_t row = first + visible;
        if (row >= g.row_count)
            break;
        draw_row(row, visible, text, phase == 0u && visible == 0u);
    }
    if (phase != 0u)
        shift_frame(phase, next_row);
    A->blit_full();
}

static void navigate(int8_t direction)
{
    const int32_t maximum = (int32_t)(g.row_count - VISIBLE_ROWS) * ROW_HEIGHT;
    int32_t target = (int32_t)g.target + (int32_t)direction * SCROLL_STEP;
    if (target < 0)
        target = 0;
    if (target > maximum)
        target = maximum;
    g.target = (uint16_t)target;
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    load_system_info();
    g.row_count = ROW_COUNT;
    g.running = true;
    g.dirty = true;
    A->backlight_on();

    uint8_t previous = APP_KEY_INVALID;
    uint8_t battery_ticks = 0;
    uint16_t repeat_ms = 0u;
    while (g.running) {
        const uint8_t key = A->get_key();
        if (key == APP_KEY_SAVER) {
            previous = APP_KEY_INVALID;
            repeat_ms = 0u;
            g.suspended = true;
        } else if (key == APP_KEY_WAKE) {
            previous = APP_KEY_INVALID;
            repeat_ms = 0u;
            g.suspended = false;
            refresh_power();
            refresh_temperature();
            battery_ticks = 0u;
            g.dirty = true;
        } else if (key == APP_KEY_INVALID) {
            previous = key;
            repeat_ms = 0u;
        } else if (key != previous) {
            const int8_t direction = (key == APP_KEY_DOWN) - (key == APP_KEY_UP);
            A->backlight_on();
            if (key == APP_KEY_EXIT)
                g.running = false;
            else if (direction != 0)
                navigate(direction);
            else if (key != APP_KEY_PTT)
                A->play_tone(500u, 60u);
            previous = key;
            repeat_ms = direction != 0 ? NAV_REPEAT_DELAY_MS : 0u;
        } else {
            const int8_t direction = (key == APP_KEY_DOWN) - (key == APP_KEY_UP);
            if (direction != 0) {
                if (repeat_ms > INPUT_TICK_MS) {
                    repeat_ms -= INPUT_TICK_MS;
                } else {
                    A->backlight_on();
                    navigate(direction);
                    repeat_ms = NAV_REPEAT_MS;
                }
            }
        }

        if (!g.suspended) {
            if (g.scroll < g.target) {
                g.scroll += SCROLL_FRAME_STEP;
                g.dirty = true;
            }
            if (g.scroll > g.target) {
                g.scroll -= SCROLL_FRAME_STEP;
                g.dirty = true;
            }
            if (g.dirty) { draw(); g.dirty = false; }
        }

        A->delay_ms(INPUT_TICK_MS);
        if (!g.suspended && ++battery_ticks >= 67u) {
            const uint16_t first = g.scroll / ROW_HEIGHT;
            battery_ticks = 0u;
            refresh_power();
            if ((first <= UPTIME_ROW &&
                 first + VISIBLE_ROWS > UPTIME_ROW) ||
                (first <= RAM_FREE_LAST_ROW &&
                 first + VISIBLE_ROWS > RAM_FREE_FIRST_ROW) ||
                (first <= BATTERY_LEVEL_ROW &&
                 first + VISIBLE_ROWS > BATTERY_LEVEL_ROW))
                g.dirty = true;
            else
                draw_status();
        }
        A->backlight_update();
    }
}
