/* Copyright 2026 F4HWN
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "driver/backlight.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/mb_flash.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "ui/helper.h"
#include "ui/multiboot.h"

static uint8_t gRunningSlot = MB_SLOT_BACKUP;

static uint8_t mb_remember_running_slot(uint8_t slot)
{
    gRunningSlot = slot;
    return slot;
}

uint8_t MB_GetRunningSlot(void)
{
    return gRunningSlot;
}

static const char *mb_error_text(uint8_t err)
{
    switch (err)
    {
        case MB_OK:                return "OK";
        case MB_ERR_MAGIC:         return "empty";
        case MB_ERR_VERSION:       return "new header";
        case MB_ERR_NOT_COMMITTED: return "incomplete";
        case MB_ERR_SIZE:          return "bad size";
        case MB_ERR_CRC:           return "CRC ERROR";
        case MB_ERR_SPI:           return "SPI ERROR";
        case MB_ERR_SLOT:          return "bad slot";
        case MB_ERR_AUTH:          return "auth";
        case MB_ERR_RAM_LOAD:      return "RAM LOAD ERROR";
        default:                   return "error";
    }
}

static void mb_copy_label(char *dst, uint8_t cap, const char *src, uint8_t src_cap)
{
    uint8_t n = 0;
    while (n + 1u < cap && n < src_cap && src[n])
    {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
}

static uint8_t mb_copy_slot_version(char *dst, uint8_t cap, const mb_slot_header_t *header)
{
    uint8_t n = 0;

    dst[0] = 0;
    for (uint8_t i = 0; i + 1u < MB_VERSION_LEN && header->fw_version[i]; i++)
    {
        if (header->fw_version[i] == 'v' &&
            header->fw_version[i + 1u] >= '0' &&
            header->fw_version[i + 1u] <= '9')
        {
            i++;
            while (n + 1u < cap && i < MB_VERSION_LEN)
            {
                const char c = header->fw_version[i];
                if ((c < '0' || c > '9') && c != '.')
                    break;
                dst[n++] = c;
                i++;
            }
            break;
        }
    }

    dst[n] = 0;
    return n;
}

/* "F4HWN MULTIBOOT" banner in the top status bar, shown on every screen - the
 * same way the firmware puts mode labels there (inverse 3x5 capsule). */
static void mb_status_bar(void)
{
    UI_StatusClear();
    GUI_DisplaySmallestInverse("F4HWN MULTIBOOT", 34, 0, true, true, 94);

    /* Thin line dressing up the otherwise blank row between the status bar and
     * the first content row. Drawn on gFrameBuffer[0] (line 0), which every
     * multiboot screen leaves blank, so it shows on all of them. Bit 3 (~mid of
     * the row) stays clear of the selected-slot capsule's top edge (bit 7). */
    for (uint8_t x = 2u; x < LCD_WIDTH - 2u; x++)
        gFrameBuffer[0][x] |= 0x08u;
}

/* Bottom key-hint line: each key name as an inverse 3x5 capsule label, its
 * action in plain 3x5 text beside it. Drawn on the bottom line
 * (gFrameBuffer[6] -> y = 6*8+1 = 49). MENU is pinned to the left and EXIT to
 * the right, leaving an airy gap in the middle. "MENU"/"EXIT" are 4 chars
 * (16 px); their capsule spans [x-2 .. x+16]. */
static void mb_key_hints(const char *act_menu, const char *act_exit)
{
    const uint8_t sp = 6u;                              /* label <-> action gap  */
    const uint8_t ae = (uint8_t)strlen(act_exit);
    const uint8_t xm = 4u;                              /* MENU text; capsule at x=2 */
    const uint8_t xe = (uint8_t)(124u - ae * 4u - sp - 16u); /* EXIT action ends at x=124 */

    GUI_DisplaySmallestInverse("MENU", xm, 6, false, true, (uint8_t)(xm + 16u));
    GUI_DisplaySmallest(act_menu, (uint8_t)(xm + 16u + sp), 49, false, true);

    GUI_DisplaySmallestInverse("EXIT", xe, 6, false, true, (uint8_t)(xe + 16u));
    GUI_DisplaySmallest(act_exit, (uint8_t)(xe + 16u + sp), 49, false, true);
}

