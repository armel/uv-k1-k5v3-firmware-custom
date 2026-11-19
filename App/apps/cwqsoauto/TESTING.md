# CW QSO Auto v1.0 — V7 validation

Target: feature_update_v7 at 68d4d40793b254d7578f12d10f9d63259c4b0e31.
Use GNU Arm Embedded GCC 13.3.Rel1, host GCC, Python, Node, optional Edge:

```
# ARM_CC points to arm-none-eabi-gcc
python App/apps/cwqsoauto/test/run.py --browser
```

The runner compiles with the strict 4096-byte linker and -Werror, generates
the production/mocked-API host harness, and runs:

- 305 config/persistence-mock/SETTINGS PTT/refusal/held-input/repeat checks.
- 272 captured-selection repeat cases, all 16 messages, 1/5/15 s, AUTO/A/B,
  navigation during pause, SIDE stop, PTT, cancellation, refusal and wrap.
- 4320 name boundary/invalid-byte cases; 2112 bounded preview cases.
- 1184 navigation cases and 896 independent message/speed oracles (5–60 WPM).
- 1488 bank-reader cases, 16 proxy-layout checks, 144 AUTO/A/B scenarios.
- Four early-HP/RF/RX command traces and 157 actual Thumb relay ABI checks.
- V7 helper/font rendering, including all 12 SetFnt values on QSO and SETTINGS.
  FW_ASSETS_Read is modeled as a byte copy; physical SPI latency is not tested.
- Editor limits, immutable code/data checks, every-byte corruption rejection,
  default and personalized export/reimport; real Edge headless file:// with
  network forced offline, mobile width and zero external requests.
- Personalized APP read/transmission through the production C harness, release
  hashes/CRCs and independent repacking with the official packer.

The old repeat01 APP is only a fixture at test/fixtures/legacy-repeat01.app;
it is not an installable current reference. Some older individual drivers
(configuration/settings/menu_hint/startup/repeat) describe historical versions;
the current runner uses their replacement coverage above rather than claiming
their obsolete release/UI assertions pass unchanged.

## Hardware scope

The user confirmed the exact stable v1.0 APP works on UV-K5 V3. Rebuilding on
V7 produces that same APP: overlay 4084 including 92 BSS, assets 2286, file 6434.
No physical-radio V7 test was performed during integration. The existing small
startup audio click is not claimed eliminated. Flash persistence is mocked.

On V7, install the matching resident firmware/assets, then verify RX audio,
first DIT, paddles in all four modes, 16 names/previews/PTT, RST, repeat delays,
SIDE 2 during TX/pause, SETTINGS PTT hold/release/refusal and reboot persistence.
UV-K1 hardware remains unqualified here. Test using an appropriate RF load.

The release.json manifest pins the current source, code, assets and APP.
Generated logs, render images and host executables remain ignored.
