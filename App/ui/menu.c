/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "../app/action.h"
#include "../app/dtmf.h"
#include "../app/menu.h"
#include "../bitmaps.h"
#include "../board.h"
#include "../dcs.h"
#include "../driver/backlight.h"
#include "../driver/bk4819.h"
#include "../driver/eeprom.h"
#include "../driver/st7565.h"
#include "../external/printf/printf.h"
#include "../font.h"
#include "../frequencies.h"
#include "../fw_assets.h"
#include "../helper/battery.h"
#include "../misc.h"
#include "../settings.h"

#ifdef ENABLE_FEAT_F4HWN
    #include "../version.h"
#endif

#include "helper.h"
#include "inputbox.h"
#include "menu.h"
#include "ui.h"
#include "welcome.h"
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
    #include "driver/mb_flash.h"
    #include "multiboot.h"
#endif


#define MENU_ITEM(name, id) id,
const uint8_t MenuListId[] =
{
#include "menu_list.def"
};
#undef MENU_ITEM

#define MENU_ITEM(name, id) name,
const char MenuListName[][MENU_NAME_SIZE] FW_ASSET =
{
#include "menu_list.def"
};
#undef MENU_ITEM

_Static_assert(ARRAY_SIZE(MenuListId) == ARRAY_SIZE(MenuListName), "menu list tables out of sync");

const uint8_t FIRST_HIDDEN_MENU_ITEM = MENU_F_LOCK;

#ifdef ENABLE_FEAT_F4HWN_SET_FNT
static const char gSetFntLabels[][19] FW_ASSET =
{
    "NORMAL / TERMINUS",
    "NORMAL / ALT",
    "NORMAL / 7 SEGMENT",
    "NORMAL / ORIGINAL",
    "CAPS / TERMINUS",
    "CAPS / ALT",
    "CAPS / 7 SEGMENT",
    "CAPS / ORIGINAL",
    "BLOCK / TERMINUS",
    "BLOCK / ALT",
    "BLOCK / 7 SEGMENT",
    "BLOCK / ORIGINAL"
};
_Static_assert(ARRAY_SIZE(gSetFntLabels) == FONT_VARIANT_COUNT,
               "one SetFnt label per font variant");
#endif

const char gSubMenu_TXP[][6] FW_ASSET =
{
    "USER",
    "LOW 1",
    "LOW 2",
    "LOW 3",
    "LOW 4",
    "LOW 5",
    "MID",
    "HIGH"
};

const char gSubMenu_SFT_D[][4] FW_ASSET =
{
    "OFF",
    "+",
    "-"
};

const char gSubMenu_W_N[][7] FW_ASSET =
{
    "WIDE",
    "NARROW"
};

const char gSubMenu_OFF_ON[][4] FW_ASSET =
{
    "OFF",
    "ON"
};

const char* gSubMenu_NA = "N/A";

const char gSubMenu_RXMode[][16] FW_ASSET =
{
    "MAIN\nONLY",       // TX and RX on main only
    "DUAL RX\nRESPOND", // Watch both and respond
    "CROSS\nBAND",      // TX on main, RX on secondary
    "MAIN TX\nDUAL RX", // always TX on main, but RX on both
#ifdef ENABLE_FEAT_F4HWN_FULL_WATCH
    "FULL RX\nRESPOND", // Watch A, B and priority channels, then respond on the promoted channel
    "MAIN TX\nFULL RX"  // Watch all channels, but always transmit on the selected VFO
#endif
};

#ifdef ENABLE_VOICE
    const char gSubMenu_VOICE[][4] FW_ASSET =
    {
        "OFF",
        "CHI",
        "ENG"
    };
#endif

const char gSubMenu_MDF[][15] FW_ASSET =
{
    "FREQ",
    "CHANNEL\nNUMBER",
    "NAME",
    "NAME\n+\nFREQ"
};

#ifdef ENABLE_DTMF_CALLING
const char gSubMenu_D_RSP[][11] FW_ASSET =
{
    "DO\nNOTHING",
    "RING",
    "REPLY",
    "BOTH"
};
#endif

const char gSubMenu_PTT_ID[][15] FW_ASSET =
{
    "OFF",
    "UP CODE",
    "DOWN CODE",
    "UP+DOWN\nCODE",
    "APOLLO\nQUINDAR"
};

const char gSubMenu_PONMSG[][8] FW_ASSET =
{
#ifdef ENABLE_FEAT_F4HWN
    "ALL",
    "SOUND",
#else
    "FULL",
#endif
    "MESSAGE",
    "VOLTAGE",
#ifdef ENABLE_FEAT_F4HWN_LOGO
    "LOGO",
#endif
    "NONE"
};

#if defined(ENABLE_FEAT_F4HWN) && defined(ENABLE_FEAT_F4HWN_LOGO_SAV)
const char gSubMenu_SET_SAV[][7] FW_ASSET =
{
    "OFF",
    "LOGO",
    "LOGO+",
    "MATRIX"
};
#endif

const char gSubMenu_ROGER[][6] FW_ASSET =
{
    "OFF",
    "ROGER",
    "MDC"
};

const char gSubMenu_RESET[][4] FW_ASSET =
{
    "VFO",
    "ALL"
};

const char gSubMenu_F_LOCK[][25] FW_ASSET =
{
    "DEFAULT+\n137-174\n400-470",
    "FCC HAM\n144-148\n420-450",
#ifdef ENABLE_FEAT_F4HWN_CA
    "CA HAM\n144-148\n430-450",
#endif
    "CE HAM\n144-146\n430-440",
    "GB HAM\n144-148\n430-440",
    "137-174\n400-430",
    "137-174\n400-438",
#ifdef ENABLE_FEAT_F4HWN_PMR
    "PMR 446",
#endif
#ifdef ENABLE_FEAT_F4HWN_GMRS_FRS_MURS
    "GMRS\nFRS\nMURS",
#endif
    "DISABLE\nALL",
    "UNLOCK\nALL",
};

const char gSubMenu_RX_TX[][6] FW_ASSET =
{
    "OFF",
    "TX",
    "RX",
    "TX/RX"
};

const char gSubMenu_BAT_TXT[][8] FW_ASSET =
{
    "NONE",
    "VOLTAGE",
    "PERCENT"
};

const char gSubMenu_BATTYP[][11] FW_ASSET =
{
    "1600mAh K5",
    "2200mAh K5",
    "3500mAh K5",
    "1400mAh K1",
    "2500mAh K1"
};

const char gSubMenu_SET_NAV[][17] FW_ASSET =
{
    "LEFT\nRIGHT\nUV-K1",
    "UP\nDOWN\nUV-K5(8)",
};

#ifndef ENABLE_FEAT_F4HWN
const char gSubMenu_SCRAMBLER[][7] FW_ASSET =
{
    "OFF",
    "2600Hz",
    "2700Hz",
    "2800Hz",
    "2900Hz",
    "3000Hz",
    "3100Hz",
    "3200Hz",
    "3300Hz",
    "3400Hz",
    "3500Hz"
};
#endif

#ifdef ENABLE_FEAT_F4HWN
    const char gSubMenu_SET_PWR[][6] FW_ASSET =
    {
        "< 20m",
        "125m",
        "250m",
        "500m",
        "1",
        "2",
        "5"
    };

    const char gSubMenu_SET_PTT[][8] FW_ASSET =
    {
        "CLASSIC",
        "ONEPUSH"
    };

    const char gSubMenu_SET_TOT[][7] FW_ASSET =
    {
        "OFF",
        "SOUND",
        "VISUAL",
        "ALL"
    };

    const char gSubMenu_SET_LCK[][17] FW_ASSET =
    {
        "KEYS",
        "KEYS\nACTIONS",
        "KEYS\nPTT",
        "KEYS\nACTIONS\nPTT"
    };

    const char gSubMenu_SET_MET[][8] FW_ASSET =
    {
        "TINY",
        "CLASSIC"
    };

    #ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
        const char gSubMenu_SET_SCN[][7] FW_ASSET =
        {
            "NORMAL",
            "FAST"
        };
    #endif

    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        const char gSubMenu_SET_AUD_FM[][6] FW_ASSET =
        {
            "FLAT",
            "CLEAN",
            "MID",
            "BOOST",
            "MAX"
        };

        const char gSubMenu_SET_AUD_AM[][6] FW_ASSET =
        {
            "SHARP",
            "STOCK",
            "OPEN"
        };
    #endif

    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        const char gSubMenu_SET_NFM[][9] FW_ASSET =
        {
            "NARROW",
            "NARROWER"
        };
    #endif

    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        const char gSubMenu_SET_KEY[][9] FW_ASSET =
        {
            "KEY_MENU",
            "KEY_UP",
            "KEY_DOWN",
            "KEY_EXIT",
            "KEY_STAR"
        };
    #endif