static void mb_invert_rounded_row(uint8_t line)
{
    gFrameBuffer[line][0] ^= 0x7Fu;
    for (uint8_t x = 1u; x < LCD_WIDTH - 1u; x++)
    {
        gFrameBuffer[line][x] ^= 0xFFu;
        gFrameBuffer[line - 1u][x] ^= 0x80u;
    }
    gFrameBuffer[line][LCD_WIDTH - 1u] ^= 0x7Fu;
}

static void mb_show_message(const char *line1, const char *line2, const char *line3)
{
    UI_DisplayClear();
    mb_status_bar();
    if (line1) UI_PrintStringSmallNormal(line1, 2, 126, 2);
    if (line2) UI_PrintStringSmallNormal(line2, 2, 126, 4);
    if (line3) UI_PrintStringSmallNormal(line3, 2, 126, 6);
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

static void mb_wait_release(void)
{
    uint8_t stable = 0;
    while (stable < 10u)
    {
        if (!GPIO_IsPttPressed() && KEYBOARD_Poll() == KEY_INVALID)
            stable++;
        else
            stable = 0;
        SYSTEM_DelayMs(10);
    }
}

static KEY_Code_t mb_get_key(void)
{
    for (;;)
    {
        KEY_Code_t key = KEYBOARD_Poll();
        if (key != KEY_INVALID)
        {
            SYSTEM_DelayMs(30);
            if (KEYBOARD_Poll() == key)
            {
                while (KEYBOARD_Poll() != KEY_INVALID)
                    SYSTEM_DelayMs(10);
                return key;
            }
        }
        SYSTEM_DelayMs(10);
    }
}

static void mb_scan_slots(mb_slot_header_t headers[MB_SLOT_COUNT], uint8_t status[MB_SLOT_COUNT])
{
    mb_show_message("Scanning slots...", NULL, "Please wait");
    for (uint8_t slot = 0; slot < MB_SLOT_COUNT; slot++)
        status[slot] = MB_ValidateSlot(slot, &headers[slot], NULL);
}

static void mb_render_slots(uint8_t selected,
                            const mb_slot_header_t headers[MB_SLOT_COUNT],
                            const uint8_t status[MB_SLOT_COUNT])
{
    char line[19]; /* 18 glyphs max: 18 * 7 px fits from x=2 to x=126. */
    char version[MB_VERSION_LEN];

    UI_DisplayClear();
    mb_status_bar();

    for (uint8_t slot = 0; slot < MB_SLOT_COUNT; slot++)
    {
        const uint8_t fbLine = (uint8_t)(slot + 1u); /* page 1 stays blank */
        uint8_t version_len = 0;
        uint8_t version_x = 0;

        memset(line, 0, sizeof(line));
        memset(version, 0, sizeof(version));
        /* Slot 0 is the auto-backed-up main firmware: label it 'M' (Main) so it
         * reads apart from the numbered user slots 1..4. */
        line[0] = (slot == 0u) ? 'M' : (char)('0' + slot);
        line[1] = ' ';
        line[2] = ' ';

        if (status[slot] == MB_OK)
        {
            uint8_t name_cap = sizeof(line) - 3u;

            version_len = mb_copy_slot_version(version, sizeof(version), &headers[slot]);
            if (version_len)
            {
                const uint8_t name_x = 2u + 3u * 7u;
                uint8_t available;

                version_x = (uint8_t)(LCD_WIDTH - 2u - version_len * 7u);
                available = version_x > name_x
                    ? (uint8_t)((version_x - name_x) / 7u)
                    : 0u;
                if (available + 1u < name_cap)
                    name_cap = available + 1u;
            }

            if (headers[slot].name[0])
                mb_copy_label(&line[3], name_cap, headers[slot].name, MB_NAME_LEN);
            else if (!version_len)
                mb_copy_label(&line[3], name_cap, headers[slot].fw_version, MB_VERSION_LEN);
        }
        else
            mb_copy_label(&line[3], sizeof(line) - 3u, mb_error_text(status[slot]), 20u);

        UI_PrintStringSmallNormal(line, 2, 0, fbLine);
        if (version_len)
            UI_PrintStringSmallNormal(version, version_x, 0, fbLine);

        /* Selected row: full-width rounded inverse capsule. */
        if (slot == selected)
            mb_invert_rounded_row(fbLine);
    }

    mb_key_hints("SELECT", "QUIT");

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

static void mb_draw_progress_outline(void)
{
    /* Same rounded outline and hatch pattern as the scan progress gauge. */
    gFrameBuffer[6][3] = 0x0Cu;
    gFrameBuffer[6][4] = 0x12u;
    gFrameBuffer[6][123] = 0x12u;
    gFrameBuffer[6][124] = 0x0Cu;
    for (uint8_t x = 5; x < 123u; x++)
        gFrameBuffer[6][x] = 0x21u;
}

/* Full progress frame used while restoring a firmware slot. */
__attribute__((noinline)) static void mb_prepare_progress_screen(const char *title,
                                                                  const char *detail)
{
    UI_DisplayClear();
    mb_status_bar();
    UI_PrintStringSmallNormal(title, 2, 126, 1);
    UI_PrintStringSmallNormal("DO NOT POWER OFF", 2, 126, 3);
    UI_PrintStringSmallNormal(detail, 2, 126, 5);
    /* Empty gauge that the RAM copier fills as it reflashes. */
    mb_draw_progress_outline();
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

static void mb_prepare_progress(uint8_t slot)
{
    char slot_title[] = "Restore slot 0";
    slot_title[13] = (char)('0' + slot);
    const char *title = (slot == 0u) ? "Restore Main" : slot_title;

    mb_prepare_progress_screen(title, "Writing / Verify");
}

/* Discreet "Main backup" screen shown once, at the first boot after a normal
 * Flash-Firmware install, while the running firmware is copied into slot 0. */
static void mb_backup_prepare(void)
{
    UI_DisplayClear();
    UI_StatusClear();
    UI_PrintStringSmallNormal("Init Main", 2, 126, 1);
    UI_PrintStringSmallNormal("DO NOT POWER OFF", 2, 126, 3);
    mb_draw_progress_outline();
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

static void mb_backup_progress(uint32_t done, uint32_t total)
{
    uint32_t cols = total ? (done * 118u / total) : 118u;
    if (cols > 118u)
        cols = 118u;
    for (uint32_t i = 0; i < cols; i++)
        gFrameBuffer[6][5u + i] = 0x2Du;
    ST7565_BlitFullScreen();
}

/* With no trustworthy profile, continuing would let normal boot-time settings
 * writes modify an arbitrary bank. Keep the radio in a read-only error state;
 * a power cycle can recover from a transient SPI fault. */
__attribute__((noreturn)) static void mb_profile_error_halt(void)
{
    BACKLIGHT_TurnOn();
    mb_show_message("PROFILE ERROR", "Flash state unknown", "Restart radio");
    for (;;)
        SYSTEM_DelayMs(100);
}

static void mb_confirm_screen(uint8_t slot)
{
    char slot_title[] = "Restore slot 0?";
    slot_title[13] = (char)('0' + slot);
    const char *title = (slot == 0u) ? "Restore Main?" : slot_title;

    UI_DisplayClear();
    mb_status_bar();
    UI_PrintStringSmallNormal(title, 2, 126, 3);
    mb_key_hints("CONFIRM", "BACK");
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

void UI_MultibootSelector(void)
{
    mb_slot_header_t headers[MB_SLOT_COUNT];
    uint8_t status[MB_SLOT_COUNT];
    uint8_t selected = 0;

    /* Clear + blit the LCD BEFORE the backlight comes on, otherwise it reveals
     * the random power-on contents of the display RAM for a moment. */
    mb_show_message("Release keys", NULL, NULL);
    BACKLIGHT_TurnOn();
    mb_wait_release();
    mb_scan_slots(headers, status);

    /* Pre-select the firmware currently running (its slot, from the marker), so
     * the cursor lands on "where you are". If that slot isn't restorable (erased,
     * bad CRC...), fall back to the first valid slot. */
    selected = MB_GetActiveProfile();
    if (selected >= MB_SLOT_COUNT || status[selected] != MB_OK)
    {
        for (uint8_t slot = 0; slot < MB_SLOT_COUNT; slot++)
        {
            if (status[slot] == MB_OK)
            {
                selected = slot;
                break;
            }
        }
    }

    for (;;)
    {
        mb_render_slots(selected, headers, status);
        KEY_Code_t key = mb_get_key();

        if (key == KEY_EXIT)
        {
            /* MENU was latched by BOOT_GetMode(). Do not let that stale boot
             * key reach the normal application after leaving the selector. */
            gKeyReading0 = KEY_INVALID;
            gKeyReading1 = KEY_INVALID;
            gDebounceCounter = 0;
            return;
        }
        if (key == KEY_UP)
        {
            selected = (uint8_t)((selected + MB_SLOT_COUNT - 1u) % MB_SLOT_COUNT);
            continue;
        }
        if (key == KEY_DOWN)
        {
            selected = (uint8_t)((selected + 1u) % MB_SLOT_COUNT);
            continue;
        }
        if (key != KEY_MENU)
            continue;

        if (status[selected] != MB_OK)
        {
            mb_show_message("SLOT NOT VALID", mb_error_text(status[selected]), "Press any key");
            (void)mb_get_key();
            continue;
        }

        mb_confirm_screen(selected);
        key = mb_get_key();
        if (key != KEY_MENU)
            continue;

        /* Bind this slot to its own settings profile BEFORE reflashing. The
         * write is verified (read-back); if it can't be confirmed we must NOT
         * reflash - otherwise the next boot could resolve to the wrong profile
         * (e.g. when two slots hold the same firmware image). */
        if (MB_SetActiveProfile(selected) != MB_OK)
        {
            mb_show_message("PROFILE ERROR", "Marker not saved", "Press any key");
            (void)mb_get_key();
            continue;
        }

        mb_prepare_progress(selected);
        uint8_t err = MB_RestoreSlot(selected, gFrameBuffer[6]);

        /* Only reached when the final pre-erase validation refused the slot. */
        status[selected] = err;
        mb_show_message("RESTORE REFUSED", mb_error_text(err), "Press any key");
        (void)mb_get_key();
        mb_scan_slots(headers, status);
    }
}

/* Adopt the running internal firmware as Main: back it up into slot 0 and point
 * the marker at profile 0. Reached when the firmware was installed outside
 * multiboot (fresh radio, or a plain Flash-Firmware). */
static uint8_t mb_adopt_internal_as_main(void)
{
    mb_backup_prepare();
    BACKLIGHT_TurnOn();
    if (MB_BackupInternalToSlot0(mb_backup_progress) == MB_OK)
        (void)MB_SetActiveProfile(MB_SLOT_BACKUP);
    return MB_SLOT_BACKUP;
}

uint8_t MB_BootResolveProfile(void)
{
    mb_profile_state_t mark;
    mb_mark_status_t ms = MB_MARK_IO;

    for (uint8_t retry = 0; retry < 3u && ms == MB_MARK_IO; retry++)
    {
        ms = MB_ReadActiveProfile(&mark);
        if (ms == MB_MARK_IO)
            SYSTEM_DelayMs(10);
    }

    /* A reliably-read marker is authoritative: it carries the expected internal
     * identity, so we don't even need the slot header. */
    if (ms == MB_MARK_VALID)
    {
        if (MB_InternalMatchesProfile(&mark))
            return mb_remember_running_slot(mark.index); /* slot named by the marker */

        /* Marker read fine but internal no longer carries its identity -> the
         * firmware was replaced outside multiboot (a plain Flash-Firmware). Adopt
         * it as Main. Deliberately NOT a content scan here: a build that merely
         * duplicates a user slot (or a marker that already points at such a slot)
         * must still refresh Main. */
        return mb_remember_running_slot(mb_adopt_internal_as_main());
    }

    /* Marker unreliable (MISSING / LEGACY / CORRUPT / IO): identify the running
     * firmware by content, and never destroy Main on uncertainty - internal is
     * adopted only when it matches no slot AND every read was clean, so a
     * transient SPI error or a half-written marker can never destroy Main. */
    bool had_io = false;
    for (uint8_t slot = 0; slot < MB_SLOT_COUNT; slot++)
    {
        mb_fw_match_t m = MB_FW_IO;
        for (uint8_t retry = 0; retry < 3u && m == MB_FW_IO; retry++)
            m = MB_InternalMatchesSlot(slot);
        if (m == MB_FW_MATCH)
        {
            (void)MB_SetActiveProfile(slot);    /* record/repair the marker */
            return mb_remember_running_slot(slot);
        }
        if (m == MB_FW_IO)
            had_io = true;
    }

    if (had_io || ms == MB_MARK_IO || ms == MB_MARK_CORRUPT)
    {
        /* Halt to protect an existing Main while the flash state is uncertain;
         * but if slot 0 holds no valid backup there is nothing to protect, so
         * fall through and adopt instead of bricking a first boot. */
        uint8_t main_status = MB_SlotInfo(MB_SLOT_BACKUP, NULL);
        bool main_exists = (main_status != MB_ERR_MAGIC &&
                            main_status != MB_ERR_NOT_COMMITTED);
        if (main_exists)
            mb_profile_error_halt();
    }

    return mb_remember_running_slot(mb_adopt_internal_as_main());
}
