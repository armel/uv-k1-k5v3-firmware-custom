/* Categorized menu for DualMode: Radio, Scan, Mode, Config
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef ENABLE_FEAT_DUALMODE

#include <stdbool.h>

#include "menu.h"
#include "misc.h"

#define CAT_RADIO  0
#define CAT_SCAN   1
#define CAT_MODE   2
#define CAT_CONFIG 3

static bool   gMenuSelectingCategory = true;
static uint8_t gMenuCategory = 0;

/* Return category 0-3 for menu_id. Default Config for unknown. */
static uint8_t GetCategoryForMenuId(uint8_t menu_id)
{
    switch (menu_id) {
        case MENU_STEP:
        case MENU_TXP:
        case MENU_R_DCS:
        case MENU_R_CTCS:
        case MENU_T_DCS:
        case MENU_T_CTCS:
        case MENU_SFT_D:
        case MENU_OFFSET:
        case MENU_W_N:
#ifndef ENABLE_FEAT_F4HWN
        case MENU_SCR:
#endif
        case MENU_BCL:
        case MENU_COMPAND:
        case MENU_AM:
#ifdef ENABLE_FEAT_F4HWN
        case MENU_TX_LOCK:
#endif
        case MENU_LIST_CH:
        case MENU_MEM_CH:
        case MENU_DEL_CH:
        case MENU_MEM_NAME:
            return CAT_RADIO;

        case MENU_S_LIST:
        case MENU_S_PRI:
        case MENU_S_PRI_CH_1:
        case MENU_S_PRI_CH_2:
        case MENU_SC_REV:
#ifdef ENABLE_NOAA
        case MENU_NOAA_S:
#endif
            return CAT_SCAN;

        case MENU_F1SHRT:
        case MENU_F1LONG:
        case MENU_F2SHRT:
        case MENU_F2LONG:
        case MENU_MLONG:
#ifdef ENABLE_FEAT_DUALMODE
        case MENU_OP_MODE:
        case MENU_OP_MODE_SET_PIN:
#endif
            return CAT_MODE;

        default:
            return CAT_CONFIG;
    }
}

uint8_t UI_MENU_GetCategoryItemCount(uint8_t category)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < gMenuListCount && MenuList[i].name[0] != '\0'; i++) {
        if (GetCategoryForMenuId(MenuList[i].menu_id) == category)
            count++;
    }
    return count;
}

/* Return MenuList index for the idx-th item in category. */
uint8_t UI_MENU_GetCategoryMenuListIndex(uint8_t category, uint8_t idx)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < gMenuListCount && MenuList[i].name[0] != '\0'; i++) {
        if (GetCategoryForMenuId(MenuList[i].menu_id) == category) {
            if (count == idx)
                return i;
            count++;
        }
    }
    return 0;
}

/* Get category (0-3) for MenuList index i. */
uint8_t UI_MENU_GetCategoryFromListIndex(uint8_t list_idx)
{
    if (list_idx >= gMenuListCount)
        return CAT_CONFIG;
    return GetCategoryForMenuId(MenuList[list_idx].menu_id);
}

void UI_MENU_EnterWithCategories(void)
{
    gMenuSelectingCategory = true;
    gMenuCategory = 0;
    gMenuCursor = 0;
}

bool UI_MENU_IsSelectingCategory(void)
{
    return gMenuSelectingCategory;
}

void UI_MENU_SetSelectingCategory(bool sel)
{
    gMenuSelectingCategory = sel;
}

uint8_t UI_MENU_GetCategory(void)
{
    return gMenuCategory;
}

void UI_MENU_SetCategory(uint8_t cat)
{
    if (cat <= CAT_CONFIG)
        gMenuCategory = cat;
}

/* Effective MenuList index for display/logic when in category mode. */
uint8_t UI_MENU_GetEffectiveListIndex(void)
{
    if (gMenuSelectingCategory)
        return 0; /* not used when selecting category */
    return UI_MENU_GetCategoryMenuListIndex(gMenuCategory, gMenuCursor);
}

uint8_t UI_MENU_GetEffectiveListCount(void)
{
    if (gMenuSelectingCategory)
        return 4; /* Radio, Scan, Mode, Config */
    return UI_MENU_GetCategoryItemCount(gMenuCategory);
}

#endif /* ENABLE_FEAT_DUALMODE */
