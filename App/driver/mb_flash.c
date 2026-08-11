/* Copyright 2026 F4HWN
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

#include <string.h>

#include "driver/mb_flash.h"

#include "py32f0xx.h"
#include "driver/py25q16.h"
#include "driver/st7565.h"
#include "ui/helper.h"
#include "version.h"

/* Internal-flash program/erase keys (FLASH_KEY1 / FLASH_KEY2). */
#define MB_FLASH_KEY1   0x45670123u
#define MB_FLASH_KEY2   0xCDEF89ABu

/* Internal flash granularity (PY32F071xB): program & page-erase = 256 bytes. */
#define MB_FLASH_PAGE   256u

/* External SPI flash chip-select is on PA3 (see driver/py25q16.c). */
#define MB_CS_PIN       (1u << 3)

/* LCD control pins used only for RAM-resident progress updates. */
#define MB_LCD_CS_PIN   (1u << 2)  /* PB2 */
#define MB_LCD_A0_PIN   (1u << 6)  /* PA6 */

/* Rounded progress gauge geometry, matching ScanProgress_DrawGaugeLine(). */
#define MB_PROGRESS_COLS        118u
#define MB_PROGRESS_FIRST_COL     5u
#define MB_PROGRESS_FILLED      0x2Du

/* Number of erase/program retries per page before giving up (and resetting
 * anyway - the region is already erased, so USB recovery is the only option). */
#define MB_PAGE_RETRIES 3u

/* Bounded waits used by the RAM-only copier. A timeout forces an immediate
 * reset instead of hanging forever with IRQs disabled. */
#define MB_RAM_SPI_TIMEOUT    100000u
#define MB_RAM_FLASH_TIMEOUT 10000000u

/*
 * Factory flash-timing parameter records, held in Puya system memory.
 * Mirror of the HAL's _FlashTimmingParam[] table (the HAL module is not built
 * in this project). Indexed by the HSI frequency setting (RCC->ICSCR HSI_FS).
 * Each entry is the address of a 5-word record read at +0/+8/+16/+24/+32.
 */
static const uint32_t mb_flash_timing[8] = {
    0x1FFF3238, 0x1FFF3260, 0x1FFF3288, 0x1FFF32B0,
    0x1FFF32D8, 0x1FFF3238, 0x1FFF3238, 0x1FFF3238
};

/* -------------------------------------------------------------------------- */
/* Helpers (flash-resident).                                                  */
/* -------------------------------------------------------------------------- */

/* zlib/PNG CRC-32 (poly 0xEDB88320), streaming. Seed 'crc' with 0xFFFFFFFF and
 * XOR the final result with 0xFFFFFFFF. No lookup table (saves flash). */