#endif

#define SIDEFUNCTION_NAMES(X) \
    X(ACTION_OPT_NONE,           "NONE") \
    X(ACTION_OPT_FLASHLIGHT,     "FLASH\nLIGHT") \
    X(ACTION_OPT_POWER,          "POWER") \
    X(ACTION_OPT_MONITOR,        "MONITOR") \
    X(ACTION_OPT_SCAN,           "SCAN") \
    X(ACTION_OPT_VOX,            "VOX") \
    X(ACTION_OPT_FM,             "FM RADIO") \
    X(ACTION_OPT_1750,           "1750Hz") \
    X(ACTION_OPT_KEYLOCK,        "LOCK\nKEYPAD") \
    X(ACTION_OPT_A_B,            "VFO A\nVFO B") \
    X(ACTION_OPT_VFO_MR,         "VFO\nMEM") \
    X(ACTION_OPT_SWITCH_DEMODUL, "MODE") \
    X(ACTION_OPT_RXMODE,         "RX MODE") \
    X(ACTION_OPT_MAINONLY,       "MAIN ONLY") \
    X(ACTION_OPT_PTT,            "PTT") \
    X(ACTION_OPT_WN,             "WIDE\nNARROW") \
    X(ACTION_OPT_MUTE,           "MUTE") \
    X(ACTION_OPT_RXA,            "RxA") \
    X(ACTION_OPT_RXTX_LOG,       "RF LOG") \
    X(ACTION_OPT_BEAM,           "BEAM") \
    X(ACTION_OPT_POWER_HIGH,     "POWER\nHIGH") \
    X(ACTION_OPT_REMOVE_OFFSET,  "REMOVE\nOFFSET") \
    X(ACTION_OPT_FOXHUNT,        "FOX HUNT") \
    X(ACTION_OPT_BEACON,         "BEACON") \
    X(ACTION_OPT_TONECALL,       "TONE CALL")

#define SIDEFUNCTION_NAME_ENTRY(action, name) [action] = name,
const char gSubMenu_SIDEFUNCTIONS[ACTION_OPT_LEN][14] FW_ASSET =
{
    SIDEFUNCTION_NAMES(SIDEFUNCTION_NAME_ENTRY)
};
#undef SIDEFUNCTION_NAME_ENTRY

#define SIDEFUNCTION_COUNT_ENTRY(action, name) + 1u
#define SIDEFUNCTION_MASK_ENTRY(action, name) | (1u << (action))
enum
{
    SIDEFUNCTION_NAME_COUNT = 0 SIDEFUNCTION_NAMES(SIDEFUNCTION_COUNT_ENTRY),
    SIDEFUNCTION_NAME_MASK  = 0 SIDEFUNCTION_NAMES(SIDEFUNCTION_MASK_ENTRY)
};
#undef SIDEFUNCTION_COUNT_ENTRY
#undef SIDEFUNCTION_MASK_ENTRY
#undef SIDEFUNCTION_NAMES

static_assert(ACTION_OPT_LEN < 32u);
static_assert((int)SIDEFUNCTION_NAME_COUNT == (int)ACTION_OPT_LEN);
static_assert(SIDEFUNCTION_NAME_MASK == ((1u << ACTION_OPT_LEN) - 1u));

bool    gIsInSubMenu;
uint8_t gMenuCursor;
uint8_t gMenuIndices[ARRAY_SIZE(MenuListId)]; // Etape 1: table position affichee -> index MenuList (vue courante)

int UI_MENU_GetCurrentMenuId() {
#ifdef ENABLE_FEAT_F4HWN_MENU_CAT
    if (gMenuLevel == MENU_LEVEL_CAT)
        return 0xFF;   // pas d'item courant au niveau categories
#endif
    if(gMenuCursor < gMenuListCount)
        return MenuListId[gMenuIndices[gMenuCursor]];

    return MenuListId[ARRAY_SIZE(MenuListId)-1];
}

uint8_t UI_MENU_GetMenuIdx(uint8_t id)
{
    for(uint8_t i = 0; i < ARRAY_SIZE(MenuListId); i++)
        if(MenuListId[i] == id)
            return i;
    return 0;
}

// Position dans la vue courante (gMenuIndices) du menu_id, ou gMenuCursor si absent.
// En vue All (identite) equivaut a UI_MENU_GetMenuIdx ; en vue categorie, donne la
// position filtree correcte.
uint8_t UI_MENU_GetViewPos(uint8_t id)
{
    for (uint8_t i = 0; i < gMenuListCount; i++)
        if (MenuListId[gMenuIndices[i]] == id)
            return i;
    return gMenuCursor;
}

#ifdef ENABLE_FEAT_F4HWN_MENU_CAT
// --- Etape 2a : donnee du classement par categorie (cible Fusion) ---
// Chaque liste = les menu_id d'une categorie, DANS l'ordre d'affichage voulu
// (ex. SetPwr colle a Power). CAT_ALL n'a pas de liste : il reprend MenuList
// tel quel, donc ordre et numeros d'origine preserves.
const char CategoryNames[CAT_COUNT][9] FW_ASSET = {
    [CAT_CHANNELS] = "Channels",
    [CAT_SCAN]     = "Scan",
    [CAT_KEYS]     = "Keys",
    [CAT_POWER]    = "Power",
    [CAT_DISPLAY]  = "Display",
    [CAT_TIMERS]   = "Timers",
    [CAT_AUDIO]    = "Audio",
    [CAT_RADIO]    = "Radio",
    [CAT_DTMF]     = "DTMF",
    [CAT_SERVICE]  = "Service",
    [CAT_ALL]      = "All",
};

// Les menu_id de sous-features optionnelles sont gardes exactement comme dans
// l'enum (menu.h) : sur un preset qui ne les compile pas, ils ne sont pas
// references (sinon build KO, ex. preset Custom). Les autres MENU_SET_* sont
// sous ENABLE_FEAT_F4HWN, garanti par la dependance CMake (App/CMakeLists.txt).
static const uint8_t CatChannels[] = {
    MENU_STEP, MENU_TXP, MENU_SET_PWR, MENU_R_DCS, MENU_R_CTCS, MENU_T_DCS,
    MENU_T_CTCS, MENU_SFT_D, MENU_OFFSET, MENU_W_N,
#ifdef ENABLE_FEAT_F4HWN_NARROWER
    MENU_SET_NFM,
#endif
    MENU_BCL, MENU_COMPAND, MENU_AM, MENU_TX_LOCK, MENU_PTT_ID, MENU_LIST_CH,
    MENU_MEM_CH, MENU_DEL_CH, MENU_MEM_NAME,
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
    MENU_SET_CFG,
#endif
};
static const uint8_t CatScan[]    = {
    MENU_S_LIST, MENU_S_PRI, MENU_S_PRI_CH_1, MENU_S_PRI_CH_2, MENU_SC_REV,
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
    MENU_SET_SCN,
#endif
};
static const uint8_t CatKeys[]    = {
    MENU_F1SHRT, MENU_F1LONG, MENU_F2SHRT, MENU_F2LONG, MENU_MLONG, MENU_AUTOLK, MENU_SET_LCK,
#ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
    MENU_SET_KEY,
#endif
    MENU_SET_PTT, MENU_1_CALL,
};
static const uint8_t CatPower[]   = {
    MENU_SAVE, MENU_BAT_TXT,
#ifdef ENABLE_FEAT_F4HWN_SLEEP
    MENU_SET_OFF,
#endif
#ifdef ENABLE_FEAT_F4HWN_LOGO_SAV
    MENU_SET_SAV,
#endif
};
static const uint8_t CatDisplay[] = {
    MENU_MDF, MENU_PONMSG, MENU_ABR, MENU_ABR_MIN, MENU_ABR_MAX,
    MENU_ABR_ON_TX_RX, MENU_SET_CTR, MENU_SET_INV, MENU_SET_MET, MENU_SET_GUI,
    MENU_VOL,
#ifdef ENABLE_FEAT_F4HWN_SET_FNT
    MENU_SET_FNT,
#endif
};
static const uint8_t CatTimers[]  = { MENU_TOT, MENU_SET_TOT, MENU_SET_EOT, MENU_SET_TMR };
static const uint8_t CatAudio[]   = {
    MENU_MIC, MENU_MIC_BAR, MENU_BEEP,
#ifdef ENABLE_FEAT_F4HWN_VOL
    MENU_SET_VOL,
#endif
#ifdef ENABLE_FEAT_F4HWN_AUDIO
    MENU_SET_AUD, MENU_SET_RXG,
#endif
};
static const uint8_t CatRadio[]   = { MENU_SQL, MENU_STE, MENU_RP_STE, MENU_ROGER, MENU_VOX, MENU_TDR };
static const uint8_t CatDtmf[]    = { MENU_UPCODE, MENU_DWCODE, MENU_D_ST, MENU_D_PRE, MENU_D_LIVE_DEC };
static const uint8_t CatService[] = { MENU_F_LOCK, MENU_350EN, MENU_BATCAL, MENU_BATTYP, MENU_SET_NAV, MENU_RESET };

