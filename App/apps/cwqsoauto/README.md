# CW QSO Auto v1.0

Standalone CW application for compatible F4HWN UV-K5 V3 firmware, ABI 1/API 3
minimum. This contribution is compiled and software-tested against
`feature_update_v7` (resident API 4). The stable app was hardware-tested by its
author on UV-K5 V3; this packaging does not claim a physical-radio V7 test.

[Français](README.fr.md) · [Architecture](ARCHITECTURE.md) · [Validation](TESTING.md)

## Personalize and install

Open [editor.html](editor.html) as a local file in a recent browser. It embeds
the exact current APP and works offline, with no server or compiler required.
Each of the 16 slots has a fixed number, a name of 0–12 printable ASCII
characters and up to 64 encoded CW bytes. Names are display-only, never sent.
CW supports A–Z, digits, space, `/`, `?` and `{RST}` (one encoded byte).
Leading/trailing/doubled CW spaces are rejected. Empty contents disable a message.
No automatic case conversion or truncation occurs. Replace F4XXX and SEB before
on-air use. CALL and NAME are literal text, not runtime tokens.

Export `CWQSOAuto-personnalise.app`, keep it as backup, then install it using
UV Studio's Applications view on compatible firmware. Reimport restores names
and contents. Closing the editor does not save automatically; installing fresh
defaults overwrites personalization. The HTML does not read data from the radio.

The single current installable reference is
[`archive/apps/v6.3.0/CWQSOAuto.app`](../../../archive/apps/v6.3.0/CWQSOAuto.app).
The archive directory follows the existing V7 repository convention; it does
not change the target branch. The embedded APP is byte-identical. Old approved
imports retain their executable; old formats without names expose contents only.

## Controls

| Control | Main screen |
| --- | --- |
| UP/DOWN | Circular message selection 01–16 |
| PTT | Send the selected message once; holding it does not repeat |
| SIDE 1 | Repeat the selection captured at activation |
| SIDE 2 | Stop repetition; let a started message finish, cancel a paused restart |
| EXIT or paddle during automatic TX | Cancel immediately through normal RF shutdown |
| Digits | Shift the three RST digits; default 599 at entry |
| MENU | Open SETTINGS |
| EXIT when idle | Exit and save settings |

The initial selection is 01. Navigation during a pause changes the displayed
selection, not the captured repeat target. PTT during a pause ends repetition
and sends the current selection. Repeated SIDE 1 presses do not queue requests.
All 16 selections show number/name and a bounded preview: `*` means RST and
`>` means more text. MENU is lower-left; no preview-help row is displayed.

In SETTINGS: UP/DOWN adjusts REPEAT (1–15 s, default 5, persistent); 1 switches
STRAIGHT/PADDLE; 8 cycles AUTO/IAMB A/IAMB B; 2 changes WPM (5–60), 3 tone,
4 break-in, 5 paddle reversal, 6 XTAL calibration, 0 speaker. F then a numeric
setting decreases it. PTT is a straight key while held, with the existing ramps,
TX-denial latch and maximum-mark protection. MENU/EXIT returns to the main screen.
Release controls at startup and after cancellation. There is no TIP arming hold.

## Build and limitations

Set ARM_CC to GNU Arm Embedded GCC 13.3.Rel1, then run
`python App/apps/cwqsoauto/build.py` from the repo root. `build.sh` is the normal
compile-app entry. Overlay **4084/4096 bytes including 92 BSS**, assets **2286**.
The APP is identical to stable tag `cw-qso-auto-v1.0-stable`; FAP version remains
0.4 to preserve that validated binary. v1.0 names the stable feature release.

Early speaker activation makes the first dit audible. A slight startup click
remains: an intentional trade-off reported on the author's radio. No claim of
click-free V7 analogue behavior or UV-K1 hardware qualification is made.
No resident or other application changes are required.
