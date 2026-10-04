/*
 * DTMF Menu - Overlay App for UV-K1 / UV-K5 (F4HWN firmware)
 *
 * Menu z 7 definiowalnymi w kodzie pozycjami DTMF.
 * Wybranie pozycji nadaje zadany kod DTMF na wybranej czestotliwosci
 * z wybrana moca i nastepnie powraca bezposrednio do ekranu VFO.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"

#define LCD_WIDTH    128
#define TICK_MS      50

/* ==========================================================================
 * KONFIGURACJA CZASÓW DTMF (w milisekundach)
 * ========================================================================== */
#define DTMF_PREAMBLE_MS 200  /* Domyslna preambula / czas nosnej przed DTMF (ms) */
#define DTMF_DIGIT_MS    100  /* Domyslny czas trwania pojedynczego tonu DTMF (ms)*/
#define DTMF_GAP_MS      40   /* Przerwa miedzy tonami                     */
#define DTMF_POST_TX_MS  80   /* Podtrzymanie nosnej po zakonczeniu tonow  */

/* ==========================================================================
 * POZIOMY MOCY NADAJNIKA (power)
 * ==========================================================================
 * PWR_CURRENT (0)  - pozostaw moc ustawiona w aktualnym VFO radia
 * PWR_20MW    (8)  - bardzo niska moc (~20 mW)
 * PWR_100MW   (15) - mala moc (~100-125 mW)
 * PWR_250MW   (30) - mala moc (~250 mW)
 * PWR_500MW   (45) - niska moc (~500 mW)
 * PWR_1W      (70) - moc ~1 W
 * PWR_LOW     (1)  - standardowe LOW radia (~0.5 W)
 * PWR_MID     (2)  - standardowe MID radia (~2.0 W)
 * PWR_HIGH    (3)  - standardowe HIGH radia (~5.0 W)
 * 4..255           - bezposredni rejestr bias wzmacniacza BK4819
 * ========================================================================== */
#define PWR_CURRENT    0
#define PWR_LOW        1
#define PWR_MID        2
#define PWR_HIGH       3
#define PWR_20MW       8
#define PWR_100MW      15
#define PWR_250MW      30
#define PWR_500MW      45
#define PWR_1W         70

/* ==========================================================================
 * DEFINICJA STRUKTURY POZYCJI DTMF
 * ========================================================================== */
typedef struct {
    const char *name;        /* Nazwa wyswietlana w menu (do ~10 znakow)     */
    uint32_t   freq;        /* Czestotliwosc w Hz (np. 432333000) lub kHz   */
                             /* (np. 432333). 0 = czestotliwosc biezacego VFO */
    uint8_t    power;       /* Poziom mocy: PWR_CURRENT, PWR_100MW, PWR_LOW..*/
    uint16_t   preamble_ms; /* Preambula w ms (0 = domyslna DTMF_PREAMBLE_MS)*/
    uint16_t   digit_ms;    /* Dlugosc tonu w ms (0 = domyslna DTMF_DIGIT_MS)*/
    const char *dtmf;        /* Ciag znakow DTMF: '0'-'9','A'-'D','*','#',','*/
                             /* Pusty "" = pozycja nieaktywna/pusta           */
} dtmf_preset_t;

/* ==========================================================================
 * 7 POZYCJI MENU - TUTAJ ZDEFINIUJ SWOJE PARAMETRY
 * ==========================================================================
 * Znak przecinka ',' w kodzie DTMF powoduje pauze 250 ms.
 * ========================================================================== */
static const dtmf_preset_t PRESETS[7] = {
    { "BRAMA",    432333000, PWR_100MW,   200, 100, "2137" },
    { "[EMPTY 2]", 0,        PWR_CURRENT, 200, 100, ""     },
    { "[EMPTY 3]", 0,        PWR_CURRENT, 200, 100, ""     },
    { "[EMPTY 4]", 0,        PWR_CURRENT, 200, 100, ""     },
    { "[EMPTY 5]", 0,        PWR_CURRENT, 200, 100, ""     },
    { "[EMPTY 6]", 0,        PWR_CURRENT, 200, 100, ""     },
    { "[EMPTY 7]", 0,        PWR_CURRENT, 200, 100, ""     },
};