typedef struct { const uint8_t *ids; uint8_t len; } cat_list_t;

static const cat_list_t CategoryLists[CAT_COUNT] = {
    [CAT_CHANNELS] = { CatChannels, ARRAY_SIZE(CatChannels) },
    [CAT_SCAN]     = { CatScan,     ARRAY_SIZE(CatScan)     },
    [CAT_KEYS]     = { CatKeys,     ARRAY_SIZE(CatKeys)     },
    [CAT_POWER]    = { CatPower,    ARRAY_SIZE(CatPower)    },
    [CAT_DISPLAY]  = { CatDisplay,  ARRAY_SIZE(CatDisplay)  },
    [CAT_TIMERS]   = { CatTimers,   ARRAY_SIZE(CatTimers)   },
    [CAT_AUDIO]    = { CatAudio,    ARRAY_SIZE(CatAudio)    },
    [CAT_RADIO]    = { CatRadio,    ARRAY_SIZE(CatRadio)    },
    [CAT_DTMF]     = { CatDtmf,     ARRAY_SIZE(CatDtmf)     },
    [CAT_SERVICE]  = { CatService,  ARRAY_SIZE(CatService)  },
    [CAT_ALL]      = { NULL, 0 },
};

uint8_t gMenuCategory = CAT_ALL;

// Index de 'id' dans MenuList, ou 0xFF si absent (item non compile).
static uint8_t menu_find_idx(uint8_t id)
{
    for (uint8_t i = 0; MenuListId[i] != 0xFF; i++)
        if (MenuListId[i] == id)
            return i;
    return 0xFF;
}

uint8_t gMenuLevel     = MENU_LEVEL_CAT;
uint8_t gCatOrder[CAT_COUNT];
uint8_t gMenuCatCursor = 0;
uint8_t gCatLastPos[CAT_COUNT];   // derniere position du curseur item, par categorie

// Nombre d'items presents (compiles) dans une categorie.
uint8_t UI_MENU_CategoryItemCount(uint8_t cat)
{
    uint8_t n = 0;

    if (cat == CAT_ALL)
    {
        for (uint8_t i = 0; MenuListId[i] != 0xFF; i++)
        {
            if (!gF_LOCK && MenuListId[i] == FIRST_HIDDEN_MENU_ITEM)
                break;
            n++;
        }
        return n;
    }

    const cat_list_t *cl = &CategoryLists[cat];
    for (uint8_t k = 0; k < cl->len; k++)
        if (menu_find_idx(cl->ids[k]) != 0xFF)
            n++;
    return n;
}

// Construit l'ecran niveau categories : gCatOrder = categories visibles,
// gMenuListCount = leur nombre. Service n'apparait que si gF_LOCK.
void UI_MENU_BuildCategoryScreen(void)
{
    gMenuListCount = 0;
    for (uint8_t c = 0; c < CAT_COUNT; c++)
    {
        if (c == CAT_SERVICE && !gF_LOCK)
            continue;
        gCatOrder[gMenuListCount++] = c;
    }
}

// Rendu de l'ecran des categories (niveau 1).
static void UI_MENU_DrawCategories(void)
{
    char str[16];
    const unsigned int sep = 64;          // separateur decale a droite : noms longs (ex. "Channels")
    const unsigned int x1  = sep + 2;
    const unsigned int x2  = LCD_WIDTH - 1;

    UI_DisplayClear();

    UI_DrawLineBuffer(gFrameBuffer, sep, 0, sep, 55, 1);
    for (uint8_t i = 0; i < sep; i += 2)
        gFrameBuffer[5][i] = 0x40;

    const int count = gMenuListCount;
    const int cur   = gMenuCursor;

    int prev = cur - 1; if (prev < 0)      prev = count - 1;
    int next = cur + 1; if (next >= count) next = 0;

    if (count > 1)
    {
        FW_ASSETS_STRCPY(str, CategoryNames, gCatOrder[prev]);
        UI_PrintStringSmallNormal(str, 0, 0, 1);
    }
    FW_ASSETS_STRCPY(str, CategoryNames, gCatOrder[cur]);
    UI_PrintString(str, 0, 0, 2, 8);
    if (count > 1)
    {
        FW_ASSETS_STRCPY(str, CategoryNames, gCatOrder[next]);
        UI_PrintStringSmallNormal(str, 0, 0, 4);
    }

    sprintf(str, "%02u/%02u", 1 + cur, count);
    UI_PrintStringSmallNormal(str, 6, 0, 6);

    sprintf(str, "%02u", UI_MENU_CategoryItemCount(gCatOrder[cur]));
    UI_PrintString(str, x1, x2, 1, 8);
    UI_PrintStringSmallNormal("items", x1, x2, 5);

    ST7565_BlitFullScreen();
}
#endif

// Construit la "vue" courante = table position affichee -> index MenuList.
// Unique endroit qui fixe gMenuListCount + gMenuIndices.
// CAT_ALL (defaut) = liste plate, identite -> ordre/numeros d'origine preserves.
void UI_MENU_BuildView(void)
{
    gMenuListCount = 0;

#ifdef ENABLE_FEAT_F4HWN_MENU_CAT
    if (gMenuCategory != CAT_ALL)
    {
        const cat_list_t *cl = &CategoryLists[gMenuCategory];
        for (uint8_t k = 0; k < cl->len; k++)
        {
            uint8_t idx = menu_find_idx(cl->ids[k]);
            if (idx != 0xFF)
                gMenuIndices[gMenuListCount++] = idx;
        }
        return;
    }
#endif

    for (uint8_t i = 0; MenuListId[i] != 0xFF; i++)
    {
        if (!gF_LOCK && MenuListId[i] == FIRST_HIDDEN_MENU_ITEM)
            break;

        gMenuIndices[gMenuListCount++] = i;
    }
}

int32_t gSubMenuSelection;

// edit box
char    edit_original[17]; // a copy of the text before editing so that we can easily test for changes/difference
char    edit[17];
int     edit_index;
bool    edit_is_uppercase = false;

static void UI_MENU_DrawTopRightRoundedBadge(const char *text, const uint8_t line, const bool center_in_area, const uint8_t area_x1, const uint8_t area_x2)
{
    const size_t length = strlen(text);
    const size_t char_pitch = FONT_SMALL_WIDTH + 1u;
    const size_t text_width = length * char_pitch;
    const size_t capsule_span = text_width + 1u; // matches UI_PrintStringSmallNormalInverse x_end computation
    uint8_t text_x;

    if (length == 0 || line == 0 || line >= FRAME_LINES) {
        return;
    }

    if (center_in_area && area_x2 > area_x1 + 2u) {
        const uint8_t min_x = area_x1 + 1u;
        uint8_t max_x;
        const uint8_t area_width = area_x2 - area_x1 + 1u;

        if (capsule_span >= area_width) {
            text_x = min_x;
        } else {
            text_x = (uint8_t)(area_x1 + ((area_width - capsule_span) / 2u));
        }

        if (area_x2 > capsule_span) {
            max_x = (uint8_t)(area_x2 - capsule_span);
        } else {
            max_x = min_x;
        }

        if (max_x < min_x) {
            max_x = min_x;
        }
        if (text_x < min_x) {
            text_x = min_x;
        } else if (text_x > max_x) {
            text_x = max_x;
        }
    } else {
        if (capsule_span >= (LCD_WIDTH - 3u)) {
            text_x = 1u;
        } else {
            const uint8_t global_shift_right = 1u;
            const uint8_t base_text_x = (uint8_t)(LCD_WIDTH - capsule_span - 3u);
            const uint8_t max_text_x  = (uint8_t)(LCD_WIDTH - capsule_span - 1u);
            const uint16_t shifted_x = (uint16_t)base_text_x + global_shift_right;

            if (shifted_x > max_text_x) {
                text_x = max_text_x;
            } else {
                text_x = (uint8_t)shifted_x;
            }
        }
    }

    UI_PrintStringSmallNormalInverse(text, text_x, 0, line);
}

/* Draw `text` (3x5 font) centred inside a fixed-width rounded inverse capsule.
 * `y` is the text's pixel position; the capsule extends one pixel above it.
 * This uses the same shape as GUI_DisplaySmallestInverse, but may cross a
 * framebuffer page boundary and its width is independent of the text length. */
