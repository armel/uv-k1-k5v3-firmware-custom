#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "selective_call.h"
#include "driver/bk4819.h"
#include "driver/system.h"
#include "ui/ui.h"
#include "audio.h"
#include "settings.h"

// ------------------------------------------------------
// Tables des fréquences ZVEI-1, ZVEI-2, CCIR
// ------------------------------------------------------

// ZVEI-1 
static const uint16_t ZVEI1_TABLE[16] = {
    1060,1160,1270,1400,1600,
    1800,2000,2200,970, 885,
    810, 740, 680, 620, 550, 500
};

// ZVEI-2 (officiel pour canaux E secours montagne, REGA, SDIS)
static const uint16_t ZVEI2_TABLE[16] = {
    2600, // 0
    1060, // 1
    1160, // 2
    1270, // 3
    1400, // 4
    1600, // 5
    1800, // 6
    2000, // 7
    2200, // 8
    940,  // 9
    825,  // A
    770,  // B
    710,  // C
    660,  // D
    600,  // E
    530   // F
};

// CCIR 
static const uint16_t CCIR_TABLE[16] = {
    700,800,900,1000,1100,
    1200,1300,1400,1500,1600,
    1700,1800,1900,2000,2100,2200
};

// Séquences REGA (ZVEI2 officiel)
static const uint8_t REGA_SOS[5]  = REGA_SOS_SEQ;   // 21414
static const uint8_t REGA_TEST[5] = REGA_TEST_SEQ;  // 21301

// ------------------------------------------------------
// État interne RX
// ------------------------------------------------------
static uint8_t rx_buffer[SELECTIVE_MAX_TONES];
static uint8_t rx_index = 0;

static selective_conf_t current_cfg;   // config active pour comparaison

// ------------------------------------------------------
// Retourne la fréquence correspondant au code ZVEI/CCIR
// ------------------------------------------------------
static uint16_t get_frequency(selective_mode_t mode, uint8_t code)
{
    code &= 0x0F;

    switch (mode)
    {
        case SELECTIVE_ZVEI1:
            return ZVEI1_TABLE[code];

        case SELECTIVE_ZVEI2:
        case SELECTIVE_REGA_SOS:
        case SELECTIVE_REGA_TEST:
            return ZVEI2_TABLE[code];

        case SELECTIVE_CCIR:
            return CCIR_TABLE[code];

        default:
            return 0;
    }
}

// ------------------------------------------------------
// Prépare la séquence REGA automatiquement
// ------------------------------------------------------
static void prepare_rega_sequence(selective_conf_t *cfg)
{
    if (cfg->mode == SELECTIVE_REGA_SOS) {
        memcpy(cfg->tx_seq, REGA_SOS, 5);
        memcpy(cfg->rx_seq, REGA_SOS, 5);
    }
    else if (cfg->mode == SELECTIVE_REGA_TEST) {
        memcpy(cfg->tx_seq, REGA_TEST, 5);
        memcpy(cfg->rx_seq, REGA_TEST, 5);
    }
}

// ------------------------------------------------------
// ENVOI TX séquence 5 tons (ZVEI1, ZVEI2, CCIR, REGA)
// ------------------------------------------------------
void SelectiveCall_Send(const selective_conf_t *conf)
{
    if (!conf || conf->mode == SELECTIVE_OFF)
        return;

    selective_conf_t local = *conf;

    // Si mode REGA : remplir automatiquement la séquence
    if (local.mode == SELECTIVE_REGA_SOS ||
        local.mode == SELECTIVE_REGA_TEST)
    {
        prepare_rega_sequence(&local);
    }

    // stocker config active pour RX
    memcpy(&current_cfg, &local, sizeof(selective_conf_t));

    // Option : sidetone comme DTMF (on  entend aussi les tons sur la radio)
    if (gEeprom.DTMF_SIDE_TONE)
    {
        AUDIO_AudioPathOn();
       // gEnableSpeaker = true;
    }

    // Met le BK4819 en mode TX tonalités (comme pour  DTMF)
    BK4819_EnterDTMF_TX(gEeprom.DTMF_SIDE_TONE);

    // Laisser le temps au chemin TX/tone de se stabiliser (a voir: 100, 150, 200 ms)
    SYSTEM_DelayMs(SELECTIVE_PRE_DELAY_MS);

    for (uint8_t i = 0; i < SELECTIVE_MAX_TONES; i++)
    {
        uint8_t  code = local.tx_seq[i];
        uint16_t freq = get_frequency(local.mode, code);

        BK4819_PlaySingleTone(
            freq,
            SELECTIVE_TONE_DURATION_MS,
            100,     // amplitude (à ajuster si besoin)
            true     
        );

        SYSTEM_DelayMs(SELECTIVE_TONE_PAUSE_MS);
    }

    // Quitter le mode tonalités TX et revenir à la phonie
    BK4819_ExitDTMF_TX(false);

    // Couper sidetone
    AUDIO_AudioPathOff();
   // gEnableSpeaker = false;
}







// ------------------------------------------------------
// Réinitialise le buffer de réception
// ------------------------------------------------------
void SelectiveCall_ResetRx(void)
{
    rx_index = 0;
    memset(rx_buffer, 0xFF, sizeof(rx_buffer));
}

// ------------------------------------------------------
// Ajout d’un ton détecté coté RX
// ------------------------------------------------------
void SelectiveCall_ProcessTone(uint8_t tone)
{
    if (rx_index < SELECTIVE_MAX_TONES)
        rx_buffer[rx_index++] = tone;
}

// ------------------------------------------------------
// Vérifie la  correspondance RX avec séquence attendue
// ------------------------------------------------------
bool SelectiveCall_Match(void)
{
    if (rx_index < SELECTIVE_MAX_TONES)
        return false;

    for (uint8_t i = 0; i < SELECTIVE_MAX_TONES; i++) {
        if (rx_buffer[i] != current_cfg.rx_seq[i])
            return false;
    }

    return true;
}
// ------------------------------------------------------
// Helper : envoi SelCall basé sur la config globale EEPROM
// ------------------------------------------------------
void SelectiveCall_SendFromEeprom(void)
{
    // Si le SelCall est désactivé, on ne fait rien
    if (gEeprom.selective_mode == 0)   // SELECTIVE_OFF
        return;

    selective_conf_t conf;
    memset(&conf, 0, sizeof(conf));

    conf.mode = (selective_mode_t)gEeprom.selective_mode;

    // Pour l’instant : on utilise la même séquence en TX et RX
    for (uint8_t i = 0; i < SELECTIVE_MAX_TONES; i++) {
        conf.tx_seq[i] = gEeprom.selective_tx_seq[i];
        conf.rx_seq[i] = gEeprom.selective_tx_seq[i];
    }

    SelectiveCall_Send(&conf);
}
