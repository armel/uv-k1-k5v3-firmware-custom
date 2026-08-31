#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate App/ui/menu_cn.c/.h (Chinese menu names) from the translation table.

The generated C file stores GB2312 bytes as \\xNN escapes so it compiles
regardless of the source charset. Each menu name must fit the 48 px left
column: 8 px per ASCII char (gFontBig pitch) + 16 px per Chinese char.
Unconditional items have cond=None; 'X' wraps the case in
#ifdef X, '!X' in #ifndef X. Items with several mutually exclusive
definitions (MENU_VOL, MENU_NOAA_S) take a list of (cond, text) branches.

Usage: python tools/gen_menu_cn.py
"""

import sys

# 多系统环境下 stdout 可能不是 UTF-8，强制 UTF-8 避免状态信息打印报错。
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

# (menu_id, [(cond, text), ...])  -- text is UTF-8 source, encoded GB2312 in C
MENU_CN = [
    # ---- main menu ----
    ("MENU_STEP",          [("步进", None)]),
    ("MENU_TXP",           [("功率", None)]),
    ("MENU_R_DCS",         [("收DCS", None)]),
    ("MENU_R_CTCS",        [("收亚音", None)]),
    ("MENU_T_DCS",         [("发DCS", None)]),
    ("MENU_T_CTCS",        [("发亚音", None)]),
    ("MENU_SFT_D",         [("差频", None)]),
    ("MENU_OFFSET",        [("频差", None)]),
    ("MENU_W_N",           [("带宽", None)]),
    ("MENU_SCR",           [("加密", "!ENABLE_FEAT_F4HWN")]),
    ("MENU_BCL",           [("忙锁", None)]),
    ("MENU_COMPAND",       [("压扩", None)]),
    ("MENU_AM",            [("模式", None)]),
    ("MENU_TX_LOCK",       [("TX锁", "ENABLE_FEAT_F4HWN")]),
    ("MENU_LIST_CH",       [("信道表", None)]),
    ("MENU_MEM_CH",        [("存信道", None)]),
    ("MENU_DEL_CH",        [("删信道", None)]),
    ("MENU_MEM_NAME",      [("信道名", None)]),
    ("MENU_S_LIST",        [("扫描表", None)]),
    ("MENU_S_PRI",         [("优先扫", None)]),
    ("MENU_S_PRI_CH_1",    [("优先1", None)]),
    ("MENU_S_PRI_CH_2",    [("优先2", None)]),
    ("MENU_SC_REV",        [("扫恢复", None)]),
    ("MENU_F1SHRT",        [("F1短按", None)]),
    ("MENU_F1LONG",        [("F1长按", None)]),
    ("MENU_F2SHRT",        [("F2短按", None)]),
    ("MENU_F2LONG",        [("F2长按", None)]),
    ("MENU_MLONG",         [("M长按", None)]),
    ("MENU_AUTOLK",        [("键盘锁", None)]),
    ("MENU_TOT",           [("限时", None)]),
    ("MENU_SAVE",          [("省电", None)]),
    ("MENU_BAT_TXT",       [("电量", None)]),
    ("MENU_MIC",           [("麦克风", None)]),
    ("MENU_MIC_BAR",       [("麦电平", None)]),
    ("MENU_MDF",           [("信道显", None)]),
    ("MENU_PONMSG",        [("开机", None)]),
    ("MENU_ABR",           [("背光", None)]),
    ("MENU_ABR_MIN",       [("背光暗", None)]),
    ("MENU_ABR_MAX",       [("背光亮", None)]),
    ("MENU_ABR_ON_TX_RX",  [("收发光", None)]),
    ("MENU_BEEP",          [("提示音", None)]),
    ("MENU_VOICE",         [("语音", "ENABLE_VOICE")]),
    ("MENU_ROGER",         [("结束音", None)]),
    ("MENU_STE",           [("尾音", None)]),
    ("MENU_RP_STE",        [("中继尾", None)]),
    ("MENU_1_CALL",        [("1呼叫", None)]),
    ("MENU_AL_MOD",        [("报警", "ENABLE_ALARM")]),
    ("MENU_ANI_ID",        [("本机码", "ENABLE_DTMF_CALLING")]),
    ("MENU_UPCODE",        [("上线码", None)]),
    ("MENU_DWCODE",        [("下线码", None)]),
    # "PTT ID" kept in English: "PTT身份" exceeds the 48 px column
    ("MENU_D_ST",          [("DTMF音", None)]),
    ("MENU_D_RSP",         [("DTMF应", "ENABLE_DTMF_CALLING")]),
    ("MENU_D_HOLD",        [("DTMF保", "ENABLE_DTMF_CALLING")]),
    ("MENU_D_PRE",         [("DTMF预", None)]),
    ("MENU_D_DCD",         [("DTMF收", "ENABLE_DTMF_CALLING")]),
    ("MENU_D_LIST",        [("DTMF表", "ENABLE_DTMF_CALLING")]),
    ("MENU_D_LIVE_DEC",    [("DTMF解", None)]),
    ("MENU_AM_FIX",        [("AM校", "!ENABLE_FEAT_F4HWN", "ENABLE_AM_FIX")]),
    ("MENU_VOX",           [("声控", None)]),
    # same id, two mutually exclusive definitions
    ("MENU_VOL",           [("系统", "ENABLE_FEAT_F4HWN"), ("电量", "!ENABLE_FEAT_F4HWN")]),
    ("MENU_TDR",           [("收模式", None)]),
    ("MENU_SQL",           [("静噪", None)]),
    ("MENU_SET_PWR",       [("设功率", "ENABLE_FEAT_F4HWN")]),
    ("MENU_SET_PTT",       [("设PTT", "ENABLE_FEAT_F4HWN")]),
    ("MENU_SET_TOT",       [("设TOT", "ENABLE_FEAT_F4HWN")]),
    ("MENU_SET_EOT",       [("设EOT", "ENABLE_FEAT_F4HWN")]),
    ("MENU_SET_CTR",       [("设对比", "ENABLE_FEAT_F4HWN")]),
    ("MENU_SET_INV",       [("设反显", "ENABLE_FEAT_F4HWN")]),
    ("MENU_SET_LCK",       [("设锁定", "ENABLE_FEAT_F4HWN")]),
    ("MENU_SET_MET",       [("设表头", "ENABLE_FEAT_F4HWN")]),
    ("MENU_SET_GUI",       [("设界面", "ENABLE_FEAT_F4HWN")]),
    ("MENU_LANG",          [("语言", None)]),
    ("MENU_SET_AUD",       [("设收音", "ENABLE_FEAT_F4HWN_AUDIO")]),
    ("MENU_SET_TMR",       [("设时器", "ENABLE_FEAT_F4HWN")]),
    ("MENU_SET_OFF",       [("设关机", "ENABLE_FEAT_F4HWN_SLEEP")]),
    ("MENU_SET_NFM",       [("设窄带", "ENABLE_FEAT_F4HWN_NARROWER")]),
    ("MENU_SET_VOL",       [("设音量", "ENABLE_FEAT_F4HWN_VOL")]),
    ("MENU_SET_KEY",       [("设按键", "ENABLE_FEAT_F4HWN_RESCUE_OPS")]),
    # same id: F4HWN name "SetNWR" / stock name "NOAA-S"
    ("MENU_NOAA_S",        [("设NOAA", "ENABLE_FEAT_F4HWN", "ENABLE_NOAA"),
                            ("NOAA扫", "!ENABLE_FEAT_F4HWN", "ENABLE_NOAA")]),
    ("MENU_SET_SCN",       [("设扫描", "ENABLE_FEAT_F4HWN_SCAN_FASTER")]),
    ("MENU_SET_SAV",       [("设省电", "ENABLE_FEAT_F4HWN_LOGO_SAV")]),
    # ---- hidden menu items ----
    ("MENU_F_LOCK",        [("频段锁", None)]),
    ("MENU_200TX",         [("200发", "!ENABLE_FEAT_F4HWN")]),
    ("MENU_350TX",         [("350发", "!ENABLE_FEAT_F4HWN")]),
    ("MENU_500TX",         [("500发", "!ENABLE_FEAT_F4HWN")]),
    ("MENU_350EN",         [("350开", None)]),
    ("MENU_SCREN",         [("加密开", "!ENABLE_FEAT_F4HWN")]),
    ("MENU_F_CALI",        [("频率校", "ENABLE_F_CAL_MENU")]),
    ("MENU_BATCAL",        [("电池校", None)]),
    ("MENU_BATTYP",        [("电池型", None)]),
    ("MENU_SET_NAV",       [("设导航", None)]),
    ("MENU_RESET",         [("复位", None)]),
]

CAT_CN = [
    ("CAT_CHANNELS", "信道"),
    ("CAT_SCAN",     "扫描"),
    ("CAT_KEYS",     "按键"),
    ("CAT_POWER",    "功率"),
    ("CAT_DISPLAY",  "显示"),
    ("CAT_TIMERS",   "定时"),
    ("CAT_AUDIO",    "音频"),
    ("CAT_RADIO",    "电台"),
    ("CAT_DTMF",     "DTMF"),
    ("CAT_SERVICE",  "服务"),
    ("CAT_ALL",      "全部"),
]

MENU_COLUMN_PX = 48  # 6 ASCII chars * 8 px


def width_px(s):
    return sum(8 if ord(ch) < 128 else 16 for ch in s)


def esc_bytes(s):
    """UTF-8 -> C string literal. GB2312 bytes use octal escapes (fixed 3
    digits, so a following hex-digit ASCII char can never merge into the
    escape, unlike \\xNN)."""
    out = []
    for ch in s:
        if ord(ch) < 128:
            if ch == '"':
                out.append('\\"')
            elif ch == '\\':
                out.append('\\\\')
            else:
                out.append(ch)
        else:
            out.extend("\\%03o" % b for b in ch.encode("gb2312"))
    return "".join(out)


def gen():
    # validate
    for mid, branches in MENU_CN:
        for text, *conds in branches:
            w = width_px(text)
            if w > MENU_COLUMN_PX:
                raise SystemExit("too wide: %s %r = %d px" % (mid, text, w))
            try:
                esc_bytes(text)
            except UnicodeEncodeError:
                raise SystemExit("not GB2312-encodable: %s %r" % (mid, text))

    lines = [
        "/* GENERATED FILE - do not edit. Regenerate with tools/gen_menu_cn.py */",
        "",
        '#include <stddef.h>',
        "",
        '#include "menu.h"',
        "",
        "#ifdef ENABLE_FEAT_F4HWN_CN_FONT",
        "",
        "// Chinese menu item names, GB2312. NULL -> fall back to the English name.",
        "const char *UI_MENU_GetNameCN(uint8_t MenuId)",
        "{",
        "    switch (MenuId)",
        "    {",
    ]

    # each branch is an independent #ifdef block: (cond, text) pairs where
    # cond may be None (unconditional), "X" (#ifdef X) or "!X" (#ifndef X)
    # each branch is an independent #ifdef block with balanced #endif(s);
    # entries with several branches (MENU_VOL, MENU_NOAA_S) must be mutually
    # exclusive, so every one of their branches carries its own conditions
    def emit_branches(mid, branches):
        for text, *conds in branches:
            conds = [c for c in conds if c]   # drop None placeholders
            if not conds:
                if len(branches) > 1:
                    raise SystemExit("branch without condition: %s %r" % (mid, text))
                lines.append("    case %s: return \"%s\";" % (mid, esc_bytes(text)))
                continue
            for cond in conds:
                lines.append("#ifdef %s" % cond if not cond.startswith("!") else "#ifndef %s" % cond[1:])
            lines.append("    case %s: return \"%s\";" % (mid, esc_bytes(text)))
            for _ in conds:
                lines.append("#endif")

    for mid, branches in MENU_CN:
        emit_branches(mid, branches)

    lines += [
        "    default:",
        "        return NULL;",
        "    }",
        "}",
        "",
        "// Value shown for the MENU_LANG item when Chinese is active.",
        "const char *UI_MENU_GetLangValueCN(void)",
        "{",
        "    return \"%s\";" % esc_bytes("简体中文"),
        "}",
        "",
        "#ifdef ENABLE_FEAT_F4HWN_MENU_CAT",
        "",
        "// Chinese category names (menu level 1), GB2312. NULL -> English.",
        "const char *UI_MENU_GetCategoryCN(uint8_t Cat)",
        "{",
        "    switch (Cat)",
        "    {",
    ]
    for cat, text in CAT_CN:
        lines.append("    case %s: return \"%s\";" % (cat, esc_bytes(text)))
    lines += [
        "    default:",
        "        return NULL;",
        "    }",
        "}",
        "",
        "#endif // ENABLE_FEAT_F4HWN_MENU_CAT",
        "",
        "#endif // ENABLE_FEAT_F4HWN_CN_FONT",
        "",
    ]

    header = [
        "/* GENERATED FILE - do not edit. Regenerate with tools/gen_menu_cn.py */",
        "",
        "#ifndef UI_MENU_CN_H",
        "#define UI_MENU_CN_H",
        "",
        "#include <stdint.h>",
        "",
        "#ifdef ENABLE_FEAT_F4HWN_CN_FONT",
        "",
        "// Chinese menu item name for MenuId, GB2312-encoded; NULL -> English.",
        "const char *UI_MENU_GetNameCN(uint8_t MenuId);",
        "",
        "// \"简体中文\": the MENU_LANG value shown while Chinese is active.",
        "const char *UI_MENU_GetLangValueCN(void);",
        "",
        "#ifdef ENABLE_FEAT_F4HWN_MENU_CAT",
        "// Chinese category name; NULL -> English.",
        "const char *UI_MENU_GetCategoryCN(uint8_t Cat);",
        "#endif",
        "",
        "#endif // ENABLE_FEAT_F4HWN_CN_FONT",
        "",
        "#endif // UI_MENU_CN_H",
        "",
    ]

    import os
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "App", "ui", "menu_cn.c"), "w", newline="\n") as f:
        f.write("\n".join(lines))
    with open(os.path.join(here, "..", "App", "ui", "menu_cn.h"), "w", newline="\n") as f:
        f.write("\n".join(header))

    # sanity: every #ifdef/#ifndef must be closed before the next one opens
    depth = 0
    for ln in lines:
        if ln.startswith("#ifdef ") or ln.startswith("#ifndef "):
            depth += 1
        elif ln.startswith("#endif"):
            depth -= 1
            if depth < 0:
                raise SystemExit("unbalanced #endif in generated output")
    if depth != 0:
        raise SystemExit("unbalanced #ifdef in generated output (depth %d)" % depth)

    print("generated App/ui/menu_cn.c / menu_cn.h (%d menu items, %d categories)"
          % (len(MENU_CN), len(CAT_CN)))


if __name__ == "__main__":
    gen()
