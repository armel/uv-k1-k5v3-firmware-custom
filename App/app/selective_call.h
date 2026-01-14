#ifndef SELECTIVE_CALL_H
#define SELECTIVE_CALL_H

#include <stdint.h>
#include <stdbool.h>

#define SELECTIVE_MAX_TONES 5
#define SELECTIVE_TONE_DURATION_MS 70
#define SELECTIVE_TONE_PAUSE_MS    30

// Séquences REGA (ZVEI2 officiel Secours Montagne / SDIS 74)
#define REGA_SOS_SEQ  {2,1,4,1,4}   // 21414
#define REGA_TEST_SEQ {2,1,3,0,1}   // 21301

// Modes supportés
typedef enum {
    SELECTIVE_OFF = 0,
    SELECTIVE_ZVEI1,
    SELECTIVE_ZVEI2,        // utilisé en Alpes / REGA / SDIS 74
    SELECTIVE_CCIR,

    // Modes REGA spécialisés
    SELECTIVE_REGA_SOS,     // ZVEI2 → 21414
    SELECTIVE_REGA_TEST     // ZVEI2 → 21301
} selective_mode_t;

// Configuration TX/RX par canal
typedef struct {
    selective_mode_t mode;
    uint8_t tx_seq[SELECTIVE_MAX_TONES];  // Séquence d'émission
    uint8_t rx_seq[SELECTIVE_MAX_TONES];  // Séquence à détecter
} selective_conf_t;

// API
void SelectiveCall_Send(const selective_conf_t *conf);
void SelectiveCall_ResetRx(void);
void SelectiveCall_ProcessTone(uint8_t tone);
bool SelectiveCall_Match(void);
void SelectiveCall_SendFromEeprom(void);


#endif
