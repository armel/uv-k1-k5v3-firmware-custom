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

#include <stddef.h>
#include <string.h>

#include "driver/mb_flash.h"
#include "driver/py25q16.h"

#include "py32f0xx.h"

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

/* These primitives are called repeatedly while application flash is offline.
 * Keep one copy of each in the copied RAM section: noinline/noclone prevents
 * GCC from silently duplicating one back into MB_RamReflash. */
#define MB_RAM_HELPER __attribute__((section(".RamFunc"), noinline, noclone, used))

/* Polled single-byte SPI2 transfer. The result is returned through out so
 * timeout and received 0xFF remain distinguishable. */
MB_RAM_HELPER static bool mb_ram_spi(uint8_t v, uint8_t *out)
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

MB_RAM_HELPER static bool mb_ram_flash_idle(void)
{
    uint32_t timeout = MB_RAM_FLASH_TIMEOUT;
    while (FLASH->SR & FLASH_SR_BSY)
        if (!--timeout)
            return false;
    return true;
}

MB_RAM_HELPER __attribute__((noreturn)) static void mb_ram_reset(void)
{
    __DSB();

    SCB->AIRCR = (0x5FAu << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
    __DSB();
    for (;;) { }
}

/* Minimal SPI1 LCD writer. A display timeout merely disables progress updates:
 * it must never abort or delay the safety-critical flash copy. */
MB_RAM_HELPER static bool mb_ram_lcd_spi(uint8_t v)
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

/* Blank the whole LCD RAM (all 8 pages) right before the reset. The MCU reset
 * leaves the display controller powered and still showing the "Restore slot N"
 * screen; it stays visible through the next boot until ST7565_Init re-inits the
 * panel. Wiping it here means the reboot window shows nothing instead of a
 * stale restore screen. Best-effort: a display timeout just leaves it as-is. */
__attribute__((always_inline)) static inline void mb_ram_lcd_clear(void)
{
    for (uint8_t page = 0; page < 8u; page++)
    {
        GPIOB->BRR = MB_LCD_CS_PIN;
        GPIOA->BRR = MB_LCD_A0_PIN;             /* command */
        if (!mb_ram_lcd_spi((uint8_t)(0xB0u | page)) ||  /* set page 0..7 */
            !mb_ram_lcd_spi(0x10u) ||           /* column high nibble */
            !mb_ram_lcd_spi(0x04u))             /* visible RAM starts at column 4 */
        {
            GPIOB->BSRR = MB_LCD_CS_PIN;
            return;
        }
        GPIOA->BSRR = MB_LCD_A0_PIN;            /* data */
        for (uint32_t i = 0; i < 128u; i++)
            if (!mb_ram_lcd_spi(0x00u))
            {
                GPIOB->BSRR = MB_LCD_CS_PIN;
                return;
            }
        GPIOB->BSRR = MB_LCD_CS_PIN;
    }
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
    if (lcdEnabled)
        mb_ram_lcd_clear();
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
    /* Slot 0 is the firmware-managed base backup: never erasable from the host. */
    if (slot >= MB_SLOT_COUNT || slot == MB_SLOT_BACKUP)
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
    /* Slot 0 is the firmware-managed base backup: never writable from the host. */
    if (slot >= MB_SLOT_COUNT || slot == MB_SLOT_BACKUP)
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

/* -------------------------------------------------------------------------- */
/* Per-profile settings banks.                                                */
/*                                                                            */
/* The banking offset itself lives in the flash driver (PY25Q16_SetProfileBase);*/
/* here we only own the active-profile marker and the profile->base mapping.  */
/* All external-flash only, never brick-critical.                             */
/* -------------------------------------------------------------------------- */

static uint32_t mb_crc32_bytes(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t MB_ProfileBase(uint8_t profile)
{
    if (profile == 0)
        return 0;                    /* profile 0 = historical config region */
    if (profile < MB_PROFILE_COUNT)
        return MB_PROFILE1_EXT_BASE + (uint32_t)(profile - 1u) * MB_PROFILE_BANK_SIZE;
    return 0;                        /* out of range -> safe default */
}

static mb_mark_status_t mb_read_profile_copy(uint32_t base, mb_profile_state_t *st)
{
    mb_spi_err = 0;
    mb_ext_read(base, (uint8_t *)st, sizeof(*st));
    if (mb_spi_err)                              return MB_MARK_IO;
    if (st->magic == 0xFFFFFFFFu)                return MB_MARK_MISSING;
    if (st->magic == MB_PROFILE_LEGACY_MAGIC)
    {
        /* FMP1 was { magic, index, ~index, reserved[2] }. Preserve its slot
         * choice long enough for boot resolution to migrate it to FMP2. */
        const uint8_t legacy_index = ((const uint8_t *)st)[4];
        const uint8_t legacy_inv   = ((const uint8_t *)st)[5];
        if ((uint8_t)~legacy_inv != legacy_index || legacy_index >= MB_PROFILE_COUNT)
            return MB_MARK_CORRUPT;
        memset(st, 0, sizeof(*st));
        st->magic = MB_PROFILE_LEGACY_MAGIC;
        st->index = legacy_index;
        st->index_inv = legacy_inv;
        return MB_MARK_LEGACY;
    }
    if (st->magic != MB_PROFILE_MAGIC)           return MB_MARK_CORRUPT;
    if ((uint8_t)~st->index_inv != st->index)    return MB_MARK_CORRUPT;
    if (st->index >= MB_PROFILE_COUNT)           return MB_MARK_CORRUPT;
    if (st->image_size == 0u ||
        st->image_size > MB_INT_APP_SIZE)         return MB_MARK_CORRUPT;
    if (mb_crc32_bytes((const uint8_t *)st,
                       sizeof(*st) - sizeof(st->state_crc32)) != st->state_crc32)
                                                    return MB_MARK_CORRUPT;
    return MB_MARK_VALID;
}

static bool mb_generation_newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

static mb_mark_status_t mb_read_active_profile(mb_profile_state_t *state,
                                                uint32_t *state_base)
{
    mb_profile_state_t a;
    mb_profile_state_t b;
    mb_mark_status_t sa = mb_read_profile_copy(MB_PROFILE_STATE_A_BASE, &a);
    mb_mark_status_t sb = mb_read_profile_copy(MB_PROFILE_STATE_B_BASE, &b);

    /* If either sector could not be read, it might contain the newest record.
     * Do not silently select an older state and risk loading the wrong bank. */
    if (sa == MB_MARK_IO || sb == MB_MARK_IO)
        return MB_MARK_IO;

    const mb_profile_state_t *chosen = NULL;
    uint32_t chosen_base = 0;
    if (sa == MB_MARK_VALID && sb == MB_MARK_VALID)
    {
        if (mb_generation_newer(b.generation, a.generation))
        {
            chosen = &b;
            chosen_base = MB_PROFILE_STATE_B_BASE;
        }
        else
        {
            chosen = &a;
            chosen_base = MB_PROFILE_STATE_A_BASE;
        }
    }
    else if (sa == MB_MARK_VALID)
    {
        chosen = &a;
        chosen_base = MB_PROFILE_STATE_A_BASE;
    }
    else if (sb == MB_MARK_VALID)
    {
        chosen = &b;
        chosen_base = MB_PROFILE_STATE_B_BASE;
    }

    if (chosen)
    {
        if (state)
            *state = *chosen;
        if (state_base)
            *state_base = chosen_base;
        return MB_MARK_VALID;
    }

    /* A legacy record is usable for migration but has no firmware identity.
     * Prefer A if both somehow exist; the next successful repair writes FMP2. */
    if (sa == MB_MARK_LEGACY || sb == MB_MARK_LEGACY)
    {
        const bool use_b = sa != MB_MARK_LEGACY;
        if (state)
            *state = use_b ? b : a;
        if (state_base)
            *state_base = use_b ? MB_PROFILE_STATE_B_BASE : MB_PROFILE_STATE_A_BASE;
        return MB_MARK_LEGACY;
    }

    return (sa == MB_MARK_MISSING && sb == MB_MARK_MISSING)
             ? MB_MARK_MISSING : MB_MARK_CORRUPT;
}

mb_mark_status_t MB_ReadActiveProfile(mb_profile_state_t *state)
{
    return mb_read_active_profile(state, NULL);
}

uint8_t MB_GetActiveProfile(void)
{
    mb_profile_state_t st;
    mb_mark_status_t status = MB_ReadActiveProfile(&st);
    return (status == MB_MARK_VALID || status == MB_MARK_LEGACY) ? st.index : 0;
}

uint8_t MB_SetActiveProfile(uint8_t profile)
{
    mb_slot_header_t hdr;
    mb_profile_state_t st;
    mb_profile_state_t current;
    uint32_t current_base = 0;

    if (profile >= MB_PROFILE_COUNT)
        return MB_ERR_SLOT;

    uint8_t hdr_err = mb_read_header(profile, &hdr);
    if (hdr_err != MB_OK)
        return hdr_err;

    mb_mark_status_t current_status = mb_read_active_profile(&current, &current_base);
    if (current_status == MB_MARK_IO)
        return MB_ERR_SPI;

    memset(&st, 0, sizeof(st));
    st.magic       = MB_PROFILE_MAGIC;
    st.generation  = (current_status == MB_MARK_VALID) ? current.generation + 1u : 0u;
    st.image_size  = hdr.image_size;
    st.image_crc32 = hdr.image_crc32;
    st.index       = profile;
    st.index_inv   = (uint8_t)~profile;
    st.state_crc32 = mb_crc32_bytes((const uint8_t *)&st,
                                    sizeof(st) - sizeof(st.state_crc32));

    const uint32_t target_base = ((current_status == MB_MARK_VALID ||
                                   current_status == MB_MARK_LEGACY) &&
                                  current_base == MB_PROFILE_STATE_A_BASE)
                                   ? MB_PROFILE_STATE_B_BASE
                                   : MB_PROFILE_STATE_A_BASE;

    mb_spi_err = 0;
    mb_spi_polled_mode();
    if (!mb_ext_sector_erase(target_base))
        return MB_ERR_SPI;
    if (!mb_ext_program(target_base, (const uint8_t *)&st, sizeof(st)))
        return MB_ERR_SPI;

    /* Verify the new copy directly. The previous valid sector has not been
     * touched, so any failure here remains power-loss safe. */
    mb_profile_state_t chk;
    mb_mark_status_t chk_status = mb_read_profile_copy(target_base, &chk);
    if (chk_status == MB_MARK_IO)                    return MB_ERR_SPI;
    if (chk_status != MB_MARK_VALID ||
        memcmp(&chk, &st, sizeof(st)) != 0)           return MB_ERR_CRC;

    return MB_OK;
}

uint8_t MB_ProfileErase(uint8_t profile)
{
    /* Profile 0 (the base config) is not resettable from the host: reset it by
     * factory-resetting the running base firmware instead. Profiles 1..N live
     * in their own 64 KiB banks, clear of the shared calibration at 0x010000. */
    if (profile == 0 || profile >= MB_PROFILE_COUNT)
        return MB_ERR_SLOT;

    const uint32_t base = MB_ProfileBase(profile);

    /* Invalidate before the first raw erase so an early failure cannot leave a
     * cache entry referring to a sector that was already erased. */
    PY25Q16_InvalidateCache();

    mb_spi_err = 0;
    mb_spi_polled_mode();
    for (uint32_t off = 0; off < MB_PROFILE_BANK_SIZE; off += MB_EXT_SECTOR)
        if (!mb_ext_sector_erase(base + off))
            return MB_ERR_SPI;

    return MB_OK;
}

/* -------------------------------------------------------------------------- */
/* Slot 0 self-backup (base firmware).                                        */
/* -------------------------------------------------------------------------- */

/* Bounded copy of a NUL-terminated string into a fixed field, zero-padded. */
static void mb_copy_str(char *dst, uint8_t cap, const char *src)
{
    uint8_t n = 0;
    while (n + 1u < cap && src[n])
    {
        dst[n] = src[n];
        n++;
    }
    while (n < cap)
        dst[n++] = 0;
}

/* CRC-32 (zlib) of the internal application flash, which is memory-mapped so
 * no SPI is involved. Same polynomial as the external slot CRC. */
static uint32_t mb_int_image_crc32(uint32_t len)
{
    const uint8_t *p = (const uint8_t *)MB_INT_APP_BASE;
    return mb_crc32_bytes(p, len);
}

static bool mb_internal_matches(uint32_t image_size, uint32_t image_crc32)
{
    return mb_int_image_crc32(image_size) == image_crc32;
}

mb_fw_match_t MB_InternalMatchesSlot(uint8_t slot)
{
    mb_slot_header_t hdr;
    uint8_t err = mb_read_header(slot, &hdr);

    if (err == MB_ERR_SPI)
        return MB_FW_IO;         /* couldn't read the header: never conclude mismatch */
    if (err != MB_OK)
        return MB_FW_MISMATCH;   /* no valid header: definitely not this firmware */

    return mb_internal_matches(hdr.image_size, hdr.image_crc32)
             ? MB_FW_MATCH : MB_FW_MISMATCH;
}

bool MB_InternalMatchesProfile(const mb_profile_state_t *state)
{
    if (!state || state->image_size == 0u || state->image_size > MB_INT_APP_SIZE)
        return false;
    return mb_internal_matches(state->image_size, state->image_crc32);
}

uint8_t MB_BackupInternalToSlot0(mb_progress_fn progress)
{
    const uint8_t *img  = (const uint8_t *)MB_INT_APP_BASE;
    const uint32_t size = MB_INT_APP_SIZE;
    const uint32_t base = MB_SLOT0_EXT_BASE + (uint32_t)MB_SLOT_BACKUP * MB_SLOT_STRIDE;

    mb_spi_err = 0;
    mb_spi_polled_mode();

    /* Erase the header sector + image area (rounded up to 4 KiB sectors). */
    for (uint32_t off = 0; off < MB_SLOT_IMG_OFFSET + size; off += MB_EXT_SECTOR)
        if (!mb_ext_sector_erase(base + off))
            return MB_ERR_SPI;

    /* Program the whole internal application region verbatim, in chunks,
     * reporting progress. It is an exact copy of what executes, so restoring
     * it later reproduces the running firmware bit-for-bit. */
    for (uint32_t done = 0; done < size; )
    {
        uint32_t n = (size - done < MB_EXT_SECTOR) ? (size - done) : MB_EXT_SECTOR;
        if (!mb_ext_program(base + MB_SLOT_IMG_OFFSET + done, img + done, n))
            return MB_ERR_SPI;
        done += n;
        if (progress)
            progress(done, size);
    }

    /* Validate the stored image before committing its header. Until that final
     * header write the slot remains invalid, so a failed CRC or interrupted
     * backup is guaranteed to retry at the next boot. */
    const uint32_t image_crc = mb_int_image_crc32(size);
    uint32_t ext_crc = mb_ext_image_crc32(base + MB_SLOT_IMG_OFFSET, size);
    if (mb_spi_err)                return MB_ERR_SPI;
    if (ext_crc != image_crc)      return MB_ERR_CRC;

    /* Write the COMMITTED header only after the external image verified. */
    mb_slot_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = MB_SLOT_MAGIC;
    hdr.hdr_version = MB_HDR_VERSION;
    hdr.flags       = MB_FLAG_COMMITTED;
    hdr.image_size  = size;
    hdr.image_crc32 = image_crc;
    mb_copy_str(hdr.name, MB_NAME_LEN, EDITION_STRING);
    mb_copy_str(hdr.fw_version, MB_VERSION_LEN, VERSION_STRING_2);

    if (!mb_ext_program(base, (const uint8_t *)&hdr, sizeof(hdr)))
        return MB_ERR_SPI;

    return MB_OK;
}
