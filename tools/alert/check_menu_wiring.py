#!/usr/bin/env python3
"""Check that a menu entry is wired into every place the firmware needs.

Adding a menu item to this codebase takes five separate edits, and missing any
one of them still compiles. The failure modes are silent and only show up on
the radio:

  * missing from the enum          -> will not build
  * missing from MenuList          -> no name, never listed
  * missing from a Cat* array      -> invisible whenever ENABLE_CUSTOM_MENU_LAYOUT
                                      is on, which it is in every preset
  * missing a UI_DisplayMenu case  -> entry appears with an empty value area
  * missing an accept case         -> selecting it silently does nothing

All five of these were hit while porting the ALERT receiver. Run this instead
of finding out by flashing a radio.

    python tools/alert/check_menu_wiring.py [MENU_ID ...]
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "App"


def read(rel):
    return (APP / rel).read_text(encoding="utf-8", errors="replace")


def check(menu_id):
    ui_menu_h = read("ui/menu.h")
    ui_menu_c = read("ui/menu.c")
    app_menu_c = read("app/menu.c")

    results = []

    results.append((
        "declared in the menu enum (ui/menu.h)",
        re.search(rf"^\s*{menu_id}\s*,", ui_menu_h, re.M) is not None))

    results.append((
        "listed in MenuList[] (ui/menu.c)",
        re.search(rf'\{{\s*"[^"]{{1,6}}"\s*,\s*{menu_id}\s*\}}', ui_menu_c) is not None))

    cats = re.findall(r"static const uint8_t Cat\w+\[\][^;]*;", ui_menu_c, re.S)
    results.append((
        "a member of some Cat* category (ui/menu.c)",
        any(re.search(rf"\b{menu_id}\b", c) for c in cats)))

    # the value-rendering switch lives in UI_DisplayMenu
    results.append((
        "has a UI_DisplayMenu render case (ui/menu.c)",
        re.search(rf"case\s+{menu_id}\s*:", ui_menu_c) is not None))

    results.append((
        "has an accept case (app/menu.c)",
        len(re.findall(rf"case\s+{menu_id}\s*:", app_menu_c)) >= 1))

    # ordering: hidden items sit at or after FIRST_HIDDEN_MENU_ITEM
    order = [m.group(1) for m in re.finditer(r"^\s*(MENU_[A-Z0-9_]+)\s*,", ui_menu_h, re.M)]
    if menu_id in order and "MENU_F_LOCK" in order:
        results.append((
            "declared before MENU_F_LOCK, so it is visible",
            order.index(menu_id) < order.index("MENU_F_LOCK")))

    print(f"{menu_id}:")
    ok = True
    for label, passed in results:
        print(f"  {'PASS' if passed else 'FAIL'}  {label}")
        ok &= passed
    return ok


def main():
    ids = sys.argv[1:] or ["MENU_ALERT"]
    if all(check(i) for i in ids):
        print("\nall menu wiring checks passed")
        return 0
    print("\nMENU WIRING INCOMPLETE - the entry will misbehave on the radio")
    return 1


if __name__ == "__main__":
    sys.exit(main())
