#include <string.h>
#include "app/messenger_store.h"
#include "app/messenger_packet.h"
#include "driver/py25q16.h"
#include "audio.h"
#include "misc.h"

#define MSG_CFG_MAGIC 0x47u
#define MSG_CFG_VERSION 4u
#define MSG_CFG_FLASH_ADDR 0x012000u
#define MSG_CFG_FLASH_SIZE 0x1000u

MSG_Config_t gMessengerConfig;
MSG_Message_t gMessengerInbox[MSG_INBOX_CAPACITY];
MSG_Message_t gMessengerOutbox[MSG_OUTBOX_CAPACITY];

static void MSG_STORE_DefaultConfig(void)
{
    memset(&gMessengerConfig, 0, sizeof(gMessengerConfig));
    gMessengerConfig.magic = MSG_CFG_MAGIC;
    gMessengerConfig.version = MSG_CFG_VERSION;
    gMessengerConfig.msg_rx = 1;
    gMessengerConfig.callsign_tx = 1;
    gMessengerConfig.msg_ack = 0;
    gMessengerConfig.msg_hop = 0;
    gMessengerConfig.msg_beep = 1;
    gMessengerConfig.msg_led = 1;
    gMessengerConfig.msg_debug = 0;
    gMessengerConfig.next_msg_id = 1;
    strncpy(gMessengerConfig.callsign, "UVK1", MSG_CALLSIGN_EDIT_LEN);
    strncpy(gMessengerConfig.drafts[0], "OK", MSG_TEXT_LEN);
    strncpy(gMessengerConfig.drafts[1], "NEED HELP", MSG_TEXT_LEN);
    strncpy(gMessengerConfig.drafts[2], "WHERE ARE YOU?", MSG_TEXT_LEN);
    strncpy(gMessengerConfig.drafts[3], "ON THE WAY", MSG_TEXT_LEN);
    strncpy(gMessengerConfig.drafts[4], "ARRIVED SAFE", MSG_TEXT_LEN);
    strncpy(gMessengerConfig.drafts[5], "CALL ME", MSG_TEXT_LEN);
    strncpy(gMessengerConfig.drafts[6], "NEGATIVE", MSG_TEXT_LEN);
    strncpy(gMessengerConfig.drafts[7], "BATTERY LOW", MSG_TEXT_LEN);
}

static void flash_read_struct(uint32_t addr, void *dst, uint16_t size)
{
    PY25Q16_ReadBuffer(addr, dst, size);
}

static void flash_write_struct(uint32_t addr, const void *src, uint16_t size)
{
    // Store Messenger data in a dedicated GOGUFW flash sector.
    // Do not use EEPROM-compatible addresses here: the old 0x1E80 location
    // overlaps the MR/channel memory compatibility area.
    if (size > MSG_CFG_FLASH_SIZE) return;
    PY25Q16_WriteBuffer(addr, src, size, false);
}

static void MSG_STORE_SanitizeCallsign(void)
{
    gMessengerConfig.callsign[MSG_CALLSIGN_EDIT_LEN] = 0;
    for (uint8_t i = 0; i < MSG_CALLSIGN_EDIT_LEN; i++) {
        char c = gMessengerConfig.callsign[i];
        if (c == 0) break;
        if (c >= 'a' && c <= 'z') gMessengerConfig.callsign[i] = (char)(c - ('a' - 'A'));
    }
}

void MSG_STORE_SaveConfig(void)
{
    MSG_STORE_SanitizeCallsign();
    flash_write_struct(MSG_CFG_FLASH_ADDR, &gMessengerConfig, sizeof(gMessengerConfig));
}

void MSG_STORE_Init(void)
{
    flash_read_struct(MSG_CFG_FLASH_ADDR, &gMessengerConfig, sizeof(gMessengerConfig));
    if (gMessengerConfig.magic != MSG_CFG_MAGIC || gMessengerConfig.version != MSG_CFG_VERSION) {
        MSG_STORE_DefaultConfig();
        MSG_STORE_SaveConfig();
    } else {
        MSG_STORE_SanitizeCallsign();
    }
}

static uint8_t count_list(MSG_Message_t *list, uint8_t cap)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < cap; i++) if (list[i].used) n++;
    return n;
}

static void add_to_list(MSG_Message_t *list, uint8_t cap, const char *text, const char *from, const char *to,
                        uint16_t id, uint8_t ttl_init, uint8_t ttl_remain, bool unread)
{
    for (int i = cap - 1; i > 0; i--) list[i] = list[i - 1];
    memset(&list[0], 0, sizeof(list[0]));
    list[0].used = true;
    list[0].unread = unread;
    list[0].id = id;
    list[0].ttl_init = ttl_init;
    list[0].ttl_remain = ttl_remain;
    list[0].status = MSG_STATUS_NONE;
    strncpy(list[0].from, from && from[0] ? from : "UVK1", MSG_CALLSIGN_LEN);
    strncpy(list[0].to, to && to[0] ? to : "ALL", MSG_CALLSIGN_LEN);
    strncpy(list[0].text, text ? text : "TEST", MSG_TEXT_LEN);
}

uint16_t MSG_STORE_NextMsgId(void)
{
    uint16_t id = gMessengerConfig.next_msg_id++;
    if (gMessengerConfig.next_msg_id == 0) gMessengerConfig.next_msg_id = 1;
    MSG_STORE_SaveConfig();
    return id;
}

