/* GENERATED FILE - do not edit. Regenerate with tools/gen_menu_cn.py */

#include <stddef.h>

#include "menu.h"

#ifdef ENABLE_FEAT_F4HWN_CN_FONT

// Chinese menu item names, GB2312. NULL -> fall back to the English name.
const char *UI_MENU_GetNameCN(uint8_t MenuId)
{
    switch (MenuId)
    {
    case MENU_STEP: return "\262\275\275\370";
    case MENU_TXP: return "\271\246\302\312";
    case MENU_R_DCS: return "\312\325DCS";
    case MENU_R_CTCS: return "\312\325\321\307\322\364";
    case MENU_T_DCS: return "\267\242DCS";
    case MENU_T_CTCS: return "\267\242\321\307\322\364";
    case MENU_SFT_D: return "\262\356\306\265";
    case MENU_OFFSET: return "\306\265\262\356";
    case MENU_W_N: return "\264\370\277\355";
#ifndef ENABLE_FEAT_F4HWN
    case MENU_SCR: return "\274\323\303\334";
#endif
    case MENU_BCL: return "\303\246\313\370";
    case MENU_COMPAND: return "\321\271\300\251";
    case MENU_AM: return "\304\243\312\275";
#ifdef ENABLE_FEAT_F4HWN
    case MENU_TX_LOCK: return "TX\313\370";
#endif
    case MENU_LIST_CH: return "\320\305\265\300\261\355";
    case MENU_MEM_CH: return "\264\346\320\305\265\300";
    case MENU_DEL_CH: return "\311\276\320\305\265\300";
    case MENU_MEM_NAME: return "\320\305\265\300\303\373";
    case MENU_S_LIST: return "\311\250\303\350\261\355";
    case MENU_S_PRI: return "\323\305\317\310\311\250";
    case MENU_S_PRI_CH_1: return "\323\305\317\3101";
    case MENU_S_PRI_CH_2: return "\323\305\317\3102";
    case MENU_SC_REV: return "\311\250\273\326\270\264";
    case MENU_F1SHRT: return "F1\266\314\260\264";
    case MENU_F1LONG: return "F1\263\244\260\264";
    case MENU_F2SHRT: return "F2\266\314\260\264";
    case MENU_F2LONG: return "F2\263\244\260\264";
    case MENU_MLONG: return "M\263\244\260\264";
    case MENU_AUTOLK: return "\274\374\305\314\313\370";
    case MENU_TOT: return "\317\336\312\261";
    case MENU_SAVE: return "\312\241\265\347";
    case MENU_BAT_TXT: return "\265\347\301\277";
    case MENU_MIC: return "\302\363\277\313\267\347";
    case MENU_MIC_BAR: return "\302\363\265\347\306\275";
    case MENU_MDF: return "\320\305\265\300\317\324";
    case MENU_PONMSG: return "\277\252\273\372";
    case MENU_ABR: return "\261\263\271\342";
    case MENU_ABR_MIN: return "\261\263\271\342\260\265";
    case MENU_ABR_MAX: return "\261\263\271\342\301\301";
    case MENU_ABR_ON_TX_RX: return "\312\325\267\242\271\342";
    case MENU_BEEP: return "\314\341\312\276\322\364";
#ifdef ENABLE_VOICE
    case MENU_VOICE: return "\323\357\322\364";
#endif
    case MENU_ROGER: return "\275\341\312\370\322\364";
    case MENU_STE: return "\316\262\322\364";
    case MENU_RP_STE: return "\326\320\274\314\316\262";
    case MENU_1_CALL: return "1\272\364\275\320";
#ifdef ENABLE_ALARM
    case MENU_AL_MOD: return "\261\250\276\257";
#endif
#ifdef ENABLE_DTMF_CALLING
    case MENU_ANI_ID: return "\261\276\273\372\302\353";
#endif
    case MENU_UPCODE: return "\311\317\317\337\302\353";
    case MENU_DWCODE: return "\317\302\317\337\302\353";
    case MENU_D_ST: return "DTMF\322\364";
#ifdef ENABLE_DTMF_CALLING
    case MENU_D_RSP: return "DTMF\323\246";
#endif
#ifdef ENABLE_DTMF_CALLING
    case MENU_D_HOLD: return "DTMF\261\243";
#endif
    case MENU_D_PRE: return "DTMF\324\244";
#ifdef ENABLE_DTMF_CALLING
    case MENU_D_DCD: return "DTMF\312\325";
#endif
#ifdef ENABLE_DTMF_CALLING
    case MENU_D_LIST: return "DTMF\261\355";
#endif
    case MENU_D_LIVE_DEC: return "DTMF\275\342";
#ifndef ENABLE_FEAT_F4HWN
#ifdef ENABLE_AM_FIX
    case MENU_AM_FIX: return "AM\320\243";
#endif
#endif
    case MENU_VOX: return "\311\371\277\330";
#ifdef ENABLE_FEAT_F4HWN
    case MENU_VOL: return "\317\265\315\263";
#endif
#ifndef ENABLE_FEAT_F4HWN
    case MENU_VOL: return "\265\347\301\277";
#endif
    case MENU_TDR: return "\312\325\304\243\312\275";
    case MENU_SQL: return "\276\262\324\353";
#ifdef ENABLE_FEAT_F4HWN
    case MENU_SET_PWR: return "\311\350\271\246\302\312";
#endif
#ifdef ENABLE_FEAT_F4HWN
    case MENU_SET_PTT: return "\311\350PTT";
#endif
#ifdef ENABLE_FEAT_F4HWN
    case MENU_SET_TOT: return "\311\350TOT";
#endif
#ifdef ENABLE_FEAT_F4HWN
    case MENU_SET_EOT: return "\311\350EOT";
#endif
#ifdef ENABLE_FEAT_F4HWN
    case MENU_SET_CTR: return "\311\350\266\324\261\310";
#endif
#ifdef ENABLE_FEAT_F4HWN
    case MENU_SET_INV: return "\311\350\267\264\317\324";
#endif
#ifdef ENABLE_FEAT_F4HWN
    case MENU_SET_LCK: return "\311\350\313\370\266\250";
#endif
#ifdef ENABLE_FEAT_F4HWN
    case MENU_SET_MET: return "\311\350\261\355\315\267";
#endif
#ifdef ENABLE_FEAT_F4HWN
    case MENU_SET_GUI: return "\311\350\275\347\303\346";
#endif
    case MENU_LANG: return "\323\357\321\324";
#ifdef ENABLE_FEAT_F4HWN_AUDIO
    case MENU_SET_AUD: return "\311\350\312\325\322\364";
#endif
#ifdef ENABLE_FEAT_F4HWN
    case MENU_SET_TMR: return "\311\350\312\261\306\367";
#endif
#ifdef ENABLE_FEAT_F4HWN_SLEEP
    case MENU_SET_OFF: return "\311\350\271\330\273\372";
#endif
#ifdef ENABLE_FEAT_F4HWN_NARROWER
    case MENU_SET_NFM: return "\311\350\325\255\264\370";
#endif
#ifdef ENABLE_FEAT_F4HWN_VOL
    case MENU_SET_VOL: return "\311\350\322\364\301\277";
#endif
#ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
    case MENU_SET_KEY: return "\311\350\260\264\274\374";
#endif
#ifdef ENABLE_FEAT_F4HWN
#ifdef ENABLE_NOAA
    case MENU_NOAA_S: return "\311\350NOAA";
#endif
#endif
#ifndef ENABLE_FEAT_F4HWN
#ifdef ENABLE_NOAA
    case MENU_NOAA_S: return "NOAA\311\250";
#endif
#endif
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
    case MENU_SET_SCN: return "\311\350\311\250\303\350";
#endif
#ifdef ENABLE_FEAT_F4HWN_LOGO_SAV
    case MENU_SET_SAV: return "\311\350\312\241\265\347";
#endif
    case MENU_F_LOCK: return "\306\265\266\316\313\370";
#ifndef ENABLE_FEAT_F4HWN
    case MENU_200TX: return "200\267\242";
#endif
#ifndef ENABLE_FEAT_F4HWN
    case MENU_350TX: return "350\267\242";
#endif
#ifndef ENABLE_FEAT_F4HWN
    case MENU_500TX: return "500\267\242";
#endif
    case MENU_350EN: return "350\277\252";
#ifndef ENABLE_FEAT_F4HWN
    case MENU_SCREN: return "\274\323\303\334\277\252";
#endif
#ifdef ENABLE_F_CAL_MENU
    case MENU_F_CALI: return "\306\265\302\312\320\243";
#endif
    case MENU_BATCAL: return "\265\347\263\330\320\243";
    case MENU_BATTYP: return "\265\347\263\330\320\315";
    case MENU_SET_NAV: return "\311\350\265\274\272\275";
    case MENU_RESET: return "\270\264\316\273";
    default:
        return NULL;
    }
}

// Value shown for the MENU_LANG item when Chinese is active.
const char *UI_MENU_GetLangValueCN(void)
{
    return "\274\362\314\345\326\320\316\304";
}

#ifdef ENABLE_FEAT_F4HWN_MENU_CAT

// Chinese category names (menu level 1), GB2312. NULL -> English.
const char *UI_MENU_GetCategoryCN(uint8_t Cat)
{
    switch (Cat)
    {
    case CAT_CHANNELS: return "\320\305\265\300";
    case CAT_SCAN: return "\311\250\303\350";
    case CAT_KEYS: return "\260\264\274\374";
    case CAT_POWER: return "\271\246\302\312";
    case CAT_DISPLAY: return "\317\324\312\276";
    case CAT_TIMERS: return "\266\250\312\261";
    case CAT_AUDIO: return "\322\364\306\265";
    case CAT_RADIO: return "\265\347\314\250";
    case CAT_DTMF: return "DTMF";
    case CAT_SERVICE: return "\267\376\316\361";
    case CAT_ALL: return "\310\253\262\277";
    default:
        return NULL;
    }
}

#endif // ENABLE_FEAT_F4HWN_MENU_CAT

#endif // ENABLE_FEAT_F4HWN_CN_FONT
