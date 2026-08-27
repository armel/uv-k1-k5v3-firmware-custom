/* GENERATED FILE - do not edit. Regenerate with tools/gen_menu_cn.py */

#ifndef UI_MENU_CN_H
#define UI_MENU_CN_H

#include <stdint.h>

#ifdef ENABLE_FEAT_F4HWN_CN_FONT

// Chinese menu item name for MenuId, GB2312-encoded; NULL -> English.
const char *UI_MENU_GetNameCN(uint8_t MenuId);

// "¼òÌåÖÐÎÄ": the MENU_LANG value shown while Chinese is active.
const char *UI_MENU_GetLangValueCN(void);

#ifdef ENABLE_FEAT_F4HWN_MENU_CAT
// Chinese category name; NULL -> English.
const char *UI_MENU_GetCategoryCN(uint8_t Cat);
#endif

#endif // ENABLE_FEAT_F4HWN_CN_FONT

#endif // UI_MENU_CN_H