static uint32_t mb_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    while (len--)
    {
        crc ^= *data++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static void mb_copy_str(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (src)
        for (; i + 1 < cap && src[i]; i++)
            dst[i] = src[i];
    for (; i < cap; i++)
        dst[i] = 0;
}

/* -------------------------------------------------------------------------- */
/* Flash-resident preparation (runs while the flash is still readable).       */
/* -------------------------------------------------------------------------- */

static void MB_PrepareInternalFlash(void)
{
    /* Unlock the internal flash control register. */
    if (FLASH->CR & FLASH_CR_LOCK)
    {
        FLASH->KEYR = MB_FLASH_KEY1;
        FLASH->KEYR = MB_FLASH_KEY2;
    }

    /* Program/erase timing sequence (factory calibrated), replicating
     * __HAL_FLASH_TIMMING_SEQUENCE_CONFIG(). These registers persist, so it is
     * enough to set them once here, before the RAM copier starts erasing. */
    uint32_t base = mb_flash_timing[(RCC->ICSCR & RCC_ICSCR_HSI_FS) >> RCC_ICSCR_HSI_FS_Pos];
    uint32_t p0 = *(volatile uint32_t *)(base + 0);
    uint32_t p1 = *(volatile uint32_t *)(base + 8);
    uint32_t p2 = *(volatile uint32_t *)(base + 16);
    uint32_t p3 = *(volatile uint32_t *)(base + 24);
    uint32_t p4 = *(volatile uint32_t *)(base + 32);

    FLASH->TS0     =  p0 & 0xFFu;
    FLASH->TS1     = (p0 >> 16) & 0x1FFu;
    FLASH->TS3     = (p0 >> 8) & 0xFFu;
    FLASH->TS2P    =  p1 & 0xFFu;
    FLASH->TPS3    = (p1 >> 16) & 0x7FFu;
    FLASH->PERTPE  =  p2 & 0x1FFFFu;
    FLASH->SMERTPE =  p3 & 0x1FFFFu;
    FLASH->PRGTPE  =  p4 & 0xFFFFu;
    FLASH->PRETPE  = (p4 >> 16) & 0x3FFFu;
}

/* -------------------------------------------------------------------------- */
/* RAM-resident copier.                                                       */
/*                                                                            */
/* This runs while the internal application flash is being erased/programmed, */
/* during which the flash bus is unavailable. It must therefore NOT fetch any */
/* code from flash nor read any flash data: it uses raw register access only  */
/* (no external calls), reads the source from the external SPI flash in       */
/* polled mode, and resets the MCU when done. It is placed in .RamFunc, which */
/* the linker stores in flash and the startup copies to RAM alongside .data.  */
/* -------------------------------------------------------------------------- */

/* Polled single-byte SPI2 transfer. always_inline keeps all code in .RamFunc.
 * The result is returned through out so timeout and received 0xFF remain
 * distinguishable. */
__attribute__((always_inline)) static inline bool mb_ram_spi(uint8_t v, uint8_t *out)
{
    uint32_t timeout = MB_RAM_SPI_TIMEOUT;
    while (!(SPI2->SR & SPI_SR_TXE))
        if (!--timeout)
            return false;

    *(volatile uint8_t *)&SPI2->DR = v;

    timeout = MB_RAM_SPI_TIMEOUT;
    while (!(SPI2->SR & SPI_SR_RXNE))
        if (!--timeout)
            return false;

    *out = *(volatile uint8_t *)&SPI2->DR;
    return true;
}

__attribute__((always_inline)) static inline bool mb_ram_flash_idle(void)
{
    uint32_t timeout = MB_RAM_FLASH_TIMEOUT;
    while (FLASH->SR & FLASH_SR_BSY)
        if (!--timeout)
            return false;
    return true;
}

__attribute__((always_inline, noreturn)) static inline void mb_ram_reset(void)
{
    __DSB();

    SCB->AIRCR = (0x5FAu << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
    __DSB();
    for (;;) { }
}

/* Minimal SPI1 LCD writer. A display timeout merely disables progress updates:
 * it must never abort or delay the safety-critical flash copy. */
__attribute__((always_inline)) static inline bool mb_ram_lcd_spi(uint8_t v)
{
    uint32_t timeout = MB_RAM_SPI_TIMEOUT;
    while (!(SPI1->SR & SPI_SR_TXE))
        if (!--timeout)
            return false;
    *(volatile uint8_t *)&SPI1->DR = v;

    timeout = MB_RAM_SPI_TIMEOUT;
    while (!(SPI1->SR & SPI_SR_RXNE))
        if (!--timeout)
            return false;
    (void)*(volatile uint8_t *)&SPI1->DR;
    return true;
}

__attribute__((always_inline)) static inline bool mb_ram_progress_blit(const uint8_t *line)
{
    uint32_t ok = 1u;
    GPIOB->BRR = MB_LCD_CS_PIN;
    GPIOA->BRR = MB_LCD_A0_PIN;       /* command */

    if (!mb_ram_lcd_spi(0xB7u) ||     /* LCD page 7 */
        !mb_ram_lcd_spi(0x10u) ||     /* column high nibble */
        !mb_ram_lcd_spi(0x04u))       /* visible RAM starts at column 4 */
        ok = 0u;

    GPIOA->BSRR = MB_LCD_A0_PIN;      /* data */
    if (ok)
        for (uint32_t i = 0; i < 128u; i++)
            if (!mb_ram_lcd_spi(line[i]))
            {
                ok = 0u;
                break;
            }
    GPIOB->BSRR = MB_LCD_CS_PIN;
    return ok != 0u;
}

__attribute__((section(".RamFunc"), noinline, used))
static void MB_RamReflash(uint32_t intAddr, uint32_t extAddr, uint32_t imageSize,
                          uint8_t *progressLine)
{
    /* 4-byte aligned so the 64-word page program can read it as uint32_t
     * (Cortex-M0+ cannot do unaligned word accesses). */
    uint8_t buf[MB_FLASH_PAGE] __attribute__((aligned(4)));
    uint32_t remaining = imageSize;
    uint32_t regionRemaining = MB_INT_APP_SIZE;
    uint32_t pagesDone = 0;
    uint32_t progressAccumulator = 0;
    uint32_t progressFilled = 0;
    uint32_t lcdEnabled = progressLine != NULL;


    __disable_irq();

    if (FLASH->CR & FLASH_CR_LOCK)
    {
        FLASH->KEYR = MB_FLASH_KEY1;
        FLASH->KEYR = MB_FLASH_KEY2;
    }

    FLASH->SR = FLASH_SR_EOP | FLASH_SR_WRPERR | FLASH_SR_OPTVERR;

    /* Rebuild the complete application region. Bytes past imageSize are never
     * read from the external slot: they are forced to erased 0xFF, preventing
     * unvalidated padding or remnants of an older, longer firmware. */
    while (regionRemaining >= MB_FLASH_PAGE)
    {
        uint32_t readSize = remaining < MB_FLASH_PAGE ? remaining : MB_FLASH_PAGE;
        uint32_t needProgram = 0;
        uint32_t success = 0;
        uint8_t ignored;

        /* Volatile stores prevent GCC from replacing this loop with a call
         * to flash-resident memset while the application flash is unavailable. */
        volatile uint8_t *fill = buf;
        for (uint32_t i = 0; i < MB_FLASH_PAGE; i++)
            fill[i] = 0xFFu;

        if (readSize)
        {
            /* Read only CRC-validated image bytes. The rest of the page stays
             * 0xFF when imageSize is not page-aligned. */
            GPIOA->BRR = MB_CS_PIN;             /* CS low */
            if (!mb_ram_spi(0x03u, &ignored) ||
                !mb_ram_spi((extAddr >> 16) & 0xFFu, &ignored) ||
                !mb_ram_spi((extAddr >> 8) & 0xFFu, &ignored) ||
                !mb_ram_spi(extAddr & 0xFFu, &ignored))
                goto fatal_reset;

            for (uint32_t i = 0; i < readSize; i++)
                if (!mb_ram_spi(0xFFu, &buf[i]))
                    goto fatal_reset;

            GPIOA->BSRR = MB_CS_PIN;            /* CS high */
        }

        for (uint32_t i = 0; i < MB_FLASH_PAGE; i++)
        {
            if (buf[i] != 0xFFu)
            {
                needProgram = 1u;
                break;
            }
        }

        for (uint32_t retry = 0; retry < MB_PAGE_RETRIES; retry++)
        {
            const uint32_t   *src = (const uint32_t *)(const void *)buf;
            volatile uint32_t *dst = (volatile uint32_t *)intAddr;

            uint32_t          i;
            uint32_t          ok = 1u;

            /* --- page erase (256 bytes) --- */
            if (!mb_ram_flash_idle())
                goto fatal_reset;
            FLASH->CR |= FLASH_CR_PER;
            *(volatile uint32_t *)intAddr = 0xFFFFFFFFu;
            if (!mb_ram_flash_idle())
                goto fatal_reset;
            FLASH->CR &= ~FLASH_CR_PER;
            FLASH->SR = FLASH_SR_EOP | FLASH_SR_WRPERR | FLASH_SR_OPTVERR;

            if (needProgram)
            {
                /* Page program: 64 words, PGSTRT before the last word. */
                FLASH->CR |= FLASH_CR_PG;
                for (i = 0; i < 64u; i++)
                {
                    dst[i] = src[i];
                    if (i == 62u)
                        FLASH->CR |= FLASH_CR_PGSTRT;
                }
                if (!mb_ram_flash_idle())
                    goto fatal_reset;
                FLASH->CR &= ~FLASH_CR_PG;
                FLASH->SR = FLASH_SR_EOP | FLASH_SR_WRPERR | FLASH_SR_OPTVERR;
            }

            /* --- verify (read-back compare) --- */
            for (i = 0; i < 64u; i++)
            {
                if (dst[i] != src[i])
                {
                    ok = 0u;
                    break;
                }
            }
            if (ok)
            {
                success = 1u;
                break;
            }
        }

        /* Never silently continue after an unprogrammable page. Returning to
         * flash-resident code is unsafe once the application has been erased. */
        if (!success)
            goto fatal_reset;

        /* Advance the gauge without division (which could call a helper
         * in erased flash). Refresh once per 8 KiB internal sector. */
        if (lcdEnabled)
        {
            pagesDone++;
            progressAccumulator += MB_PROGRESS_COLS;
            while (progressAccumulator >= (MB_INT_APP_SIZE / MB_FLASH_PAGE))
            {
                progressAccumulator -= (MB_INT_APP_SIZE / MB_FLASH_PAGE);
                if (progressFilled < MB_PROGRESS_COLS)
                {
                    progressLine[MB_PROGRESS_FIRST_COL + progressFilled] = MB_PROGRESS_FILLED;
                    progressFilled++;
                }
            }
            if ((pagesDone & 31u) == 0u || regionRemaining == MB_FLASH_PAGE)
                lcdEnabled = mb_ram_progress_blit(progressLine);
        }

        intAddr += MB_FLASH_PAGE;
        extAddr += readSize;
        remaining -= readSize;
        regionRemaining -= MB_FLASH_PAGE;
    }

    FLASH->CR |= FLASH_CR_LOCK;
    mb_ram_reset();

fatal_reset:
    /* Release the external flash and reset immediately. If failure happened
     * after an erase, the factory USB/DFU bootloader remains the recovery path. */
    GPIOA->BSRR = MB_CS_PIN;
    FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_PG | FLASH_CR_PGSTRT);
    FLASH->CR |= FLASH_CR_LOCK;
    mb_ram_reset();
}

/* -------------------------------------------------------------------------- */
/* Public API.                                                                */
/* -------------------------------------------------------------------------- */

void MB_BackupToSlot0(uint32_t *out_size, uint32_t *out_crc32)
{
    const uint32_t imgBase = MB_SLOT0_EXT_BASE + MB_SLOT_IMG_OFFSET;
    const uint32_t size    = MB_INT_APP_SIZE;      /* full region (self-test) */

    /* CRC-32 of the internal image (memory-mapped, contiguous read). */
    uint32_t crc = mb_crc32_update(0xFFFFFFFFu, (const uint8_t *)MB_INT_APP_BASE, size)
                   ^ 0xFFFFFFFFu;

    /* Write the image first ... */
    PY25Q16_WriteBuffer(imgBase, (const void *)MB_INT_APP_BASE, size, false);

    /* ... then a valid header (COMMITTED) last, so a committed header always
     * implies a fully written image. */
    mb_slot_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = MB_SLOT_MAGIC;
    hdr.hdr_version = MB_HDR_VERSION;
    hdr.flags       = MB_FLAG_COMMITTED;
    hdr.image_size  = size;
    hdr.image_crc32 = crc;
#ifdef ENABLE_FEAT_F4HWN
    mb_copy_str(hdr.name, MB_NAME_LEN, Edition);
#else
    mb_copy_str(hdr.name, MB_NAME_LEN, "slot0");
#endif
    mb_copy_str(hdr.fw_version, MB_VERSION_LEN, Version);
    PY25Q16_WriteBuffer(MB_SLOT0_EXT_BASE, &hdr, sizeof(hdr), false);

    if (out_size)  *out_size  = size;
    if (out_crc32) *out_crc32 = crc;
}

/* Set when a polled SPI wait below times out (external flash unresponsive). */
static volatile int mb_spi_err;

/* Polled single-byte SPI2 transfer (flash-resident; runs in normal context).
 * Bounded so a wedged SPI can never freeze the firmware: on timeout it sets
 * mb_spi_err and returns 0xFF, letting the caller fail gracefully. */
#define MB_SPI_TIMEOUT 20000u   /* ~a few ms max per byte; a healthy transfer
                                 * completes in well under a microsecond */
static uint8_t mb_spi_byte(uint8_t v)
{
    uint32_t to = MB_SPI_TIMEOUT;
    while (!(SPI2->SR & SPI_SR_TXE)) { if (!--to) { mb_spi_err = 1; return 0xFFu; } }
    *(volatile uint8_t *)&SPI2->DR = v;
    to = MB_SPI_TIMEOUT;
    while (!(SPI2->SR & SPI_SR_RXNE)) { if (!--to) { mb_spi_err = 1; return 0xFFu; } }
    return *(volatile uint8_t *)&SPI2->DR;
}

/*
 * Put SPI2 into clean polled mode before a manual read. The flash driver leaves
 * SPI2 in "DMA mode": the RX/TX DMA requests stay on and the DMA channels stay
 * armed (confirmed by the SPI-state diagnostic: RD.EN=1 WR.EN=1). A polled read
 * then loses every received byte to the still-armed DMA, so RXNE never sets and
 * the read hangs forever. Disabling the DMA requests + channels and draining the
 * RX FIFO restores plain polled behaviour. The next driver operation re-arms DMA
 * on its own, so this is safe.
 */
static void mb_spi_polled_mode(void)
{
    SPI2->CR2 &= ~(SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    DMA1_Channel4->CCR &= ~DMA_CCR_EN;
    DMA1_Channel5->CCR &= ~DMA_CCR_EN;
    /* Drain any pending RX. Bounded: the RX FIFO is only a few bytes deep, so a
     * stuck/overrun RXNE (which would otherwise loop forever) can't hang here. */
    for (uint32_t guard = 64; (SPI2->SR & SPI_SR_RXNE) && guard; guard--)
        (void)*(volatile uint8_t *)&SPI2->DR;
}

/*
 * CRC-32 of `len` bytes of external flash starting at `addr`, read in ONE
 * continuous polled transfer (command 0x03, CS held low, auto-incrementing
 * address). This avoids issuing hundreds of tiny back-to-back DMA reads through
 * PY25Q16_ReadBuffer(), which is not reliable at that rate.
 */
#define MB_READ_CHUNK 2048u

static uint32_t mb_ext_image_crc32(uint32_t addr, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;

    mb_spi_polled_mode();

    /* Read in chunks. IRQs are masked during each chunk's continuous transfer
     * (an interrupt mid-transfer desyncs the polled SPI - the same reason the
     * RAM copier masks them), but re-enabled between chunks so the USB stack
     * keeps being serviced and the reply can go out afterwards. */
    while (len && !mb_spi_err)
    {
        uint32_t chunk = (len < MB_READ_CHUNK) ? len : MB_READ_CHUNK;
        uint32_t primask = __get_PRIMASK();
        __disable_irq();

        GPIOA->BRR = MB_CS_PIN;                 /* CS low */
        mb_spi_byte(0x03u);
        mb_spi_byte((addr >> 16) & 0xFFu);
        mb_spi_byte((addr >> 8) & 0xFFu);
        mb_spi_byte(addr & 0xFFu);
        for (uint32_t i = 0; i < chunk && !mb_spi_err; i++)
        {
            crc ^= mb_spi_byte(0xFFu);
            for (int k = 0; k < 8; k++)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        GPIOA->BSRR = MB_CS_PIN;                /* CS high */

        __set_PRIMASK(primask);                 /* let IRQs / USB breathe */
        addr += chunk;
        len  -= chunk;
    }

    return crc ^ 0xFFFFFFFFu;
}

/* Polled read of `len` bytes from external flash into `buf` (no DMA), matching
 * the technique the RAM copier uses. Sets mb_spi_err on a stuck SPI. */
static void mb_ext_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    mb_spi_polled_mode();

    /* IRQs off during the transfer (see mb_ext_image_crc32); break on timeout. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    GPIOA->BRR = MB_CS_PIN;
    mb_spi_byte(0x03u);
    mb_spi_byte((addr >> 16) & 0xFFu);
    mb_spi_byte((addr >> 8) & 0xFFu);
    mb_spi_byte(addr & 0xFFu);
    while (len-- && !mb_spi_err)
        *buf++ = mb_spi_byte(0xFFu);
    GPIOA->BSRR = MB_CS_PIN;

    __set_PRIMASK(primask);
}

/* -------------------------------------------------------------------------- */
/* External flash raw erase/program (polled, flash-resident).                 */
/*                                                                            */
/* Used only by the M4 slot-management commands. These bypass the stateful    */
/* PY25Q16 driver (its sector cache would desync when we erase and program a  */
/* slot behind its back, and its per-chunk read-modify-write would erase a    */
/* sector on every small chunk). Each CS-framed transaction masks IRQs for    */
/* its own burst only - an interrupt mid-transfer desyncs the polled SPI, the */
/* same reason mb_ext_image_crc32 masks them - and re-enables them between    */
/* transactions so USB keeps being serviced (notably across the long erase).  */
/* -------------------------------------------------------------------------- */

#define MB_EXT_CMD_WREN   0x06u   /* write enable                */
#define MB_EXT_CMD_PP     0x02u   /* page program (<=256 B)      */
#define MB_EXT_CMD_SE     0x20u   /* 4 KiB sector erase          */
#define MB_EXT_CMD_RDSR   0x05u   /* read status register 1      */
#define MB_EXT_SECTOR     0x1000u /* PY25Q16 erase granularity   */
#define MB_EXT_PAGE       0x100u  /* PY25Q16 program granularity */
#define MB_EXT_WIP_TIMEOUT 5000000u /* status polls before giving up (~seconds) */

static void mb_ext_wren(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    GPIOA->BRR  = MB_CS_PIN;
    mb_spi_byte(MB_EXT_CMD_WREN);
    GPIOA->BSRR = MB_CS_PIN;
    __set_PRIMASK(primask);
}

/* Poll WIP until the erase/program finishes (or mb_spi_err / timeout). IRQs are
 * masked only for each 2-byte status read, not the whole wait, so a ~300 ms
 * erase does not starve USB. */
static bool mb_ext_wait_wip(void)
{
    for (uint32_t i = 0; i < MB_EXT_WIP_TIMEOUT; i++)
    {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        GPIOA->BRR  = MB_CS_PIN;
        mb_spi_byte(MB_EXT_CMD_RDSR);
        uint8_t status = mb_spi_byte(0xFFu);
        GPIOA->BSRR = MB_CS_PIN;
        __set_PRIMASK(primask);

        if (mb_spi_err)
            return false;
        if (!(status & 1u))       /* WIP clear */
            return true;
    }
    return false;
}

static bool mb_ext_sector_erase(uint32_t addr)
{
    mb_ext_wren();
    if (mb_spi_err)
        return false;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    GPIOA->BRR  = MB_CS_PIN;
    mb_spi_byte(MB_EXT_CMD_SE);
    mb_spi_byte((addr >> 16) & 0xFFu);
    mb_spi_byte((addr >> 8) & 0xFFu);
    mb_spi_byte(addr & 0xFFu);
    GPIOA->BSRR = MB_CS_PIN;
    __set_PRIMASK(primask);

    if (mb_spi_err)
        return false;
    return mb_ext_wait_wip();
}

/* Program up to one 256-byte page; the caller must not cross a page boundary. */
static bool mb_ext_page_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    mb_ext_wren();
    if (mb_spi_err)
        return false;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    GPIOA->BRR  = MB_CS_PIN;
    mb_spi_byte(MB_EXT_CMD_PP);
    mb_spi_byte((addr >> 16) & 0xFFu);
    mb_spi_byte((addr >> 8) & 0xFFu);
    mb_spi_byte(addr & 0xFFu);
    for (uint32_t i = 0; i < len && !mb_spi_err; i++)
        mb_spi_byte(data[i]);
    GPIOA->BSRR = MB_CS_PIN;
    __set_PRIMASK(primask);

    if (mb_spi_err)
        return false;
    return mb_ext_wait_wip();
}

/* Program an arbitrary range, split on 256-byte page boundaries (a page program
 * that crosses a page boundary wraps within the page instead of advancing). */
static bool mb_ext_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    while (len)
    {
        uint32_t pageRem = MB_EXT_PAGE - (addr & (MB_EXT_PAGE - 1u));
        uint32_t n = (len < pageRem) ? len : pageRem;
        if (!mb_ext_page_program(addr, data, n))
            return false;
        addr += n;
        data += n;
        len  -= n;
    }
    return true;
}

/* Read + validate a slot header only (no CRC recompute), via polled reads. */
static uint8_t mb_read_header(uint8_t slot, mb_slot_header_t *hdr)
{
    if (slot >= MB_SLOT_COUNT)
        return MB_ERR_SLOT;

    const uint32_t slotBase = MB_SLOT0_EXT_BASE + (uint32_t)slot * MB_SLOT_STRIDE;

    mb_spi_err = 0;
    mb_ext_read(slotBase, (uint8_t *)hdr, sizeof(*hdr));
    if (mb_spi_err)                               return MB_ERR_SPI;
    if (hdr->magic != MB_SLOT_MAGIC)              return MB_ERR_MAGIC;
    if (hdr->hdr_version > MB_HDR_VERSION)         return MB_ERR_VERSION;
    if (!(hdr->flags & MB_FLAG_COMMITTED))         return MB_ERR_NOT_COMMITTED;
    if (hdr->image_size == 0 || hdr->image_size > MB_INT_APP_SIZE) return MB_ERR_SIZE;
    return MB_OK;
}

/* Validate one slot (header + image CRC-32), entirely via polled reads.
 * Never touches the internal flash. */
static uint8_t mb_validate(uint8_t slot, mb_slot_header_t *hdr, uint32_t *crcOut)
{
    uint8_t err = mb_read_header(slot, hdr);
    if (err != MB_OK)
        return err;

    const uint32_t slotBase = MB_SLOT0_EXT_BASE + (uint32_t)slot * MB_SLOT_STRIDE;

    mb_spi_err = 0;
    uint32_t crc = mb_ext_image_crc32(slotBase + MB_SLOT_IMG_OFFSET, hdr->image_size);
    if (crcOut)
        *crcOut = crc;
    if (mb_spi_err)                               return MB_ERR_SPI;
    if (crc != hdr->image_crc32)                  return MB_ERR_CRC;
    return MB_OK;
}

uint8_t MB_ValidateSlot(uint8_t slot, mb_slot_header_t *out_header, uint32_t *out_crc)
{
    mb_slot_header_t local;
    return mb_validate(slot, out_header ? out_header : &local, out_crc);
}

uint8_t MB_ValidateSlot0(uint32_t *out_crc)
{
    return MB_ValidateSlot(0, NULL, out_crc);
}

uint8_t MB_RestoreSlot(uint8_t slot, uint8_t *progress_line)
{
    mb_slot_header_t hdr;
    uint8_t err = mb_validate(slot, &hdr, NULL);
    if (err != MB_OK)
        return err;

    const uint32_t slotBase = MB_SLOT0_EXT_BASE + (uint32_t)slot * MB_SLOT_STRIDE;

    /* Valid: the RAM stub copies exactly image_size bytes, pads the partial
     * page with 0xFF and erases the remainder of the application region. */
    MB_PrepareInternalFlash();

    /* Call through a volatile pointer so the compiler emits an absolute 'blx'
     * (the RAM copy sits far beyond a Cortex-M0+ 'bl' reach from flash). */
    void (*volatile ramReflash)(uint32_t, uint32_t, uint32_t, uint8_t *) = MB_RamReflash;
    ramReflash(MB_INT_APP_BASE, slotBase + MB_SLOT_IMG_OFFSET,
               hdr.image_size, progress_line);

    return MB_OK; /* not reached */
}

uint8_t MB_RestoreSlot0(void)
{
    return MB_RestoreSlot(0, NULL);
}

/* -------------------------------------------------------------------------- */
/* M4 slot management (host tool). External flash only - never brick-critical.*/
/* -------------------------------------------------------------------------- */

uint8_t MB_SlotInfo(uint8_t slot, mb_slot_header_t *out_header)
{
    mb_slot_header_t local;
    return mb_read_header(slot, out_header ? out_header : &local);
}

uint8_t MB_SlotErase(uint8_t slot)
{
    if (slot >= MB_SLOT_COUNT)
        return MB_ERR_SLOT;

    const uint32_t base = MB_SLOT0_EXT_BASE + (uint32_t)slot * MB_SLOT_STRIDE;

    mb_spi_err = 0;
    mb_spi_polled_mode();
    for (uint32_t off = 0; off < MB_SLOT_STRIDE; off += MB_EXT_SECTOR)
        if (!mb_ext_sector_erase(base + off))
            return MB_ERR_SPI;

    return MB_OK;
}

uint8_t MB_SlotWrite(uint8_t slot, uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (slot >= MB_SLOT_COUNT)
        return MB_ERR_SLOT;
    if (len == 0)
        return MB_OK;
    if (offset > MB_SLOT_STRIDE || len > MB_SLOT_STRIDE - offset)
        return MB_ERR_SIZE;

    const uint32_t base = MB_SLOT0_EXT_BASE + (uint32_t)slot * MB_SLOT_STRIDE;

    mb_spi_err = 0;
    mb_spi_polled_mode();
    if (!mb_ext_program(base + offset, data, len))
        return MB_ERR_SPI;

    return MB_OK;
}

/* Middle of slot 0's image (32 KiB in, well within the 118 KiB image body). */
#define MB_CORRUPT_OFFSET   0x8000u
#define MB_CORRUPT_MAX      128u

void MB_CorruptSlot0(const uint8_t *data, uint32_t len)
{
    if (!data || len == 0)
        return;
    if (len > MB_CORRUPT_MAX)
        len = MB_CORRUPT_MAX;

    /* Constrained to slot 0's image body: this address range cannot reach the
     * slot header, nor calibration / EEPROM / RF log / voice regions. */
    PY25Q16_WriteBuffer(MB_SLOT0_EXT_BASE + MB_SLOT_IMG_OFFSET + MB_CORRUPT_OFFSET,
                        data, len, false);
}

volatile uint8_t mb_mark_on = 0;

void MB_Mark(const char *s)
{
    if (!mb_mark_on)
        return;
    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    UI_PrintStringSmallNormal(s, 10, 0, 3);
    ST7565_BlitFullScreen();
}

void MB_DumpExt(uint32_t addr, uint8_t *buf, uint32_t len)
{
    /* Trace the driver read step by step on the LCD so a freeze reveals where. */
    mb_mark_on = 1;
    MB_Mark("DUMP begin");
    PY25Q16_ReadBufferSafe(addr, buf, len);
    MB_Mark("DUMP done");
    mb_mark_on = 0;
}

void MB_SpiState(uint32_t out[4])
{
    out[0] = SPI2->CR1;            /* bit 6 (SPE) = SPI enabled */
    out[1] = SPI2->CR2;           /* bit0 RXDMAEN, bit1 TXDMAEN */
    out[2] = SPI2->SR;            /* bit0 RXNE, bit1 TXE, bit7 BSY */
    out[3] = DMA1_Channel4->CCR;  /* SPI2 RX DMA channel (bit0 EN) */
}
