#include <string.h>
#include <stdio.h>
#include "app/messenger_store.h"
#include "app/messenger_t9.h"
#include "app/messenger_rf.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "ui/helper.h"
#include "ui/ui.h"
#include "misc.h"
#include "font.h"

extern uint8_t gMsgHomeCursor;
extern uint8_t gMsgCursor;
extern uint8_t gMsgScroll;
extern uint8_t gMsgReadIndex;
extern uint8_t gMsgReadSource;
extern uint8_t gMsgSettingsCursor;
extern char gMsgComposeBuf[];
extern char gMsgCallsignBuf[];
extern MSG_T9Editor_t gMsgEditor;
extern MSG_T9Editor_t gMsgCallsignEditor;
extern uint8_t gMsgScreen;

enum { MSG_SCREEN_HOME = 0, MSG_SCREEN_INBOX, MSG_SCREEN_OUTBOX, MSG_SCREEN_DRAFTS, MSG_SCREEN_COMPOSE, MSG_SCREEN_READ, MSG_SCREEN_SETTINGS, MSG_SCREEN_CALLSIGN };

static void print_line(const char *s, uint8_t line, bool sel)
{
    // Keep all Messenger menu/list rows left-aligned and use the same
    // inverted selector style everywhere.  Important: do NOT pass a non-zero
    // End value to UI_PrintStringSmallNormalInverse here, because that helper
    // centers text when End > Start.  That was causing the text to drift
    // outside the selected capsule.
    char safe[18];
    strncpy(safe, s, sizeof(safe) - 1U);
    safe[sizeof(safe) - 1U] = 0;

    if (sel) UI_PrintStringSmallNormalInverse(safe, 1, 0, line);
    else UI_PrintStringSmallNormal(safe, 1, 0, line);
}

static void print_right_small(const char *s, uint8_t line)
{
    // SmallNormal pitch is 7 px.  Keep a wider right margin because the real
    // LCD showed the final digit wrapping when drawn too close to x=127.
    uint8_t len = (uint8_t)strlen(s);
    uint8_t width = (uint8_t)(len * 7U);
    uint8_t x = (width >= 120U) ? 0 : (uint8_t)(122U - width);
    UI_PrintStringSmallNormal(s, x, 0, line);
}

static void draw_title(const char *s)
{
    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    uint8_t len = (uint8_t)strlen(s);
    /* SmallBold is visually closer to a 7 px pitch on the UV-K1 LCD.
     * The previous 6 px estimate placed MESSENGER/INBOX/SENT titles
     * slightly right of center. */
    uint8_t x = (len >= 18) ? 0 : (uint8_t)((128U - (len * 7U)) / 2U);
    UI_PrintStringSmallBold(s, x, 0, 0);
}

static void draw_dotted_separator(uint8_t y)
{
    /* Light message separator: one filled segment, one gap.  It is less
     * visually heavy than a solid line and keeps the message text dominant. */
    for (uint8_t x = 0; x < 128U; x = (uint8_t)(x + 4U)) {
        UI_DrawLineBuffer(gFrameBuffer, x, y, (uint8_t)(x + 1U), y, 1);
    }
}

static void msg_set_pixel(uint8_t x, uint8_t y, bool on)
{
    if (x >= 128U || y >= 64U) return;
    uint8_t mask = (uint8_t)(1U << (y & 7U));
    if (on) gFrameBuffer[y >> 3][x] |= mask;
    else    gFrameBuffer[y >> 3][x] &= (uint8_t)~mask;
}

static void msg_fill_rect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool on)
{
    if (x1 > 127U) x1 = 127U;
    if (y1 > 63U) y1 = 63U;
    for (uint8_t y = y0; y <= y1; y++) {
        for (uint8_t x = x0; x <= x1; x++) msg_set_pixel(x, y, on);
    }
}