static void UI_MENU_DrawFixedCapsule(const char *text, uint8_t cap_left,
                                     uint8_t cap_w, uint8_t y)
{
    const uint8_t cap_right = (uint8_t)(cap_left + cap_w - 1u);
    const uint8_t text_w    = (uint8_t)(strlen(text) * 4u - 1u); /* 3x5 glyphs: 4 px/char, last one 3 px wide */
    const uint8_t tx        = (uint8_t)(cap_left + (cap_w - text_w) / 2u);
    const uint8_t page      = (uint8_t)((y - 1u) >> 3);
    const uint8_t shift     = (uint8_t)((y - 1u) & 7u);
    const uint16_t edge     = (uint16_t)0x3Eu << shift;
    const uint16_t body     = (uint16_t)0x7Fu << shift;

    GUI_DisplaySmallest(text, tx, y, false, true);

    gFrameBuffer[page][cap_left] ^= (uint8_t)edge;
    for (uint8_t x = (uint8_t)(cap_left + 1u); x < cap_right; x++)
        gFrameBuffer[page][x] ^= (uint8_t)body;
    gFrameBuffer[page][cap_right] ^= (uint8_t)edge;

    if ((body >> 8) != 0u && page + 1u < FRAME_LINES)
    {
        gFrameBuffer[page + 1u][cap_left] ^= (uint8_t)(edge >> 8);
        for (uint8_t x = (uint8_t)(cap_left + 1u); x < cap_right; x++)
            gFrameBuffer[page + 1u][x] ^= (uint8_t)(body >> 8);
        gFrameBuffer[page + 1u][cap_right] ^= (uint8_t)(edge >> 8);
    }
}

static void UI_MENU_DrawScanMixSummary(const uint8_t area_x1,
                                       const uint8_t area_x2)
{
    const uint8_t label_w = 35u;
    const uint8_t count_w = 23u;
    const uint8_t gap = 4u;
    const uint8_t pair_w = label_w + gap + count_w;
    const uint8_t label_x = (uint8_t)(area_x1 +
                                      ((area_x2 - area_x1 + 1u - pair_w) / 2u));
    uint32_t mask = gEeprom.SCAN_LIST_MIX_MASK & SCAN_LIST_MIX_MASK_ALL;
    uint8_t selected = 0;
    char count[6];

    while (mask != 0u) {
        selected += (uint8_t)(mask & 1u);
        mask >>= 1;
    }

    sprintf(count, "%02u/%02u", (unsigned)selected,
            (unsigned)MR_CHANNELS_LIST);
    UI_MENU_DrawFixedCapsule("SELECTED", label_x, label_w, 6u * 8u + 1u);
    UI_MENU_DrawFixedCapsule(count, (uint8_t)(label_x + label_w + gap),
                             count_w, 6u * 8u + 1u);
}

static void UI_MENU_DrawScanMixEditor(void)
{
    char text[9];

    // Keep the cursor centred, with two neighbouring lists above and below.
    for (int8_t row = -2; row <= 2; row++) {
        const uint8_t index = (uint8_t)((gScanMixEditorCursor + row +
                                        MR_CHANNELS_LIST) % MR_CHANNELS_LIST);
        const uint8_t line = (uint8_t)(3 + row);
        const char *name = gListName[index];

        if (IsEmptyName(name, sizeof(gListName[0])))
            sprintf(text, "%02u", (unsigned)(index + 1));
        else
            sprintf(text, "%02u (%.3s)", (unsigned)(index + 1), name);

        // The longest label ends at x=109; the compact ON capsule stays right-aligned.
        if (row == 0) {
            UI_PrintStringSmallBold(text, 54, 0, line);
        } else {
            UI_PrintStringSmallNormal(text, 54, 0, line);
        }

        if (gScanMixEditorMask & (1u << index))
            UI_MENU_DrawFixedCapsule("ON", 115, 11, (uint8_t)(line * 8u + 1u));
    }
}

// Name of MenuList entry `index`, read into `buffer` (MENU_NAME_SIZE bytes).
static const char *UI_MENU_Name(uint8_t index, char *buffer)
{
    FW_ASSETS_STRCPY(buffer, MenuListName, index);
    return buffer;
}

