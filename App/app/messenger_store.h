#ifndef APP_MESSENGER_STORE_H
#define APP_MESSENGER_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define MSG_CALLSIGN_LEN       8
#define MSG_CALLSIGN_EDIT_LEN  6
#define MSG_TEXT_LEN           36
#define MSG_INBOX_CAPACITY     20
#define MSG_OUTBOX_CAPACITY    10
#define MSG_DRAFT_CAPACITY     8

#define MSG_STATUS_NONE        0u
#define MSG_STATUS_PENDING     1u
#define MSG_STATUS_ACKED       2u
#define MSG_STATUS_FAILED      3u

typedef struct {
    bool     used;
    bool     unread;
    uint16_t id;
    uint8_t  ttl_init;
    uint8_t  ttl_remain;
    uint8_t  status;
    char     from[MSG_CALLSIGN_LEN + 1];
    char     to[MSG_CALLSIGN_LEN + 1];
    char     text[MSG_TEXT_LEN + 1];
} MSG_Message_t;

typedef struct {
    uint8_t magic;
    uint8_t version;
    uint8_t msg_rx;
    uint8_t callsign_tx;
    uint8_t msg_ack;
    uint8_t msg_hop;
    uint8_t msg_beep;
    uint8_t msg_led;
    uint8_t msg_debug;
    uint16_t next_msg_id;
    char    callsign[MSG_CALLSIGN_LEN + 1];
    char    drafts[MSG_DRAFT_CAPACITY][MSG_TEXT_LEN + 1];
} MSG_Config_t;

extern MSG_Config_t gMessengerConfig;
extern MSG_Message_t gMessengerInbox[MSG_INBOX_CAPACITY];
extern MSG_Message_t gMessengerOutbox[MSG_OUTBOX_CAPACITY];

void MSG_STORE_Init(void);
void MSG_STORE_SaveConfig(void);
uint16_t MSG_STORE_NextMsgId(void);
bool MSG_STORE_IsDuplicateInbox(const char *from, uint16_t id);
void MSG_STORE_AddInboxMessage(const char *text, const char *from, const char *to, uint16_t id, uint8_t ttl_init, uint8_t ttl_remain, bool unread);
void MSG_STORE_AddOutboxMessage(const char *text, const char *from, const char *to, uint16_t id, uint8_t ttl_init, uint8_t ttl_remain);
void MSG_STORE_SetOutboxStatusById(uint16_t id, uint8_t status);
void MSG_STORE_AddInboxDemo(const char *text);
void MSG_STORE_AddOutboxDemo(const char *text);
bool MSG_STORE_InjectNativePacket(const char *text);
void MSG_STORE_DeleteInbox(uint8_t index);
void MSG_STORE_DeleteOutbox(uint8_t index);
void MSG_STORE_MarkInboxRead(uint8_t index);
uint8_t MSG_STORE_CountInbox(void);
uint8_t MSG_STORE_CountOutbox(void);
uint8_t MSG_STORE_CountDrafts(void);
bool MSG_STORE_HasUnread(void);
void MSG_STORE_SetDraft(uint8_t index, const char *text);

#endif
