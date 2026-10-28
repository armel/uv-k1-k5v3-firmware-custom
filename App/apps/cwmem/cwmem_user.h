#ifndef APPS_CWMEM_USER_H
#define APPS_CWMEM_USER_H

/* User block at asset offset 0 of CW Mem: the callsign and the custom
 * messages of keys 7, 8 and 9. CW Mem only reads it; CW Mem Edit writes it
 * through the configuration proxy. Text fields are NUL-padded; the callsign
 * may fill its field without a NUL. An empty callsign means the one of the
 * boot message (LINE 1). Bump the marker when the layout changes: the proxy
 * then finds no target and CW Mem Edit refuses to save. */
#define MEMU_MAGIC        0x314D5743u /* "CWM1" */
#define MEMU_CALL         4u
#define MEMU_CALL_MAX     10u
#define MEMU_MSG          14u
#define MEMU_MSG_STRIDE   32u         /* 31 characters and their NUL */
#define MEMU_MSG_COUNT    3u
#define MEMU_LEN          110u

#define CW_MEM_APP_NAME      "CW Mem"
#define CW_MEM_EDIT_APP_NAME "CW Mem Edit"

_Static_assert(MEMU_CALL + MEMU_CALL_MAX == MEMU_MSG &&
               MEMU_MSG + MEMU_MSG_COUNT * MEMU_MSG_STRIDE == MEMU_LEN,
               "CW Mem user block layout must match its declared length");

#endif