void UI_DisplayMenu(void)
{
    const unsigned int menu_list_width = 6; // max no. of characters on the menu list (left side)
    const unsigned int menu_item_x1    = (8 * menu_list_width) + 2;
    const unsigned int menu_item_x2    = LCD_WIDTH - 1;
    unsigned int       i;
    char               String[64];  // bigger cuz we can now do multi-line in one string (use '\n' char)
    char               menuName[MENU_NAME_SIZE];
    char               top_right_badge[16];
    uint8_t            top_right_badge_line = 1;

#ifdef ENABLE_FEAT_F4HWN_MENU_CAT
    // The status bar shows the menu level / category capsule: refresh it.
    gUpdateStatus = true;

    if (gMenuLevel == MENU_LEVEL_CAT)
    {
        UI_MENU_DrawCategories();
        return;
    }
#endif

    const int m = UI_MENU_GetCurrentMenuId();

#ifdef ENABLE_DTMF_CALLING
    char               Contact[16];
#endif

    UI_DisplayClear();

#ifdef ENABLE_FEAT_F4HWN
    UI_DrawLineBuffer(gFrameBuffer, 48, 0, 48, 55, 1); // Be ware, status zone = 8 lines, the rest = 56 ->total 64
    //UI_DrawLineDottedBuffer(gFrameBuffer, 0, 46, 50, 46, 1);

    for (uint8_t i = 0; i < 48; i += 2)
    {
        gFrameBuffer[5][i] = 0x40;
    }
#endif

#ifndef ENABLE_CUSTOM_MENU_LAYOUT
        // original menu layout
    for (i = 0; i < 3; i++)
        if (gMenuCursor > 0 || i > 0)
            if ((gMenuListCount - 1) != gMenuCursor || i != 2)
                UI_PrintString(UI_MENU_Name(gMenuIndices[gMenuCursor + i - 1], menuName), 0, 0, i * 2, 8);

    // invert the current menu list item pixels
    for (i = 0; i < (8 * menu_list_width); i++)
    {
        gFrameBuffer[2][i] ^= 0xFF;
        gFrameBuffer[3][i] ^= 0xFF;
    }

    // draw vertical separating dotted line
    for (i = 0; i < 7; i++)
        gFrameBuffer[i][(8 * menu_list_width) + 1] = 0xAA;

    // draw the little sub-menu triangle marker
    if (gIsInSubMenu)
        FW_ASSETS_COPY(gFrameBuffer[0] + (8 * menu_list_width) + 1, BITMAP_CurrentIndicator);

    // draw the menu index number/count
    sprintf(String, "%2u.%u", 1 + gMenuCursor, gMenuListCount);

    UI_PrintStringSmallNormal(String, 2, 0, 6);

#else
    {   // new menu layout .. experimental & unfinished
        const int menu_index = gMenuCursor;  // current selected menu item
        const int menu_count = (int)gMenuListCount;

        if (menu_index >= 0 && menu_index < menu_count) 
        {
            if (!gIsInSubMenu) 
            {
                // leading menu items - small text
                int prev_index = menu_index - 1;
                if (prev_index < 0) {
                    prev_index = menu_count - 1;
                }
                UI_PrintStringSmallNormal(UI_MENU_Name(gMenuIndices[prev_index], menuName), 0, 0, 1);

                // current menu item - keep big n fat
                UI_PrintString(UI_MENU_Name(gMenuIndices[menu_index], menuName), 0, 0, 2, 8);

                // trailing menu item - small text
                int next_index = menu_index + 1;
                if (next_index >= menu_count) {
                    next_index = 0;
                }
                UI_PrintStringSmallNormal(UI_MENU_Name(gMenuIndices[next_index], menuName), 0, 0, 4);


                // draw the menu index number/count
    #ifndef ENABLE_FEAT_F4HWN
                sprintf(String, "%2u.%u", 1 + menu_index, menu_count);
                UI_PrintStringSmallNormal(String, 2, 0, 6);
    #endif
            }
            else
            {   
                // current menu item
//              strcat(String, ":");
                UI_PrintString(UI_MENU_Name(gMenuIndices[menu_index], menuName), 0, 0, 0, 8);
//              UI_PrintStringSmallNormal(String, 0, 0, 0);
            }

    #ifdef ENABLE_FEAT_F4HWN
            sprintf(String, "%02u/%02u", 1 + menu_index, menu_count);
            UI_PrintStringSmallNormal(String, 6, 0, 6);
    #endif
        }
    }
#endif

    // **************

    String[0] = '\0';
    top_right_badge[0] = '\0';

    bool already_printed = false;
    // Fixed-width asset table of the choice to show: first entry + entry size.
    const char *choiceTable;
    uint8_t     choiceSize;
#define MENU_SET_CHOICES(table) \
    do { choiceTable = (table)[0]; choiceSize = sizeof((table)[0]); } while (0)

    /* Brightness is set to max in some entries of this menu. Return it to the configured brightness
       level the "next" time we enter here.I.e., when we move from one menu to another.
       It also has to be set back to max when pressing the Exit key. */

    BACKLIGHT_TurnOn();

    if (gScanMixEditorActive)
    {
        UI_MENU_DrawScanMixEditor();
        ST7565_BlitFullScreen();
        return;
    }

    //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
        uint8_t gaugeLine = 0;
        uint8_t gaugeMin = 0;
        uint8_t gaugeMax = 0;
    //#endif

    switch (m)
    {
        case MENU_SQL:
            sprintf(String, "%d", gSubMenuSelection);
            break;

        case MENU_MIC:
            {   // display the mic gain in actual dB rather than just an index number
                const uint8_t mic = gMicGain_dB2[gSubMenuSelection];
                sprintf(String, "+%u.%udB", mic / 2, (mic % 2) * 5);

                gaugeLine = 4;
                gaugeMin = 0;
                gaugeMax = 8;
            }
            break;

        case MENU_MIC_BAR:
            #ifdef ENABLE_AUDIO_BAR
                FW_ASSETS_STRCPY(String, gSubMenu_OFF_ON, gSubMenuSelection);
            #else
                strcpy(String, gSubMenu_NA);
            #endif
            break;

        case MENU_STEP: {
            uint16_t step = gStepFrequencyTable[FREQUENCY_GetStepIdxFromSortedIdx(gSubMenuSelection)];
            sprintf(String, "%d.%02ukHz", step / 100, step % 100);
            break;
        }

        case MENU_TXP:
#ifdef ENABLE_FEAT_F4HWN
        case MENU_SET_PWR:
#endif
        {
            int32_t power = gSubMenuSelection;
#ifdef ENABLE_FEAT_F4HWN
            if (m == MENU_SET_PWR)
                power++;
#endif
            if (power == 0)
            {
                MENU_SET_CHOICES(gSubMenu_TXP);
                goto copy_menu_choice;
            }
            {
                char level[sizeof(gSubMenu_TXP[0])];
                char watts[sizeof(gSubMenu_SET_PWR[0])];

                FW_ASSETS_STRCPY(level, gSubMenu_TXP, power);
                FW_ASSETS_STRCPY(watts, gSubMenu_SET_PWR, power - 1);
                sprintf(String, "%s\n%sW", level, watts);
            }
            break;
        }

        case MENU_R_DCS:
        case MENU_T_DCS:
            if (gSubMenuSelection == 0)
                FW_ASSETS_STRCPY(String, gSubMenu_OFF_ON, 0);
            else
            {
                const bool inverted = gSubMenuSelection >= 105;
                const unsigned index = gSubMenuSelection - (inverted ? 105 : 1);
                sprintf(String, inverted ? "D%03oI" : "D%03oN", DCS_GetOption(index));
            }
            break;

        case MENU_R_CTCS:
        case MENU_T_CTCS:
        {
            if (gSubMenuSelection == 0)
                FW_ASSETS_STRCPY(String, gSubMenu_OFF_ON, 0);
            else
                sprintf(String, "%u.%uHz", CTCSS_Options[gSubMenuSelection - 1] / 10, CTCSS_Options[gSubMenuSelection - 1] % 10);
            break;
        }

        case MENU_SFT_D:
            MENU_SET_CHOICES(gSubMenu_SFT_D);
            goto copy_menu_choice;

        case MENU_OFFSET:
            if (!gIsInSubMenu || gInputBoxIndex == 0)
            {
                sprintf(String, "%3d.%05u", gSubMenuSelection / 100000, abs(gSubMenuSelection) % 100000);
            }
            else
            {
                const char * ascii = INPUTBOX_GetAscii();
                sprintf(String, "%.3s.%.3s  ",ascii, ascii + 3);
            }

            UI_PrintString(String, menu_item_x1, menu_item_x2, 1, 8);
            UI_PrintString("MHz",  menu_item_x1, menu_item_x2, 3, 8);

            already_printed = true;
            break;

        case MENU_W_N:
            MENU_SET_CHOICES(gSubMenu_W_N);
            goto copy_menu_choice;

#ifndef ENABLE_FEAT_F4HWN
        case MENU_SCR:
            FW_ASSETS_STRCPY(String, gSubMenu_SCRAMBLER, gSubMenuSelection);
            #if 1
                if (gSubMenuSelection > 0 && gSetting_ScrambleEnable)
                    BK4819_EnableScramble(gSubMenuSelection - 1);
                else
                    BK4819_DisableScramble();
            #endif
            break;
#endif

        case MENU_VOX:
            #ifdef ENABLE_VOX
                if (gSubMenuSelection == 0)
                    FW_ASSETS_STRCPY(String, gSubMenu_OFF_ON, 0);
                else
                    sprintf(String, "%u", gSubMenuSelection);
            #else
                strcpy(String, gSubMenu_NA);
            #endif
            break;

        case MENU_ABR:
        case MENU_AUTOLK:
        case MENU_TOT:
        {
            if (m != MENU_TOT && gSubMenuSelection == 0)
            {
                FW_ASSETS_STRCPY(String, gSubMenu_OFF_ON, 0);
                break;
            }
            if (m == MENU_ABR && gSubMenuSelection >= 61)
            {
                strcpy(String, "ON");
                break;
            }

            int32_t seconds = gSubMenuSelection * 5;
            gaugeLine = 4;
            gaugeMin = 1;
            gaugeMax = 60;
            if (m == MENU_AUTOLK)
            {
                seconds *= 3;
                gaugeMax = 40;
            }
            else if (m == MENU_TOT)
            {
                seconds += 5;
                gaugeMin = 5;
                gaugeMax = 179;
            }
            sprintf(String, "%02dm:%02ds", seconds / 60, seconds % 60);
            break;
        }

        case MENU_ABR_MIN:
        case MENU_ABR_MAX:
            sprintf(String, "%d", gSubMenuSelection);
            if(gIsInSubMenu)
                BACKLIGHT_SetBrightness(gSubMenuSelection);
            // Obsolete ???
            //else if(BACKLIGHT_GetBrightness() < 4)
            //    BACKLIGHT_SetBrightness(4);
            break;

        case MENU_AM:
            strcpy(String, gModulationStr[gSubMenuSelection]);
            break;

        case MENU_COMPAND:
        case MENU_ABR_ON_TX_RX:
            MENU_SET_CHOICES(gSubMenu_RX_TX);
            goto copy_menu_choice;

        case MENU_BCL:
        case MENU_BEEP:
        case MENU_STE:
        case MENU_D_ST:
#ifdef ENABLE_DTMF_CALLING
        case MENU_D_DCD:
#endif
        case MENU_D_LIVE_DEC:
        #ifdef ENABLE_NOAA
            case MENU_NOAA_S:
        #endif
#ifndef ENABLE_FEAT_F4HWN
        case MENU_350TX:
        case MENU_200TX:
        case MENU_500TX:
#endif
        case MENU_350EN:
#ifndef ENABLE_FEAT_F4HWN
        case MENU_SCREN:
#endif
#ifdef ENABLE_FEAT_F4HWN
        case MENU_SET_TMR:
        case MENU_S_PRI:
#endif
            MENU_SET_CHOICES(gSubMenu_OFF_ON);
            goto copy_menu_choice;

#if defined(ENABLE_FEAT_F4HWN) && defined(ENABLE_FEAT_F4HWN_LOGO_SAV)
        case MENU_SET_SAV:
            MENU_SET_CHOICES(gSubMenu_SET_SAV);
            goto copy_menu_choice;
#endif

        case MENU_MEM_CH:
        case MENU_1_CALL:
        case MENU_DEL_CH:
        case MENU_S_PRI_CH_1:
        case MENU_S_PRI_CH_2:
        {
            if(gSubMenuSelection == MR_CHANNELS_MAX)
            {
                UI_PrintString("None", menu_item_x1, menu_item_x2, 2, 8);
                already_printed = true;
                break;
            }
            else
            {
                const bool valid = RADIO_CheckValidChannel(gSubMenuSelection, false, 0);

                UI_GenerateChannelStringEx(String, valid, gSubMenuSelection);
                UI_PrintString(String, menu_item_x1, menu_item_x2, 0, 8);

                if (valid && !gAskForConfirmation)
                {   // show the frequency so that the user knows the channels frequency
                    const uint32_t frequency = SETTINGS_FetchChannelFrequency(gSubMenuSelection);
                    sprintf(String, "%u.%05u", frequency / 100000, frequency % 100000);
                    UI_PrintString(String, menu_item_x1, menu_item_x2, 5, 8);
                }

                SETTINGS_FetchChannelName(String, gSubMenuSelection);
                UI_PrintString(String[0] ? String : "--", menu_item_x1, menu_item_x2, 2, 8);
                already_printed = true;
                break;
            }
        }

        case MENU_MEM_NAME:
        {
            const bool valid = RADIO_CheckValidChannel(gSubMenuSelection, false, 0);

            UI_GenerateChannelStringEx(String, valid, gSubMenuSelection);
            UI_PrintString(String, menu_item_x1, menu_item_x2, 0, 8);

            if (valid)
            {
                const uint32_t frequency = SETTINGS_FetchChannelFrequency(gSubMenuSelection);

                //if (!gIsInSubMenu || edit_index < 0)
                if (!gIsInSubMenu)
                    edit_index = -1;
                if (edit_index < 0)
                {   // show the channel name
                    SETTINGS_FetchChannelName(String, gSubMenuSelection);
                    char *pPrintStr = String[0] ? String : "--";
                    UI_PrintString(pPrintStr, menu_item_x1, menu_item_x2, 2, 8);
                }
                else
                {   // show the channel name being edited
                    //UI_PrintString(edit, menu_item_x1, 0, 2, 8);
                    UI_PrintString(edit, menu_item_x1, menu_item_x2, 2, 8);
                    if (edit_index < 10) {
                        // UI_PrintString("^", menu_item_x1 - 1 + (8 * edit_index),0, 4, 8); // show the cursor
                        uint8_t x = menu_item_x1 - 1;
                        for (uint8_t i = 0; i < 10; i++) 
                        {
                            if (i != edit_index) 
                            {
                                if (edit[i] != 'g' && edit[i] != 'j')
                                {
                                    UI_DrawLineBuffer(gFrameBuffer, x, 29, x + 6, 29, 1);
                                }
                            }
                            else 
                            {
                                UI_DrawLineBuffer(gFrameBuffer, x + 2, 30, x + 4, 30, 1);
                                UI_DrawPixelBuffer(gFrameBuffer, x + 3, 29, 1);
                            }
                            x += 8;
                        }
                        
                        UI_PrintStringSmallNormal(edit_is_uppercase ? "ABC" : "abc", 77, 0, 4);
                    }
                }

                if (!gAskForConfirmation)
                {   // show the frequency so that the user knows the channels frequency
                    sprintf(String, "%u.%05u", frequency / 100000, frequency % 100000);
                    UI_PrintString(String, menu_item_x1, menu_item_x2, 5, 8);
                }
            }

            already_printed = true;
            break;
        }

        case MENU_SAVE:
            if (gSubMenuSelection == 0)
                FW_ASSETS_STRCPY(String, gSubMenu_OFF_ON, 0);
            else
                sprintf(String, "1:%u", gSubMenuSelection);
            break;

        case MENU_TDR:
            MENU_SET_CHOICES(gSubMenu_RXMode);
            goto copy_menu_choice;

        #ifdef ENABLE_VOICE
            case MENU_VOICE:
                FW_ASSETS_STRCPY(String, gSubMenu_VOICE, gSubMenuSelection);
                break;
        #endif

        case MENU_SC_REV:
            if(gSubMenuSelection == 0)
            {
                strcpy(String, "STOP");
            }
            else if(gSubMenuSelection < 81)
            {
                sprintf(String, "CARRIER\n%02ds:%03dms", ((gSubMenuSelection * 250) / 1000), ((gSubMenuSelection * 250) % 1000));
                //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
                //ST7565_Gauge(5, 1, 80, gSubMenuSelection);
                gaugeLine = 5;
                gaugeMin = 1;
                gaugeMax = 80;
                //#endif
            }
            else
            {
                sprintf(String, "TIMEOUT\n%02dm:%02ds", (((gSubMenuSelection - 80) * 5) / 60), (((gSubMenuSelection - 80) * 5) % 60));
                //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
                //ST7565_Gauge(5, 80, 104, gSubMenuSelection);
                gaugeLine = 5;
                gaugeMin = 80;
                gaugeMax = 104;
                //#endif
            }
            break;

        case MENU_MDF:
            MENU_SET_CHOICES(gSubMenu_MDF);
            goto copy_menu_choice;

        case MENU_RP_STE:
            if (gSubMenuSelection == 0)
                FW_ASSETS_STRCPY(String, gSubMenu_OFF_ON, 0);
            else
                sprintf(String, "%u*100ms", gSubMenuSelection);
            break;

        case MENU_LIST_CH:
        case MENU_S_LIST:
            if (gSubMenuSelection == SCAN_LIST_MODE_MIX && m == MENU_S_LIST)
                strcpy(String, "MIX");
            else if (gSubMenuSelection == SCAN_LIST_MODE_ALL)
                strcpy(String, "ALL");
            else if (gSubMenuSelection == 0 && m == MENU_LIST_CH)
                strcpy(String, "OFF");
            else {
                const char *name = gListName[gSubMenuSelection - 1];
                
                // If first character is empty/invalid, display "N/A"
                if (IsEmptyName(name, sizeof(gListName[0])))
                    sprintf(String, "%02u", gSubMenuSelection);
                else
                    sprintf(String, "%02u (%.3s)", gSubMenuSelection, name);
            }
            break;
            
#ifdef ENABLE_DTMF_CALLING
        case MENU_ANI_ID:
            strcpy(String, gEeprom.ANI_DTMF_ID);
            break;
#endif
        case MENU_UPCODE:
        case MENU_DWCODE:
        {
            const char *code = m == MENU_UPCODE ? gEeprom.DTMF_UP_CODE : gEeprom.DTMF_DOWN_CODE;
            if (code[8] != '\0' && code[8] != 0xFF) {
                sprintf(String, "%.8s\n%.8s", code, code + 8);
            } else {
                sprintf(String, "%.8s", code);
            }
            break;
        }

#ifdef ENABLE_DTMF_CALLING
        case MENU_D_RSP:
            MENU_SET_CHOICES(gSubMenu_D_RSP);
            goto copy_menu_choice;

        case MENU_D_HOLD:
            sprintf(String, "%ds", gSubMenuSelection);
            break;
#endif
        case MENU_D_PRE:
            sprintf(String, "%d*10ms", gSubMenuSelection);
            break;

        case MENU_PTT_ID:
            MENU_SET_CHOICES(gSubMenu_PTT_ID);
            goto copy_menu_choice;

        case MENU_BAT_TXT:
            MENU_SET_CHOICES(gSubMenu_BAT_TXT);
            goto copy_menu_choice;

#ifdef ENABLE_DTMF_CALLING
        case MENU_D_LIST:
            gIsDtmfContactValid = DTMF_GetContact((int)gSubMenuSelection - 1, Contact);
            if (!gIsDtmfContactValid)
                strcpy(String, "NULL");
            else
                memcpy(String, Contact, 8);
            break;
#endif

        case MENU_PONMSG:
            MENU_SET_CHOICES(gSubMenu_PONMSG);
            goto copy_menu_choice;

        case MENU_ROGER:
            MENU_SET_CHOICES(gSubMenu_ROGER);
            goto copy_menu_choice;

        case MENU_VOL: {
            // SysInf is paginated. Pages appear in this order, only when their
            // feature flag is enabled:
            //   0          -> identity
            //   next       -> Build date/time         (ENABLE_FEAT_F4HWN)
            //   next       -> Battery                 (ENABLE_FEAT_F4HWN)
            //   next       -> Flash / SRAM usage      (ENABLE_FEAT_F4HWN_MEM)
            //   next, +1   -> CODE / WIKI QR codes    (ENABLE_FEAT_F4HWN_QRCODE)
            // In non-F4HWN builds, page 0 keeps the old battery-voltage display.
            const uint8_t page = (uint8_t)gSubMenuSelection;
            uint8_t       p    = 0;

            if (page == p++) {
                // Page 0: firmware identity.
#ifdef ENABLE_FEAT_F4HWN
                sprintf(String, "%s\n%s", AUTHOR_STRING_2, DISPLAY_VERSION_STRING_2);
                UI_PrintStringSmallNormal(Edition, menu_item_x1 - 1, menu_item_x2, 6);
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
                /* Two 3x5 inverse-capsule labels on one line (scan-list "label"
                 * style): the running firmware slot (M = Main) and the active
                 * config bank. They match unless SetCfg has pointed the bank at a
                 * different bank (e.g. SLOT 2 / CFG 4). */
                const uint8_t fw_slot = MB_GetRunningSlot();
                const uint8_t bank    = MB_GetActiveBank();
                char slot_lbl[8];
                char cfg_lbl[8];

                /* Only the last glyph varies (M / digit / ?), so poke it in place
                 * instead of pulling sprintf for a single character. */
                strcpy(slot_lbl, "SLOT ?");
                if (fw_slot == 0u)
                    slot_lbl[5] = 'M';
                else if (fw_slot < MB_SLOT_COUNT)
                    slot_lbl[5] = (char)('0' + fw_slot);
                strcpy(cfg_lbl, "CFG M");            /* bank 0 = base config, like SLOT M */
                if (bank != 0u)
                    cfg_lbl[4] = (char)('0' + bank);

                /* Both capsules share the wider label's width (6-char "SLOT x" ->
                 * 4*6+3 = 27 px); the shorter CFG text is centred inside its own.
                 * The two are drawn as one centred pair with a small gap, centred in
                 * the space between the separator bar (x=48) and the right screen
                 * edge, so they line up with the centred identity lines above. */
                const uint8_t cap_w     = (uint8_t)(4u * 6u + 3u);                     /* 27 */
                const uint8_t cap_gap   = 4u;
                const uint8_t pair_w    = (uint8_t)(2u * cap_w + cap_gap);             /* 58 */
                const uint8_t slot_left = (uint8_t)((48u + LCD_WIDTH - pair_w) / 2u);  /* 59 */
                const uint8_t cfg_left  = (uint8_t)(slot_left + cap_w + cap_gap);      /* 90 */

                UI_MENU_DrawFixedCapsule(slot_lbl, slot_left, cap_w, 5u * 8u + 1u);
                UI_MENU_DrawFixedCapsule(cfg_lbl,  cfg_left,  cap_w, 5u * 8u + 1u);
#endif
#else
                sprintf(String, "%u.%02uV\n%u%%",
                    gBatteryVoltageAverage / 100, gBatteryVoltageAverage % 100,
                    BATTERY_VoltsToPercent(gBatteryVoltageAverage));
#endif
                break;
            }
#ifdef ENABLE_FEAT_F4HWN
            if (page == p++) {
                strcpy(top_right_badge, "BUILD");
                UI_PrintStringSmallNormal(BuildDate, menu_item_x1 - 1, menu_item_x2, 3);
                UI_PrintStringSmallNormal(BuildTime, menu_item_x1 - 1, menu_item_x2, 4);
                UI_PrintStringSmallNormal(BuildCommit, menu_item_x1 - 1, menu_item_x2, 6);

                already_printed = true;
                break;
            }

            if (page == p++) {
                char val[16];

                strcpy(top_right_badge, "BATTERY");

                sprintf(val, "%u.%02uV %u%%",
                    gBatteryVoltageAverage / 100, gBatteryVoltageAverage % 100,
                    BATTERY_VoltsToPercent(gBatteryVoltageAverage));
                UI_PrintStringSmallNormal(val, menu_item_x1 - 1, menu_item_x2, 3);

                FW_ASSETS_STRCPY(val, gSubMenu_BATTYP, gEeprom.BATTERY_TYPE);
                UI_PrintStringSmallNormal(val, menu_item_x1 - 1, menu_item_x2, 5);

                already_printed = true;
                break;
            }
#endif
#ifdef ENABLE_FEAT_F4HWN_MEM
            if (page == p++) {
                uint16_t flash_pct = 0;
                uint16_t ram_pct   = 0;
                UI_GetMemPercents(&flash_pct, &ram_pct);

                char val[16];

                // MEMORY title capsule centered in right zone, fb line 1.
                strcpy(top_right_badge, "MEMORY");

                // Flash + SRAM values stacked below, normal small font, with a fb-line of breathing space.
                sprintf(val, "FLASH %u.%u%%",
                        (unsigned)(flash_pct / 100), (unsigned)((flash_pct / 10) % 10));
                UI_PrintStringSmallNormal(val, menu_item_x1 - 1, menu_item_x2, 3);

                sprintf(val, "SRAM  %u.%u%%",
                        (unsigned)(ram_pct / 100), (unsigned)((ram_pct / 10) % 10));
                UI_PrintStringSmallNormal(val, menu_item_x1 - 1, menu_item_x2, 5);

                already_printed = true;
                break;
            }
#endif
#ifdef ENABLE_FEAT_F4HWN_QRCODE
            // Right zone: x=49..127 (79 px). QR centered at x=72..104.
            // Capsule label above QR (small-font Inverse style at fb line 1).
            if (page == p || page == p + 1) {
                const bool is_wiki = (page == (p + 1));

                strcpy(top_right_badge, is_wiki ? "WIKI" : "CODE");
                UI_DrawQRCode(is_wiki, 72, 28);
                
                already_printed = true;
                break;
            }

            p += 2; 
#endif
            break;
        }

        case MENU_RESET:
            MENU_SET_CHOICES(gSubMenu_RESET);
            goto copy_menu_choice;

        case MENU_F_LOCK:
#ifdef ENABLE_FEAT_F4HWN
            if(!gIsInSubMenu && gUnlockAllTxConfCnt>0 && gUnlockAllTxConfCnt<3)
#else
            if(!gIsInSubMenu && gUnlockAllTxConfCnt>0 && gUnlockAllTxConfCnt<10)
#endif
                strcpy(String, "READ\nMANUAL");
            else
                FW_ASSETS_STRCPY(String, gSubMenu_F_LOCK, gSubMenuSelection);
            break;

        #ifdef ENABLE_F_CAL_MENU
            case MENU_F_CALI:
                {
                    const uint32_t value   = 22656 + gSubMenuSelection;
                    const uint32_t xtal_Hz = (0x4f0000u + value) * 5;

                    writeXtalFreqCal(gSubMenuSelection, false);

                    sprintf(String, "%d\n%u.%06u\nMHz",
                        gSubMenuSelection,
                        xtal_Hz / 1000000, xtal_Hz % 1000000);
                }
                break;
        #endif

        case MENU_BATCAL:
        {
            const uint16_t vol = (uint32_t)gBatteryVoltageAverage * gBatteryCalibration[3] / gSubMenuSelection;
            sprintf(String, "%u.%02uV\n%u", vol / 100, vol % 100, gSubMenuSelection);
            break;
        }

        case MENU_BATTYP:
            MENU_SET_CHOICES(gSubMenu_BATTYP);
            goto copy_menu_choice;

        case MENU_SET_NAV:
            MENU_SET_CHOICES(gSubMenu_SET_NAV);
            goto copy_menu_choice;

#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
        case MENU_SET_CFG:
            strcpy(String, "CFG M");         /* bank 0 = base config, like SysInfo */
            if (gSubMenuSelection != 0)
                String[4] = (char)('0' + gSubMenuSelection);
            break;
#endif

        case MENU_F1SHRT:
        case MENU_F1LONG:
        case MENU_F2SHRT:
        case MENU_F2LONG:
        case MENU_MLONG:
        {
            const uint8_t action = gSubMenuSelection;
            FW_ASSETS_STRCPY(String, gSubMenu_SIDEFUNCTIONS, gSubMenuSelection);
            if (!ACTION_IsAvailable(action)) {
                strcpy(top_right_badge, "N/A");
                top_right_badge_line = 5;
            }
            break;
        }

#ifdef ENABLE_FEAT_F4HWN_SLEEP
        case MENU_SET_OFF:
            if(gSubMenuSelection == 0)
            {
                FW_ASSETS_STRCPY(String, gSubMenu_OFF_ON, 0);
            }
            else if(gSubMenuSelection < 121)
            {
                sprintf(String, "%dh:%02dm", (gSubMenuSelection / 60), (gSubMenuSelection % 60));
                //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
                //ST7565_Gauge(4, 1, 120, gSubMenuSelection);
                gaugeLine = 4;
                gaugeMin = 1;
                gaugeMax = 120;
                //#endif
            }
            break;
#endif

#ifdef ENABLE_FEAT_F4HWN
        case MENU_SET_PTT:
            MENU_SET_CHOICES(gSubMenu_SET_PTT);
            goto copy_menu_choice;

        case MENU_SET_TOT:
        case MENU_SET_EOT:
            MENU_SET_CHOICES(gSubMenu_SET_TOT);
            goto copy_menu_choice;

        case MENU_SET_CTR:
            #ifdef ENABLE_FEAT_F4HWN_CTR
                sprintf(String, "%d", gSubMenuSelection);
                gSetting_set_ctr = gSubMenuSelection;
                ST7565_ContrastAndInv();
            #else
                strcpy(String, gSubMenu_NA);
            #endif
            break;

        case MENU_SET_INV:
            #ifdef ENABLE_FEAT_F4HWN_INV
                FW_ASSETS_STRCPY(String, gSubMenu_OFF_ON, gSubMenuSelection);
                ST7565_ContrastAndInv();
            #else
                strcpy(String, gSubMenu_NA);
            #endif
            break;

        case MENU_TX_LOCK:
            if(TX_freq_check(gEeprom.VfoInfo[gEeprom.TX_VFO].pTX->Frequency) == 0)
            {
                strcpy(String, "Inside\nF Lock\nPlan");
            }
            else
            {
                FW_ASSETS_STRCPY(String, gSubMenu_OFF_ON, gSubMenuSelection);
            }
            break;

        case MENU_SET_LCK:
            MENU_SET_CHOICES(gSubMenu_SET_LCK);
            goto copy_menu_choice;

        case MENU_SET_MET:
        case MENU_SET_GUI:
            MENU_SET_CHOICES(gSubMenu_SET_MET);
            goto copy_menu_choice;

        #ifdef ENABLE_FEAT_F4HWN_SET_FNT
            case MENU_SET_FNT:
                sprintf(String, "Font %u", (unsigned)gSubMenuSelection + 1u);
                UI_PrintStringWithFont(String, menu_item_x1, menu_item_x2, 0, 8,
                                       (uint8_t)gSubMenuSelection);
                already_printed = true;
                break;
        #endif

        #ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
            case MENU_SET_SCN:
                MENU_SET_CHOICES(gSubMenu_SET_SCN);
                goto copy_menu_choice;
        #endif

        #ifdef ENABLE_FEAT_F4HWN_AUDIO
            case MENU_SET_AUD:
                if(gTxVfo->Modulation == MODULATION_AM) {
                    FW_ASSETS_STRCPY(String, gSubMenu_SET_AUD_AM, gSubMenuSelection);
                    strcpy(top_right_badge, "AM");
                }
                else if (gTxVfo->Modulation == MODULATION_USB) {
                    strcpy(String, "USB");
                    strcpy(top_right_badge, "USB");
                }
                else {
                    FW_ASSETS_STRCPY(String, gSubMenu_SET_AUD_FM, gSubMenuSelection);
                    strcpy(top_right_badge, "FM");
                }
                break;

            case MENU_SET_RXG:
                FW_ASSETS_STRCPY(String, gSubMenu_OFF_ON, gSubMenuSelection);
                if (RADIO_IsRxCompanderOn(gTxVfo)) {   // forced ON by the RX expander
                    strcpy(top_right_badge, "COMPND");
                    top_right_badge_line = 5;
                }
                break;
        #endif

        #ifdef ENABLE_FEAT_F4HWN_NARROWER
            case MENU_SET_NFM:
                MENU_SET_CHOICES(gSubMenu_SET_NFM);
                goto copy_menu_choice;
        #endif

        #ifdef ENABLE_FEAT_F4HWN_VOL
            case MENU_SET_VOL:
                if(gSubMenuSelection == 0)
                {
                    FW_ASSETS_STRCPY(String, gSubMenu_OFF_ON, 0);
                }
                else if(gSubMenuSelection < 64)
                {
                    sprintf(String, "%02u", gSubMenuSelection);
                    //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
                    //ST7565_Gauge(4, 1, 63, gSubMenuSelection);
                    gaugeLine = 4;
                    gaugeMin = 1;
                    gaugeMax = 63;
                    //#endif
                }
                // gEeprom.VOLUME_GAIN = gSubMenuSelection;
                BK4819_SetRxAudioGain();
                break;
        #endif

        #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
            case MENU_SET_KEY:
                FW_ASSETS_STRCPY(String, gSubMenu_SET_KEY, gSubMenuSelection);
                break;                
        #endif
#endif

        copy_menu_choice:
            FW_ASSETS_ReadStr(String, choiceTable + gSubMenuSelection * choiceSize, choiceSize);
            break;
    }
#undef MENU_SET_CHOICES

    //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
    if(gaugeLine != 0)
    {
        ST7565_Gauge(gaugeLine, gaugeMin, gaugeMax, gSubMenuSelection);
    }
    //#endif

    if (!already_printed)
    {   // we now do multi-line text in a single string

        unsigned int y;
        unsigned int lines = 1;
        unsigned int len   = strlen(String);
        bool         small = false;

        if (String[0] != '\0')
        {
            // count number of lines
            for (i = 0; i < len; i++)
            {
                if (String[i] == '\n' && i < (len - 1))
                {   // found new line char
                    lines++;
                    String[i] = 0;  // null terminate the line
                }
            }

            if (lines > 3)
            {   // use small text
                small = true;
                if (lines > 7)
                    lines = 7;
            }

            // center vertically'ish
            /*
            if (small)
                y = 3 - ((lines + 0) / 2);  // untested
            else
                y = 2 - ((lines + 0) / 2);
            */

            y = (small ? 3 : 2) - (lines / 2); 

            // draw the text lines
            for (i = 0; i < len && lines > 0; lines--)
            {
                if (small)
                    UI_PrintStringSmallNormal(String + i, menu_item_x1, menu_item_x2, y);
                else
                    UI_PrintString(String + i, menu_item_x1, menu_item_x2, y, 8);

                // look for start of next line
                while (i < len && String[i] >= 32)
                    i++;

                // hop over the null term char(s)
                while (i < len && String[i] < 32)
                    i++;

                y += small ? 1 : 2;
            }
        }
    }

    if (m == MENU_S_PRI_CH_1 || m == MENU_S_PRI_CH_2)
    {

    }

    if ((m == MENU_R_CTCS || m == MENU_R_DCS) && gCssBackgroundScan)
        UI_PrintString("SCAN", menu_item_x1, menu_item_x2, 4, 8);

#ifdef ENABLE_DTMF_CALLING
    if (m == MENU_D_LIST && gIsDtmfContactValid) {
        Contact[11] = 0;
        memcpy(&gDTMF_ID, Contact + 8, 4);
        sprintf(String, "ID:%4s", gDTMF_ID);
        UI_PrintString(String, menu_item_x1, menu_item_x2, 4, 8);
    }
#endif

    const bool is_ctcs = (m == MENU_R_CTCS || m == MENU_T_CTCS);
    const bool is_dcs  = (m == MENU_R_DCS  || m == MENU_T_DCS);

    if (is_ctcs || is_dcs) {
        if (gSubMenuSelection == 0) {
            strcpy(top_right_badge, is_ctcs ? "00/00" : "000/00");
        } else {
            const uint8_t approved_index = is_ctcs ? 
                DCS_GetCtcssApprovedIndex(gSubMenuSelection - 1) : 
                DCS_GetDcsApprovedIndex(gSubMenuSelection - 1);
                
            const uint8_t width = is_ctcs ? 2 : 3;

            if (approved_index != 0xFF) {
                sprintf(top_right_badge, "%0*u/%02u", width, (unsigned)gSubMenuSelection, (unsigned)approved_index + 1);
            } else {
                sprintf(top_right_badge, "%0*u/--", width, (unsigned)gSubMenuSelection);
            }
        }
    }

#ifdef ENABLE_DTMF_CALLING
    if (m == MENU_D_LIST) {
        sprintf(top_right_badge, "%03d", gSubMenuSelection);
    }
#endif

    if (top_right_badge[0] != '\0') {
        UI_MENU_DrawTopRightRoundedBadge(top_right_badge, top_right_badge_line, true, menu_item_x1, menu_item_x2);
    }

    if (m == MENU_S_LIST &&
        gSubMenuSelection == SCAN_LIST_MODE_MIX) {
        UI_MENU_DrawScanMixSummary(menu_item_x1, menu_item_x2);
    }

    if ((m == MENU_RESET    ||
         m == MENU_MEM_CH   ||
         m == MENU_MEM_NAME ||
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
         m == MENU_SET_CFG  ||
#endif
         m == MENU_DEL_CH) && gAskForConfirmation)
    {   // display confirmation
        char *pPrintStr = (gAskForConfirmation == 1) ? "SURE?" : "WAIT!";
        UI_PrintString(pPrintStr, menu_item_x1, menu_item_x2, 5, 8);
    }

#ifdef ENABLE_FEAT_F4HWN_SET_FNT
    if (m == MENU_SET_FNT && gIsInSubMenu)
    {
        const uint8_t font = (uint8_t)gSubMenuSelection;

        UI_PrintStringSmall("ABCDabcd", menu_item_x1, menu_item_x2, 3,
                            FONT_GetSmallFont(font, false));
        UI_DisplayFrequencyWithFont("01234", 55, 4, false, font);

        FW_ASSETS_STRCPY(String, gSetFntLabels, font);
        const uint8_t badge_w = (uint8_t)(strlen(String) * 4u + 3u);
        const uint8_t badge_x = (uint8_t)(menu_item_x1 +
            (menu_item_x2 - menu_item_x1 + 1u - badge_w) / 2u);
        UI_MENU_DrawFixedCapsule(String, badge_x, badge_w, 15);
    }
#endif

    ST7565_BlitFullScreen();
}
