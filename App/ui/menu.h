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

#ifndef UI_MENU_H
#define UI_MENU_H

#include <stdbool.h>
#include <stdint.h>

#include "audio.h"     // VOICE_ID_t
#include "settings.h"

typedef struct __attribute__((packed)) {
    const char  name[7];    // menu display area only has room for 6 characters
    uint8_t     menu_id;
} t_menu_item;

enum
{
    MENU_SQL = 0,
    MENU_STEP,
    MENU_TXP,
    MENU_R_DCS,
    MENU_R_CTCS,
    MENU_T_DCS,
    MENU_T_CTCS,
    MENU_SFT_D,
    MENU_OFFSET,
    MENU_TOT,
    MENU_W_N,
#ifndef ENABLE_FEAT_F4HWN
    MENU_SCR,
#endif
    MENU_BCL,
#ifdef ENABLE_FEAT_F4HWN
    MENU_TX_LOCK, 
#endif
    MENU_MEM_CH,
    MENU_DEL_CH,
    MENU_MEM_NAME,
    MENU_MDF,
    MENU_SAVE,
    MENU_VOX,
    MENU_ABR,
    MENU_ABR_ON_TX_RX,
    MENU_ABR_MIN,
    MENU_ABR_MAX,
    MENU_TDR,
    MENU_BEEP,
#ifdef ENABLE_VOICE
    MENU_VOICE,
#endif
    MENU_SC_REV,
    MENU_AUTOLK,
    MENU_LIST_CH,
    MENU_STE,
    MENU_RP_STE,
    MENU_MIC,
    MENU_MIC_BAR,
    MENU_COMPAND,
    MENU_1_CALL,
    MENU_S_LIST,
    MENU_S_PRI,
    MENU_S_PRI_CH_1,
    MENU_S_PRI_CH_2,    
#ifdef ENABLE_DTMF_CALLING
    MENU_ANI_ID,
#endif
    MENU_UPCODE,
    MENU_DWCODE,
    MENU_PTT_ID,
    MENU_D_ST,
#ifdef ENABLE_DTMF_CALLING
    MENU_D_RSP,
    MENU_D_HOLD,
#endif
    MENU_D_PRE,
#ifdef ENABLE_DTMF_CALLING  
    MENU_D_DCD,
    MENU_D_LIST,
#endif
    MENU_D_LIVE_DEC,
    MENU_PONMSG,
    MENU_ROGER,
    MENU_VOL,
    MENU_BAT_TXT,
    MENU_AM,
#ifndef ENABLE_FEAT_F4HWN
    #ifdef ENABLE_NOAA
        MENU_NOAA_S,
    #endif
#endif
    MENU_RESET,
    MENU_F_LOCK,
#ifndef ENABLE_FEAT_F4HWN
    MENU_200TX,
    MENU_350TX,
    MENU_500TX,
#endif
    MENU_350EN,
#ifndef ENABLE_FEAT_F4HWN
    MENU_SCREN,
#endif
#ifdef ENABLE_F_CAL_MENU
    MENU_F_CALI,  // reference xtal calibration
#endif
#ifdef ENABLE_FEAT_F4HWN_SLEEP
    MENU_SET_OFF,
#endif
#ifdef ENABLE_FEAT_F4HWN
    MENU_SET_PWR,
    MENU_SET_PTT,
    MENU_SET_TOT,
    MENU_SET_EOT,
    MENU_SET_CTR,
    MENU_SET_INV,
    MENU_SET_LCK,
    MENU_SET_MET,
    MENU_SET_GUI,
    MENU_SET_TMR,
    #ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
        MENU_SET_SCN,
    #endif
    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        MENU_SET_NFM,
    #endif
    #ifdef ENABLE_FEAT_F4HWN_VOL
        MENU_SET_VOL,
    #endif
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        MENU_SET_KEY,
    #endif
    #ifdef ENABLE_NOAA
        MENU_NOAA_S,
    #endif
    MENU_SET_NAV,
    #ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
        MENU_SET_CFG,
    #endif
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        MENU_SET_AUD,
    #endif
#endif
    MENU_BATCAL,  // battery voltage calibration
    MENU_F1SHRT,
    MENU_F1LONG,
    MENU_F2SHRT,
    MENU_F2LONG,
    MENU_MLONG,
    MENU_BATTYP,
#if defined(ENABLE_FEAT_F4HWN) && defined(ENABLE_FEAT_F4HWN_LOGO_SAV)
    MENU_SET_SAV
#endif
};

