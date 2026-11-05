#ifndef APPS_APP_CONFIG_PROXY_H
#define APPS_APP_CONFIG_PROXY_H

#include <stdint.h>
#include "app_overlay.h"

/* Optional descriptor at asset offset zero.  An app carrying it uses the
 * existing cfg_load/cfg_save callbacks to edit one bounded asset block owned
 * by another installed app.  Writes remain deferred until the editor exits. */
#define APP_CONFIG_PROXY_MAGIC 0x31505843u /* "CXP1" */
#define APP_CONFIG_PROXY_MAX   117u
#define APP_CONFIG_LOCAL_MAX   16u

typedef struct {
    uint32_t magic;
    char     target_name[APP_NAME_LEN];
    uint16_t target_offset;
    uint8_t  target_len;
    uint8_t  reserved;
    uint32_t target_magic;
} app_config_proxy_t;

_Static_assert(sizeof(app_config_proxy_t) == 28u,
               "app config proxy descriptor must stay 28 bytes");

#endif
