#ifndef APPS_AF_HANDOFF_H
#define APPS_AF_HANDOFF_H

#include <stdint.h>

/* AF Scope (oscilloscope, tuner) and AF Spectro (spectrum, sonogram) are one
 * instrument split in two overlays: MENU on the last view of one hands off to
 * the other. The message keeps the audio path and the speaker, and carries
 * AF Spectro's Bell 202 twist sums through AF Scope, so that the twist
 * average survives a round trip. */
#define AF_SCOPE_APP_NAME    "AF Scope"
#define AF_SPECTRO_APP_NAME  "AF Spectro"
#define AF_HANDOFF_MAGIC     0xAFu

typedef struct {
    uint8_t  magic, raw, spk, pad[5];   /* AF_HANDOFF_MAGIC, path, speaker */
    uint64_t elo, ehi;                  /* AF Spectro's twist band energies */
} af_handoff_t;

_Static_assert(sizeof(af_handoff_t) == 24u, "AF hand-off message layout changed");

#endif