#ifdef ENABLE_FEAT_F4HWN_MENU_CAT
// Categories (niveau 1) — l'ordre = ordre d'affichage de l'ecran des categories.
enum {
    CAT_CHANNELS = 0,
    CAT_SCAN,
    CAT_KEYS,
    CAT_POWER,
    CAT_DISPLAY,
    CAT_TIMERS,
    CAT_AUDIO,
    CAT_RADIO,
    CAT_DTMF,
    CAT_SERVICE,   // menu cache : n'apparait que si gF_LOCK
    CAT_ALL,       // liste plate complete, ordre et numeros d'origine
    CAT_COUNT
};

#define MENU_LEVEL_CAT   0
#define MENU_LEVEL_ITEMS 1

extern const char *const CategoryNames[];
extern uint8_t            gMenuCategory;
extern uint8_t            gMenuLevel;
extern uint8_t            gCatOrder[];
extern uint8_t            gMenuCatCursor;
extern uint8_t            gCatLastPos[];

void UI_MENU_BuildCategoryScreen(void);
uint8_t UI_MENU_CategoryItemCount(uint8_t cat);
#endif

extern const uint8_t FIRST_HIDDEN_MENU_ITEM;
extern const t_menu_item MenuList[];

extern const char* const            gSubMenu_TXP[8];
extern const char* const            gSubMenu_SFT_D[3];
extern const char* const            gSubMenu_W_N[2];
extern const char* const            gSubMenu_OFF_ON[2];
extern const char*                  gSubMenu_NA;
extern const char* const            gSubMenu_TOT[11];
extern const char* const            gSubMenu_RXMode[4];

#ifdef ENABLE_VOICE
    extern const char* const        gSubMenu_VOICE[3];
#endif
extern const char* const            gSubMenu_MDF[4];
#ifdef ENABLE_DTMF_CALLING
extern const char* const            gSubMenu_D_RSP[4];
#endif

#ifdef ENABLE_FEAT_F4HWN
    extern const char* const        gSubMenu_SET_PWR[7];
    extern const char* const        gSubMenu_SET_PTT[2];
    extern const char* const        gSubMenu_SET_TOT[4];
    extern const char* const        gSubMenu_SET_LCK[];
    extern const char* const        gSubMenu_SET_MET[2];
    #ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
        extern const char* const    gSubMenu_SET_SCN[2];
    #endif
    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        extern const char* const    gSubMenu_SET_NFM[2];
    #endif
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        extern const char* const    gSubMenu_SET_KEY[5];
    #endif
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        extern const char* const    gSubMenu_SET_AUD_FM[5];
        extern const char* const    gSubMenu_SET_AUD_AM[3];
    #endif
    #ifdef ENABLE_FEAT_F4HWN_LOGO_SAV
        extern const char* const    gSubMenu_SET_SAV[];
    #endif
#endif

extern const char* const gSubMenu_PTT_ID[5];
#ifdef ENABLE_FEAT_F4HWN
    #ifdef ENABLE_FEAT_F4HWN_LOGO
        extern const char* const    gSubMenu_PONMSG[6];
    #else
        extern const char* const    gSubMenu_PONMSG[5];
    #endif
#else
    extern const char* const        gSubMenu_PONMSG[4];
#endif

extern const char* const            gSubMenu_ROGER[3];
extern const char* const            gSubMenu_RESET[2];
extern const char* const            gSubMenu_F_LOCK[F_LOCK_LEN];
extern const char* const            gSubMenu_RX_TX[4];
extern const char* const            gSubMenu_BAT_TXT[3];
extern const char* const            gSubMenu_BATTYP[5];
extern const char* const            gSubMenu_SET_NAV[2];

#ifndef ENABLE_FEAT_F4HWN
    extern const char* const        gSubMenu_SCRAMBLER[11];
#endif

typedef struct /* __attribute__((packed)) */ {
    const char* name; 
    uint8_t     id;
} t_sidefunction;

extern const uint8_t         gSubMenu_SIDEFUNCTIONS_size;
extern const t_sidefunction  gSubMenu_SIDEFUNCTIONS[];
                         
extern bool              gIsInSubMenu;
                         
extern uint8_t           gMenuCursor;
extern uint8_t           gMenuIndices[];

extern int32_t           gSubMenuSelection;
                         
extern char              edit_original[17];
extern char              edit[17];
extern int               edit_index;
extern bool              edit_is_uppercase;

void UI_DisplayMenu(void);
int UI_MENU_GetCurrentMenuId();
uint8_t UI_MENU_GetMenuIdx(uint8_t id);
uint8_t UI_MENU_GetViewPos(uint8_t id);
void UI_MENU_BuildView(void);

#endif