static void msg_draw_small_at_y(const char *s, uint8_t x, uint8_t y, bool inverted)
{
    const uint8_t pitch = 7U;
    uint8_t len = (uint8_t)strlen(s);
    if (inverted) {
        uint8_t w = (uint8_t)(len * pitch + 2U);
        msg_fill_rect(x ? (uint8_t)(x - 1U) : 0U, y ? (uint8_t)(y - 1U) : 0U,
                      (uint8_t)(x + w), (uint8_t)(y + 7U), true);
    }

    for (uint8_t i = 0; s[i] && x < 128U; i++, x = (uint8_t)(x + pitch)) {
        char c = s[i];
        if (c <= ' ' || c >= 127) continue;
        const uint8_t *glyph = gFontSmall[(uint8_t)c - ' ' - 1U];
        for (uint8_t col = 0; col < 6U; col++) {
            uint8_t bits = glyph[col];
            for (uint8_t row = 0; row < 7U; row++) {
                if (bits & (1U << row)) msg_set_pixel((uint8_t)(x + col), (uint8_t)(y + row), !inverted);
            }
        }
    }
}

static void print_line_y(const char *s, uint8_t y, bool sel)
{
    char safe[18];
    strncpy(safe, s, sizeof(safe) - 1U);
    safe[sizeof(safe) - 1U] = 0;
    msg_draw_small_at_y(safe, 1, y, sel);
}

static void print_wrapped_small_y(const char *s, uint8_t y, uint8_t max_lines)
{
    char linebuf[18];
    uint8_t line = 0;
    while (*s && line < max_lines) {
        uint8_t n = 0;
        while (s[n] && n < 17U) {
            linebuf[n] = s[n];
            n++;
        }
        linebuf[n] = 0;
        msg_draw_small_at_y(linebuf, 0, (uint8_t)(y + (line * 8U)), false);
        s += n;
        line++;
    }
}


static void msg_draw_hline(uint8_t x0, uint8_t x1, uint8_t y, bool on)
{
    if (x1 > 127U) x1 = 127U;
    for (uint8_t x = x0; x <= x1; x++) msg_set_pixel(x, y, on);
}

static void msg_draw_vline(uint8_t x, uint8_t y0, uint8_t y1, bool on)
{
    if (y1 > 63U) y1 = 63U;
    for (uint8_t y = y0; y <= y1; y++) msg_set_pixel(x, y, on);
}