bool MSG_STORE_IsDuplicateInbox(const char *from, uint16_t id)
{
    if (id == 0u) return false;
    const char *safe_from = (from && from[0]) ? from : "UVK1";
    for (uint8_t i = 0; i < MSG_INBOX_CAPACITY; i++) {
        if (!gMessengerInbox[i].used) continue;
        if (gMessengerInbox[i].id == id &&
            strncmp(gMessengerInbox[i].from, safe_from, MSG_CALLSIGN_LEN) == 0) {
            return true;
        }
    }
    return false;
}

void MSG_STORE_AddInboxMessage(const char *text, const char *from, const char *to, uint16_t id, uint8_t ttl_init, uint8_t ttl_remain, bool unread)
{
    /* Duplicate retry frames must not create a second inbox entry or trigger
     * unread/beep state. ACK resend is handled by the RF layer before this call. */
    if (MSG_STORE_IsDuplicateInbox(from, id)) return;

    add_to_list(gMessengerInbox, MSG_INBOX_CAPACITY, text, from, to, id, ttl_init, ttl_remain, unread);
    gUpdateStatus = true;
    gUpdateDisplay = true;
    if (unread && gMessengerConfig.msg_beep) {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_FORCE;
    }
}

void MSG_STORE_AddOutboxMessage(const char *text, const char *from, const char *to, uint16_t id, uint8_t ttl_init, uint8_t ttl_remain)
{
    add_to_list(gMessengerOutbox, MSG_OUTBOX_CAPACITY, text, from, to, id, ttl_init, ttl_remain, false);
    gMessengerOutbox[0].status = MSG_STATUS_PENDING;
}

void MSG_STORE_SetOutboxStatusById(uint16_t id, uint8_t status)
{
    for (uint8_t i = 0; i < MSG_OUTBOX_CAPACITY; i++) {
        if (gMessengerOutbox[i].used && gMessengerOutbox[i].id == id) {
            gMessengerOutbox[i].status = status;
            gUpdateDisplay = true;
            return;
        }
    }
}

void MSG_STORE_AddInboxDemo(const char *text)
{
    MSG_STORE_AddInboxMessage(text, "DEMO", "ALL", MSG_STORE_NextMsgId(), gMessengerConfig.msg_hop, gMessengerConfig.msg_hop, true);
}

void MSG_STORE_AddOutboxDemo(const char *text)
{
    MSG_STORE_AddOutboxMessage(text, gMessengerConfig.callsign, "ALL", MSG_STORE_NextMsgId(), gMessengerConfig.msg_hop, gMessengerConfig.msg_hop);
}

bool MSG_STORE_InjectNativePacket(const char *text)
{
    uint8_t frame[MSG_PKT_WIRE_LEN];
    MSG_Packet_t pkt;
    uint16_t id = MSG_STORE_NextMsgId();
    if (!MSG_PACKET_BuildText(frame, sizeof(frame), id, "NODE2", text ? text : "NATIVE TEST", gMessengerConfig.msg_hop)) return false;
    if (!MSG_PACKET_Parse(frame, sizeof(frame), &pkt)) return false;
    if (pkt.type != MSG_PKT_TYPE_TEXT) return false;
    MSG_STORE_AddInboxMessage(pkt.payload, pkt.from, pkt.to, pkt.id, pkt.ttl_init, pkt.ttl_remain, true);
    return true;
}

static void delete_from_list(MSG_Message_t *list, uint8_t cap, uint8_t index)
{
    if (index >= cap || !list[index].used) return;
    for (uint8_t i = index; i + 1 < cap; i++) list[i] = list[i + 1];
    memset(&list[cap - 1], 0, sizeof(list[cap - 1]));
}

void MSG_STORE_DeleteInbox(uint8_t index) { delete_from_list(gMessengerInbox, MSG_INBOX_CAPACITY, index); }
void MSG_STORE_DeleteOutbox(uint8_t index) { delete_from_list(gMessengerOutbox, MSG_OUTBOX_CAPACITY, index); }
void MSG_STORE_MarkInboxRead(uint8_t index)
{
    if (index < MSG_INBOX_CAPACITY) {
        gMessengerInbox[index].unread = false;
        gUpdateStatus = true;
        gUpdateDisplay = true;
    }
}
uint8_t MSG_STORE_CountInbox(void) { return count_list(gMessengerInbox, MSG_INBOX_CAPACITY); }
uint8_t MSG_STORE_CountOutbox(void) { return count_list(gMessengerOutbox, MSG_OUTBOX_CAPACITY); }
uint8_t MSG_STORE_CountDrafts(void) { return MSG_DRAFT_CAPACITY; }

void MSG_STORE_SetDraft(uint8_t index, const char *text)
{
    if (index >= MSG_DRAFT_CAPACITY) return;
    memset(gMessengerConfig.drafts[index], 0, sizeof(gMessengerConfig.drafts[index]));
    if (text && text[0]) {
        strncpy(gMessengerConfig.drafts[index], text, MSG_TEXT_LEN);
        gMessengerConfig.drafts[index][MSG_TEXT_LEN] = 0;
    }
    MSG_STORE_SaveConfig();
    gUpdateDisplay = true;
}


bool MSG_STORE_HasUnread(void)
{
    for (uint8_t i = 0; i < MSG_INBOX_CAPACITY; i++) if (gMessengerInbox[i].used && gMessengerInbox[i].unread) return true;
    return false;
}