/* ==========================================================================
 * IMPLEMENTACJA
 * ========================================================================== */
static const app_api_t *A;

/* Tabela czestotliwosci tonow DTMF */
typedef struct {
    char     ch;
    uint16_t f1;
    uint16_t f2;
} dtmf_tone_t;

static const dtmf_tone_t DTMF_TONES[16] = {
    { '0', 941, 1336 },
    { '1', 697, 1209 },
    { '2', 697, 1336 },
    { '3', 697, 1477 },
    { '4', 770, 1209 },
    { '5', 770, 1336 },
    { '6', 770, 1477 },
    { '7', 852, 1209 },
    { '8', 852, 1336 },
    { '9', 852, 1477 },
    { 'A', 697, 1633 },
    { 'B', 770, 1633 },
    { 'C', 852, 1633 },
    { 'D', 941, 1633 },
    { '*', 941, 1209 },
    { '#', 941, 1477 },
};

/* Proste narzedzia formatowania bez zaleznosci od libc */
static uint8_t slen(const char *s) {
    uint8_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static char *put(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
    return dst;
}

static char *putu(char *dst, uint32_t val) {
    char buf[10];
    int8_t n = 0;
    do {
        buf[n++] = (char)('0' + (val % 10));
        val /= 10;
    } while (val && n < 10);
    while (n--) *dst++ = buf[n];
    *dst = '\0';
    return dst;
}

/* Formatowanie czestotliwosci w MHz (np. "432.333") */
static void format_freq_mhz(char *dst, uint32_t f_raw) {
    if (f_raw == 0) {
        *dst = '\0';
        return;
    }
    uint32_t f_hz = (f_raw < 10000000u) ? (f_raw * 1000u) : f_raw;
    uint32_t mhz = f_hz / 1000000u;
    uint32_t khz = (f_hz % 1000000u) / 1000u;

    char *o = putu(dst, mhz);
    *o++ = '.';
    if (khz < 100) *o++ = '0';
    if (khz < 10)  *o++ = '0';
    o = putu(o, khz);
    *o = '\0';
}

/* Wyszukiwanie tonow dla danego znaku DTMF */
static bool get_dtmf_tones(char c, uint16_t *f1, uint16_t *f2) {
    if (c >= 'a' && c <= 'd') c -= 32;
    for (uint8_t i = 0; i < 16; i++) {
        if (DTMF_TONES[i].ch == c) {
            *f1 = DTMF_TONES[i].f1;
            *f2 = DTMF_TONES[i].f2;
            return true;
        }
    }
    return false;
}

/* Obliczenie wartosci rejestru BK4819 dla zadanej czestotliwosci tonu */
static inline uint16_t bk_tone_reg(uint16_t hz) {
    return (uint16_t)((((uint32_t)hz * 103244ULL) + 5000ULL) / 10000ULL);
}

/* Rysowanie glownego menu */
static void draw_menu(uint8_t selected) {
    A->display_clear();
    A->status_clear();

    /* Naglowek */
    A->print_inverse("  DTMF PRESETS  ", 0, 0, true, true, LCD_WIDTH);
    A->draw_battery();
    A->blit_status();

    char line_buf[24];
    for (uint8_t i = 0; i < 7; i++) {
        uint8_t line = i + 1;
        char *o = line_buf;
        *o++ = (char)('1' + i);
        *o++ = ':';
        *o++ = ' ';
        o = put(o, PRESETS[i].name ? PRESETS[i].name : "");

        /* Jesli zdefiniowano czestotliwosc, dopisz ja skrotowo z prawej */
        if (PRESETS[i].freq > 0) {
            char fbuf[12];
            format_freq_mhz(fbuf, PRESETS[i].freq);
            uint8_t flen = slen(fbuf);
            uint8_t text_len = slen(line_buf);
            if (text_len + flen < 18) {
                while (text_len < (uint8_t)(17 - flen)) {
                    line_buf[text_len++] = ' ';
                }
                line_buf[text_len] = '\0';
                put(line_buf + text_len, fbuf);
            }
        }

        A->print_normal(line_buf, 2, 0, line);

        /* Podswietlenie wybranej pozycji (inwersja calej linii) */
        if (i == selected) {
            for (uint8_t x = 0; x < LCD_WIDTH; x++) {
                A->fb[line][x] = ~A->fb[line][x];
            }
        }
    }

    A->blit_full();
}

/* Ekran ostrzezenia o pustej pozycji */
static void show_empty(void) {
    A->display_clear();
    A->print_inverse("  DTMF PRESETS  ", 0, 0, true, true, LCD_WIDTH);
    A->print_normal("POZYCJA PUSTA!", 20, 0, 3);
    A->print_normal("Brak kodu DTMF", 22, 0, 4);
    A->blit_full();
    for (uint8_t i = 0; i < 12; i++) {
        A->delay_ms(50);
        A->backlight_update();
    }
}

/* Ekran odmowy nadawania */
static void show_tx_denied(void) {
    A->display_clear();
    A->print_inverse("  DTMF PRESETS  ", 0, 0, true, true, LCD_WIDTH);
    A->print_normal("TX ZABLOKOWANY!", 16, 0, 3);
    A->blit_full();
    for (uint8_t i = 0; i < 14; i++) {
        A->delay_ms(50);
        A->backlight_update();
    }
}

/* Nadanie sekwencji DTMF */
static void transmit_preset(uint8_t idx) {
    const dtmf_preset_t *p = &PRESETS[idx];
    if (!p->dtmf || !p->dtmf[0]) {
        show_empty();
        return;
    }

    if (A->tx_state() != 0) {
        show_tx_denied();
        return;
    }

    /* Ustalenie czestotliwosci nadawania (w jednostkach 10 Hz) */
    uint32_t freq10 = 0;
    if (p->freq > 0) {
        uint32_t f_hz = (p->freq < 10000000u) ? (p->freq * 1000u) : p->freq;
        freq10 = f_hz / 10u;
    } else {
        freq10 = A->tx_freq();
    }

    /* Ekran nadawania */
    A->display_clear();
    A->status_clear();
    A->print_inverse("   NADAWANIE DTMF   ", 0, 0, true, true, LCD_WIDTH);

    char buf[24];
    char *o = put(buf, "POZ ");
    *o++ = (char)('1' + idx);
    *o++ = ':';
    *o++ = ' ';
    put(o, p->name ? p->name : "");
    A->print_normal(buf, 2, 0, 2);

    char fbuf[16];
    format_freq_mhz(fbuf, (freq10 * 10u));
    o = put(buf, fbuf);
    o = put(o, " MHz ");
    if (p->power == PWR_100MW)      put(o, "100mW");
    else if (p->power == PWR_20MW)  put(o, "20mW");
    else if (p->power == PWR_250MW) put(o, "250mW");
    else if (p->power == PWR_500MW) put(o, "500mW");
    else if (p->power == PWR_1W)    put(o, "1W");
    else if (p->power == PWR_LOW)   put(o, "LOW");
    else if (p->power == PWR_MID)   put(o, "MID");
    else if (p->power == PWR_HIGH)  put(o, "HIGH");
    else if (p->power == PWR_CURRENT) put(o, "VFO");
    A->print_normal(buf, 2, 0, 4);

    o = put(buf, "DTMF: ");
    put(o, p->dtmf);
    A->print_bold(buf, 2, 0, 6);

    A->blit_full();

    /* Uruchomienie nadajnika (PA, nośna, czerwona dioda TX) */
    A->tx_set_params();

    /* Jesli zdefiniowano inna czestotliwosc niz biezaca VFO, wpisz ja do rejestrow BK4819 */
    if (p->freq > 0) {
        A->bk_write(0x38, (uint16_t)(freq10 & 0xFFFF));
        A->bk_write(0x39, (uint16_t)((freq10 >> 16) & 0xFFFF));

        /* Przelaczenie toru filtru VHF/UHF w GPIO BK4819 (rejestr 0x33) */
        uint16_t gpio = A->bk_read(0x33);
        if (freq10 < 28000000u) {
            gpio = (gpio & ~0x08u) | 0x04u; /* VHF LNA */
        } else {
            gpio = (gpio & ~0x04u) | 0x08u; /* UHF LNA */
        }
        A->bk_write(0x33, gpio);
    }

    /* Ustawienie mocy (bias rejestru 0x36) */
    uint8_t bias = 0;
    if (p->power == PWR_LOW)       bias = 20;
    else if (p->power == PWR_MID)  bias = 80;
    else if (p->power == PWR_HIGH) bias = 190;
    else if (p->power > 3)         bias = p->power;

    if (bias > 0) {
        uint8_t gain = (freq10 < 28000000u) ? ((1u << 3) | 0u) : ((4u << 3) | 2u);
        A->bk_write(0x36, (uint16_t)((bias << 8) | (1u << 7) | gain));
    }

    /* Wycisz tor audio przed nadaniem tonow i odczekaj czas preambuly */
    uint16_t pre_ms = (p->preamble_ms > 0) ? p->preamble_ms : DTMF_PREAMBLE_MS;
    A->tx_mute(true);
    A->delay_ms(pre_ms);

    /* Aktywacja generatora dwutonowego DTMF (Tone 1 gain 65, Tone 2 gain 93) */
    A->bk_write(0x70, 0xC1DD);

    /* Petla wysylania znakow DTMF */
    for (uint8_t i = 0; p->dtmf[i] != '\0'; i++) {
        char c = p->dtmf[i];
        if (c == ',' || c == ' ') {
            A->tx_mute(true);
            A->delay_ms(250);
            continue;
        }

        uint16_t f1, f2;
        if (get_dtmf_tones(c, &f1, &f2)) {
            A->bk_write(0x71, bk_tone_reg(f1));
            A->bk_write(0x72, bk_tone_reg(f2));

            uint16_t dig_ms = (p->digit_ms > 0) ? p->digit_ms : DTMF_DIGIT_MS;
            A->tx_mute(false);
            A->delay_ms(dig_ms);

            A->tx_mute(true);
            A->delay_ms(DTMF_GAP_MS);
        }
    }

    /* Wylaczenie generatora dwutonowego i zakonczenie nadawania */
    A->bk_write(0x70, 0x0000);
    A->delay_ms(DTMF_POST_TX_MS);

    A->tx_end();          /* Wylaczenie PA, diody TX i przywrocenie rejestrow RX */
    A->audio_path(false);

    /* Krotka chwila przed powrotem do VFO */
    A->delay_ms(200);
}

/* ==========================================================================
 * PUNKT WEJSCIA APLIKACJI OVERLAY
 * ========================================================================== */
__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api) {
    A = api;

    uint8_t selected = 0;
    uint8_t prevKey = APP_KEY_INVALID;
    bool running = true;

    A->backlight_on();
    draw_menu(selected);

    while (running) {
        uint8_t key = A->get_key();
        if (key != APP_KEY_INVALID && key != prevKey) {
            prevKey = key;
            A->backlight_on();

            if (key == APP_KEY_EXIT) {
                /* Natychmiastowy powrot do VFO */
                running = false;
                break;
            } else if (key == APP_KEY_UP) {
                selected = (selected > 0) ? (selected - 1) : 6;
                draw_menu(selected);
            } else if (key == APP_KEY_DOWN) {
                selected = (selected < 6) ? (selected + 1) : 0;
                draw_menu(selected);
            } else if (key >= APP_KEY_1 && key <= APP_KEY_7) {
                /* Bezposredni wybor pozycji klawiszami cyfrowymi 1..7 */
                uint8_t idx = key - APP_KEY_1;
                selected = idx;
                if (PRESETS[idx].dtmf && PRESETS[idx].dtmf[0]) {
                    transmit_preset(idx);
                    running = false; /* Po nadaniu powrot do VFO */
                    break;
                } else {
                    show_empty();
                    draw_menu(selected);
                }
            } else if (key == APP_KEY_MENU || key == APP_KEY_PTT) {
                /* Zatwierdzenie aktualnie podswietlonej pozycji */
                if (PRESETS[selected].dtmf && PRESETS[selected].dtmf[0]) {
                    transmit_preset(selected);
                    running = false; /* Po nadaniu powrot do VFO */
                    break;
                } else {
                    show_empty();
                    draw_menu(selected);
                }
            }
        } else if (key == APP_KEY_INVALID) {
            prevKey = APP_KEY_INVALID;
        }

        A->battery_sample();
        A->backlight_update();
        A->delay_ms(TICK_MS);
    }

    /* Zakonczenie aplikacji - czyszczenie i powrot do glownego firmware/VFO */
    A->tx_end();
    A->audio_path(false);
}