static void msg_draw_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool on)
{
    int dx = (x1 > x0) ? (int)(x1 - x0) : (int)(x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -((y1 > y0) ? (int)(y1 - y0) : (int)(y0 - y1));
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    int x = x0, y = y0;
    while (1) {
        msg_set_pixel((uint8_t)x, (uint8_t)y, on);
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

static void draw_icon_envelope(uint8_t x, uint8_t y)
{
    /* HOME icon: clean 30x18 envelope, centered in the right-side icon area. */
    msg_draw_hline(x, (uint8_t)(x + 29U), y, true);
    msg_draw_hline(x, (uint8_t)(x + 29U), (uint8_t)(y + 17U), true);
    msg_draw_vline(x, y, (uint8_t)(y + 17U), true);
    msg_draw_vline((uint8_t)(x + 29U), y, (uint8_t)(y + 17U), true);
    msg_draw_line((uint8_t)(x + 1U), (uint8_t)(y + 2U), (uint8_t)(x + 14U), (uint8_t)(y + 10U), true);
    msg_draw_line((uint8_t)(x + 28U), (uint8_t)(y + 2U), (uint8_t)(x + 15U), (uint8_t)(y + 10U), true);
    msg_draw_line((uint8_t)(x + 1U), (uint8_t)(y + 16U), (uint8_t)(x + 11U), (uint8_t)(y + 9U), true);
    msg_draw_line((uint8_t)(x + 28U), (uint8_t)(y + 16U), (uint8_t)(x + 18U), (uint8_t)(y + 9U), true);
}

static void draw_icon_pencil(uint8_t x, uint8_t y)
{
    /* HOME icon: clear diagonal pencil, 30x28.  Drawn as a thick slanted
     * body with a visible point and a short baseline, matching the UI mockup. */
    for (uint8_t i = 0; i < 20U; i++) {
        uint8_t px = (uint8_t)(x + 4U + i);
        uint8_t py = (uint8_t)(y + 23U - i);
        msg_set_pixel(px, py, true);
        msg_set_pixel((uint8_t)(px + 1U), py, true);
        msg_set_pixel(px, (uint8_t)(py - 1U), true);
        msg_set_pixel((uint8_t)(px + 1U), (uint8_t)(py - 1U), true);
    }
    /* pencil tip */
    msg_draw_line((uint8_t)(x + 2U), (uint8_t)(y + 25U), (uint8_t)(x + 6U), (uint8_t)(y + 23U), true);
    msg_draw_line((uint8_t)(x + 2U), (uint8_t)(y + 25U), (uint8_t)(x + 4U), (uint8_t)(y + 21U), true);
    msg_set_pixel((uint8_t)(x + 1U), (uint8_t)(y + 26U), true);
    /* eraser/cap */
    msg_draw_line((uint8_t)(x + 22U), (uint8_t)(y + 3U), (uint8_t)(x + 26U), y, true);
    msg_draw_line((uint8_t)(x + 24U), (uint8_t)(y + 6U), (uint8_t)(x + 29U), (uint8_t)(y + 2U), true);
    msg_draw_line((uint8_t)(x + 26U), y, (uint8_t)(x + 29U), (uint8_t)(y + 2U), true);
    msg_draw_line((uint8_t)(x + 22U), (uint8_t)(y + 3U), (uint8_t)(x + 24U), (uint8_t)(y + 6U), true);
    /* writing line */
    msg_draw_hline((uint8_t)(x + 10U), (uint8_t)(x + 29U), (uint8_t)(y + 27U), true);
}

static void draw_icon_up_arrow(uint8_t x, uint8_t y)
{
    /* HOME icon: bold upload/up arrow, 31x27. */
    const uint8_t cx = (uint8_t)(x + 15U);
    for (uint8_t r = 0; r < 9U; r++) {
        msg_draw_hline((uint8_t)(cx - r), (uint8_t)(cx + r), (uint8_t)(y + r), true);
    }
    msg_fill_rect((uint8_t)(x + 12U), (uint8_t)(y + 9U), (uint8_t)(x + 18U), (uint8_t)(y + 22U), true);
    msg_draw_hline((uint8_t)(x + 4U), (uint8_t)(x + 26U), (uint8_t)(y + 26U), true);
}

static void draw_icon_floppy(uint8_t x, uint8_t y)
{
    /* HOME icon: visually square floppy disk.
     * The LCD pixels look slightly taller than wide, so the bitmap is
     * intentionally wider than high (34x24) to read as a square on-device.
     * Outer shape: square body with only the top-right corner cut off. */
    msg_draw_hline(x, (uint8_t)(x + 28U), y, true);                 /* top edge stops at the cut */
    msg_draw_line((uint8_t)(x + 29U), y, (uint8_t)(x + 33U), (uint8_t)(y + 4U), true);
    msg_draw_vline((uint8_t)(x + 33U), (uint8_t)(y + 4U), (uint8_t)(y + 23U), true);
    msg_draw_hline(x, (uint8_t)(x + 33U), (uint8_t)(y + 23U), true);
    msg_draw_vline(x, y, (uint8_t)(y + 23U), true);

    /* Top shutter/label area. */
    msg_draw_hline((uint8_t)(x + 4U), (uint8_t)(x + 23U), (uint8_t)(y + 3U), true);
    msg_draw_hline((uint8_t)(x + 4U), (uint8_t)(x + 23U), (uint8_t)(y + 9U), true);
    msg_draw_vline((uint8_t)(x + 4U), (uint8_t)(y + 3U), (uint8_t)(y + 9U), true);
    msg_draw_vline((uint8_t)(x + 23U), (uint8_t)(y + 3U), (uint8_t)(y + 9U), true);
    msg_fill_rect((uint8_t)(x + 17U), (uint8_t)(y + 4U), (uint8_t)(x + 20U), (uint8_t)(y + 7U), true);

    /* Bottom label window, kept wide to avoid a tall/narrow look. */
    msg_draw_hline((uint8_t)(x + 6U), (uint8_t)(x + 27U), (uint8_t)(y + 14U), true);
    msg_draw_hline((uint8_t)(x + 6U), (uint8_t)(x + 27U), (uint8_t)(y + 22U), true);
    msg_draw_vline((uint8_t)(x + 6U), (uint8_t)(y + 14U), (uint8_t)(y + 22U), true);
    msg_draw_vline((uint8_t)(x + 27U), (uint8_t)(y + 14U), (uint8_t)(y + 22U), true);
    msg_draw_hline((uint8_t)(x + 9U), (uint8_t)(x + 24U), (uint8_t)(y + 17U), true);
    msg_draw_hline((uint8_t)(x + 9U), (uint8_t)(x + 24U), (uint8_t)(y + 19U), true);
}

static void draw_home_icon(uint8_t idx)
{
    /* Icons share one center point in the right side, midway between the
     * INBOX and DRAFTS rows.  They are not tied to the bottom separator. */
    switch (idx) {
        case 0: draw_icon_envelope(88U, 17U); break;  /* 30x18, center y~26 */
        case 1: draw_icon_pencil(89U, 13U); break;    /* 30x28, center y~27 */
        case 2: draw_icon_up_arrow(88U, 14U); break;  /* 31x27, center y~27 */
        default: draw_icon_floppy(86U, 15U); break;   /* 34x24, visual center y~27 */
    }
}

static void draw_home(void)
{
    static const char *items[] = { "INBOX", "COMPOSE", "SENT", "DRAFTS" };
    char buf[24];
    draw_title("MESSENGER");
    /* 0.3.0: HOME list shifted 1 px up so SELECT keeps 2 px bottom
     * clearance; right-side icon uses a fixed center shared by all states. */
    for (uint8_t i = 0; i < 4; i++) {
        print_line_y(items[i], (uint8_t)(10U + (i * 9U)), gMsgHomeCursor == i);
    }
    draw_home_icon(gMsgHomeCursor);

    draw_dotted_separator(46);
    GUI_DisplaySmallest("SELECT", 0, 49, false, true);

    if (gMessengerConfig.msg_debug) {
        /* RF22 ACK debug replaces old RF counter debug to save screen space.
         * P=pending MsgID, A=last ACK id heard, R=ACK rx count, M=match count. */
        snprintf(buf, sizeof(buf), "P%04X A%04X R%u M%u",
                 MSG_RF_GetAckDbgPendingId(), MSG_RF_GetAckDbgRxId(),
                 MSG_RF_GetAckDbgRxCount(), MSG_RF_GetAckDbgMatchCount());
        UI_PrintStringSmallNormal(buf, 0, 0, 6);
    }
}

static MSG_Message_t *current_list(uint8_t *count, const char **title)
{
    if (gMsgScreen == MSG_SCREEN_INBOX) { *count = MSG_STORE_CountInbox(); *title = "INBOX"; return gMessengerInbox; }
    if (gMsgScreen == MSG_SCREEN_OUTBOX) { *count = MSG_STORE_CountOutbox(); *title = "SENT"; return gMessengerOutbox; }
    *count = MSG_DRAFT_CAPACITY; *title = "DRAFTS"; return 0;
}

static void draw_list(void)
{
    uint8_t count; const char *title;
    MSG_Message_t *list = current_list(&count, &title);
    char buf[24];
    draw_title(title);
    snprintf(buf, sizeof(buf), "%u/%u", count ? (gMsgCursor + 1) : 0, count);
    print_right_small(buf, 0);
    if (!count) { UI_PrintStringSmallNormal("EMPTY", 0, 0, 3); return; }
    for (uint8_t row = 0; row < 6; row++) {
        uint8_t idx = gMsgScroll + row;
        if (idx >= count) break;
        if (gMsgScreen == MSG_SCREEN_DRAFTS) snprintf(buf, sizeof(buf), "%u %.18s", idx + 1, gMessengerConfig.drafts[idx]);
        else if (gMsgScreen == MSG_SCREEN_OUTBOX) {
            char st = '?';
            if (list[idx].status == MSG_STATUS_ACKED) st = '+';
            else if (list[idx].status == MSG_STATUS_FAILED) st = 'x';
            snprintf(buf, sizeof(buf), "%c%.17s", st, list[idx].text);
        } else snprintf(buf, sizeof(buf), "%c%.17s", (list[idx].unread ? '*' : ' '), list[idx].text);
        print_line(buf, row + 1, idx == gMsgCursor);
    }
}

static void draw_read(void)
{
    MSG_Message_t *m = (gMsgReadSource == MSG_SCREEN_OUTBOX) ? &gMessengerOutbox[gMsgReadIndex] : &gMessengerInbox[gMsgReadIndex];
    char buf[32];
    draw_title((gMsgReadSource == MSG_SCREEN_OUTBOX) ? "SENT" : "READ");
    {
        uint8_t total = (gMsgReadSource == MSG_SCREEN_OUTBOX) ? MSG_STORE_CountOutbox() : MSG_STORE_CountInbox();
        snprintf(buf, sizeof(buf), "%u/%u", total ? (uint8_t)(gMsgReadIndex + 1U) : 0U, total);
        print_right_small(buf, 0);
    }

    uint8_t used_hops = (m->ttl_init >= m->ttl_remain) ? (uint8_t)(m->ttl_init - m->ttl_remain) : 0;
    if (gMsgReadSource == MSG_SCREEN_OUTBOX) {
        char st = '?';
        if (m->status == MSG_STATUS_ACKED) st = '+';
        else if (m->status == MSG_STATUS_FAILED) st = 'x';

        if (gMessengerConfig.msg_hop == 0U) snprintf(buf, sizeof(buf), "TO:%s HOP:OFF", m->to);
        else snprintf(buf, sizeof(buf), "TO:%s HOP:%u", m->to, gMessengerConfig.msg_hop);

        /* Metadata is pixel-positioned: one pixel lower than 0.2.4 so it
         * visually aligns with the large ACK marker and sits closer to the
         * upper separator. */
        GUI_DisplaySmallest(buf, 0, 9, false, true);
        char stbuf[2] = { st, 0 };
        UI_PrintStringSmallBold(stbuf, 120, 0, 1);
    } else {
        if (m->ttl_init == 0U) snprintf(buf, sizeof(buf), "FROM:%s HOP:OFF", m->from);
        else snprintf(buf, sizeof(buf), "FROM:%s HOP:%u/%u", m->from, used_hops, m->ttl_init);
        GUI_DisplaySmallest(buf, 0, 9, false, true);
    }

    /* 0.2.6: tighter message box.  Metadata moved down 1 px, message text
     * begins 4 px higher than before, and footer labels move up 1 px while
     * keeping the real-LCD safe area. */
    draw_dotted_separator(17);
    print_wrapped_small_y(m->text, 20, 3);
    draw_dotted_separator(46);

    if (gMsgReadSource == MSG_SCREEN_OUTBOX) {
        GUI_DisplaySmallest("RESEND", 0, 49, false, true);
    } else {
        GUI_DisplaySmallest("REPLY", 0, 49, false, true);
    }
    GUI_DisplaySmallest("F:DEL", 104, 49, false, true);
}

static void draw_compose(void)
{
    char buf[12];
    draw_title("COMPOSE");

    /* Keep the compose title clean.  The message type and character counter
     * use the same metadata row style as the READ/SENT screens: directly
     * above the dotted top separator, in the 3x5 font. */
    GUI_DisplaySmallest("NEW MESSAGE", 0, 10, false, true);
    snprintf(buf, sizeof(buf), "%u/%u", (uint8_t)strlen(gMsgComposeBuf), (uint8_t)MSG_TEXT_LEN);
    {
        uint8_t w = (uint8_t)(strlen(buf) * 4U);
        uint8_t x = (w >= 128U) ? 0U : (uint8_t)(127U - w);
        GUI_DisplaySmallest(buf, x, 10, false, true);
    }

    draw_dotted_separator(17);
    print_wrapped_small_y(gMsgComposeBuf, 20, 3);
    draw_dotted_separator(46);
    GUI_DisplaySmallest("SEND", 0, 49, false, true);
    GUI_DisplaySmallest((gMsgEditor.mode == 2U) ? "2" : (gMsgEditor.upper ? "B" : "b"), 120, 49, false, true);
}

static void draw_callsign(void)
{
    draw_title("MSG CSG");
    UI_PrintStringSmallNormal("CALLSIGN:", 0, 0, 1);
    UI_PrintStringSmallBold(gMsgCallsignBuf, 0, 0, 3);
    UI_PrintStringSmallNormal((gMsgCallsignEditor.mode == 2U) ? "2" : (gMsgCallsignEditor.upper ? "B" : "b"), 118, 0, 6);
}

static void draw_settings(void)
{
    const char *names[] = { "MSG RX", "MSG CSG", "CALLTX", "ACK", "HOP", "BEEP", "LED", "DEBUG", "TESTMSG", "BACK" };
    char buf[24];
    draw_title("MSG SET");
    uint8_t start = 0;
    if (gMsgSettingsCursor >= 5) start = (uint8_t)(gMsgSettingsCursor - 4U);
    for (uint8_t row = 0; row < 5; row++) {
        uint8_t idx = (uint8_t)(start + row);
        if (idx >= 10) break;
        switch (idx) {
            case 0: snprintf(buf, sizeof(buf), "%s:%s", names[idx], gMessengerConfig.msg_rx ? "ON" : "OFF"); break;
            case 1: snprintf(buf, sizeof(buf), "%s:%s", names[idx], gMessengerConfig.callsign); break;
            case 2: snprintf(buf, sizeof(buf), "%s:%s", names[idx], gMessengerConfig.callsign_tx ? "ON" : "OFF"); break;
            case 3: snprintf(buf, sizeof(buf), "%s:%s", names[idx], gMessengerConfig.msg_ack ? "ON" : "OFF"); break;
            case 4: snprintf(buf, sizeof(buf), "%s:%u", names[idx], gMessengerConfig.msg_hop); break;
            case 5: snprintf(buf, sizeof(buf), "%s:%s", names[idx], gMessengerConfig.msg_beep ? "ON" : "OFF"); break;
            case 6: snprintf(buf, sizeof(buf), "%s:%u", names[idx], gMessengerConfig.msg_led); break;
            case 7: snprintf(buf, sizeof(buf), "%s:%s", names[idx], gMessengerConfig.msg_debug ? "ON" : "OFF"); break;
            case 8: snprintf(buf, sizeof(buf), "%s", names[idx]); break;
            case 9: snprintf(buf, sizeof(buf), "%s", names[idx]); break;
            default: buf[0] = 0; break;
        }
        print_line(buf, row + 1, gMsgSettingsCursor == idx);
    }
    if (gMessengerConfig.msg_debug) {
        const uint8_t page = (uint8_t)((gFlashLightBlinkCounter / 64U) & 1U);
        if (page == 0) {
            snprintf(buf, sizeof(buf), "P%04X A%04X R%u M%u",
                     MSG_RF_GetAckDbgPendingId(), MSG_RF_GetAckDbgRxId(),
                     MSG_RF_GetAckDbgRxCount(), MSG_RF_GetAckDbgMatchCount());
        } else {
            snprintf(buf, sizeof(buf), "S%04X W%u T%u X%u",
                     MSG_RF_GetAckDbgSentId(), MSG_RF_GetAckDbgWaitActive(),
                     MSG_RF_GetAckDbgRetryCount(), MSG_RF_GetAckDbgMissCount());
        }
        UI_PrintStringSmallNormal(buf, 0, 0, 6);
    } else {
        snprintf(buf, sizeof(buf), "%u/10", (uint8_t)(gMsgSettingsCursor + 1));
        print_right_small(buf, 6);
    }
}

void UI_DisplayMessenger(void)
{
    switch (gMsgScreen) {
        case MSG_SCREEN_HOME: draw_home(); break;
        case MSG_SCREEN_INBOX:
        case MSG_SCREEN_OUTBOX:
        case MSG_SCREEN_DRAFTS: draw_list(); break;
        case MSG_SCREEN_READ: draw_read(); break;
        case MSG_SCREEN_COMPOSE: draw_compose(); break;
        case MSG_SCREEN_CALLSIGN: draw_callsign(); break;
        case MSG_SCREEN_SETTINGS: draw_settings(); break;
        default: draw_home(); break;
    }
    ST7565_BlitFullScreen();
}
