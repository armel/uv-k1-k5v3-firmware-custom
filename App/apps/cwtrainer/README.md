# CW Trainer 0.5

Standalone Morse practice for **UV-K5 V3**, F4HWN Fusion **ABI 1 / API 3**.
[Documentation française](README.fr.md) · [Validation and memory report](VALIDATION.md)

CW Trainer uses **local audio only**. It does not request radio transmission.
Software checks cover RF-related code paths; they are not definitive evidence
of the physical absence of RF. F4LPF tested 0.3.3 on a real UV-K5 V3 with a USB-C
paddle: IAMB A/B, TX FREE, RX TRAIN, RX>TX, audio, decoding and ergonomics. A real
straight key was not tested; STRAIGHT is validated in software only. A second
receiver on the same frequency detected no RF during the trials; this is a
practical check, not an instrumental demonstration of absolute RF absence or
an RF certification.

## 0.5 version

Version 0.5 adds the full punctuation set and the prosigns (issue #722). TX FREE
and RX>TX decode them, and RX TRAIN / RX>TX groups draw from all 63 signs.
Elements that match no sign now show as `*`, which Morse never sends, so a real
`?` (`..--..`) is no longer ambiguous.

- Punctuation: `. , ? ' ! / ( ) & : ; = + - _ " $ @`
- Prosigns with a character: AR = `+`, AS = `&`, BT = `=`, KN = `(`
- Other prosigns, shown as a sign Morse does not use:

| Shown | Prosign | Morse | Meaning |
|---|---|---|---|
| `[` | CT / KA | `-.-.-` | start of transmission |
| `]` | SK / VA | `...-.-` | end of contact |
| `^` | VE / SN | `...-.` | understood |
| `~` | HH | `........` | error |
| `#` | SOS | `...---...` | distress |
| `>` | CL | `-.-..-..` | closing down |
| `\|` | BK | `-...-.-` | break |
| `{` | AA | `.-.-` | new line |
| `}` | DO | `-..---` | shift to Wabun |

The decoder and playback now handle up to 9 elements (SOS). The Morse tables
moved to the assets in external flash as direct lookups (code to sign, sign to
code), so the overlay shrinks to 4,060 B; assets grow to 2,763 B of 3,840.

## 0.4 version

Version 0.4 normalizes the application version to the `X.Y` format. Runtime
behavior is unchanged from 0.3.3, so the existing software and hardware
validation below still describes the same code.

## 0.3.3 correction

RX TRAIN now ignores SIDE1 and removes `S1 NEW`; only `PTT RPLY` remains in
the existing tiny font. Use **7 NEW** to generate/play a group. RX>TX SIDE1
replay and all other controls/modes are unchanged. This follows the reported
hardware behavior; no firmware-side SIDE1 diagnosis or change is included.

## Features

- **TX FREE:** USB straight key or iambic A/B paddle, local sidetone and decoded
  A–Z, 0–9, punctuation and prosign text scrolling over three lines. PTT also works as a straight key.
- **RX TRAIN:** listen to random groups of letters, digits, punctuation and prosigns, optionally write them on
  paper, then reveal the target. Key 7 generates/plays a group; PTT replays it.
- **RX>TX:** listen, reproduce with the key/paddle, and compare the decoded
  response with the target. Positional score and distinct success/error sounds.
- Independent RX/TX speed (5–60 WPM) and tone (300–1,000 Hz, 50 Hz steps).
- Groups of 1–5 characters and pedagogical character GAP of 1/1.5/2/3×.
- Persistent settings, compact physical-keypad command grid and no help pages.

## Install and build

Use compatible Fusion API3 firmware on UV-K5 V3. Install the **complete
`CWTrainer.app` including its assets** into a free app slot using UV Studio.
Open it from the Apps menu; verify version 0.5 in the slot information.
No firmware rebuild, application registration or integration with another app
is needed. Keep a previous `.app` for rollback.

From the repository root:

```sh
./compile-app.sh cwtrainer
# Output: build/Apps/CWTrainer.app
```

The existing Docker script discovers `App/apps/cwtrainer/build.sh`. It builds
only this application when given `cwtrainer`; no changes to that script are
required. A direct build with Python 3 and ARM GCC 13.3.Rel1
(`arm-none-eabi-gcc` in `PATH`) is also supported:

```sh
(cd App/apps/cwtrainer && ./build.sh)
python App/apps/cwtrainer/test/run.py
```

Direct output: `App/apps/cwtrainer/CWTrainer.app`. Host tests require Python 3
and GCC (`HOST_CC` can select the host compiler). `build.sh` regenerates all
assets and headers and enforces 4 KiB, like the other apps. The measured build
uses the same GCC release as the repository Dockerfile.

## Controls

`F + number` means **press F, then the number**. Numbers act once per qualified
press (20 ms); UP/DOWN repeat after 400 ms, then every 100 ms.

| Key | TX FREE | RX TRAIN | RX>TX |
|---|---|---|---|
| 0 | Cycle TX FREE → RX TRAIN → RX>TX | Same | Same |
| 1 | STRAIGHT / IAMB A / IAMB B | Disabled | Select keyer |
| 2 / F+2 | TX WPM +1 / −1 | RX WPM +1 / −1 | RX WPM +1 / −1 |
| 3 / F+3 | TX tone +50 / −50 Hz | RX tone +50 / −50 Hz | RX tone +50 / −50 Hz |
| 4 / F+4 | Disabled | Group length +1 / −1 | Group length +1 / −1 |
| 5 | Swap USB dit/dah | Disabled | Swap USB dit/dah |
| 6 | Clear text | Disabled | Clear answer/correction |
| 7 | Disabled | New group and play | New group and play |
| 8 | Disabled | Cycle GAP | Cycle GAP |
| 9 | Disabled | Reveal | Check answer when keying has stopped |
| SIDE1 | Disabled | Disabled | Replay current target |
| PTT | Straight key in STRAIGHT | Replay selected group, any keyer setting | Straight key in STRAIGHT |
| UP / DOWN | TX WPM | RX WPM | RX WPM |
| EXIT | Leave | Stop exercise; press again to leave | Stop exercise; press again to leave |
| MENU | Reserved | Reserved | Reserved |

PTT without a selected RX target does nothing. Holding PTT, 7 or RX>TX SIDE1 does not
repeatedly trigger it. A new replay press during playback restarts the same
hidden target cleanly. Release PTT after startup/mode changes before using it;
a held PTT cannot launch playback or key across a mode transition. Release
EXIT between aborting an exercise and leaving the app. Changes to settings are
refused during active audio/keying. RX TRAIN KEY/SWAP/CLEAR are hidden and
inert even while receiving; WPM, tone, length, GAP, NEW, REPLAY and reveal remain.

Set **TX** speed/tone in TX FREE before entering RX>TX; set **RX** speed/tone
in either RX mode. GAP is shared by both RX modes and retained on exit. A minus
sign after WPM indicates the F prefix. Defaults: 18 WPM, TX 700 Hz, RX 600 Hz,
length 5, GAP 1×. Configuration uses 12 of the app's 16-byte settings area; older 10-byte settings
retain their RX/TX values, with erased GAP migrated to 1×.

GAP controls the **total silence** between characters: 3, 4.5, 6 or 9 Morse
units. DIT=1, DAH=3 and the intra-character silence=1 remain unchanged. There is
no double-counted trailing element gap. It is simplified learning spacing,
not a full Farnsworth implementation. It does not alter the TX decoder. No
word generator is present; a future implementation would apply the same
factor to the standard seven-unit word silence.

## USB key and local audio

Use only a validated passive adapter: D+/PA12 to tip, D−/PA11 to ring, GND common;
leave VBUS/CC unconnected. Do not connect the key/paddle to a PC or charger.
USB hardware state is restored when the app exits.

In STRAIGHT, PTT and the USB straight contact are combined as logical OR; sound
continues while either is held. In IAMB A/B, PTT keying is ignored so the paddle
retains exclusive ownership of decoded elements. RX TRAIN never uses PTT as
a key. A continuous straight-key mark is limited to ten seconds, then inhibited
until release. Startup/mode/playback changes require 20 ms released contacts.

The existing CW USB engine was adapted into this app's own source; CW USB is
neither modified nor included at build time. Audio uses the resident local-tone
path. Although its DSP is named TX, the tested configuration disables RF PA
gain and calls no TX/PA activation service. RF checks and their practical limits are described above.

## Validation, memory and limitations

**2,489 scenarios pass in each of five variants:** complete mock API, actual
resident audio bodies, and truncated API3 tables of 368/380/384 bytes. This is
one suite repeated in five environments, not 12,445 independent scenarios.
Tests cover STRAIGHT/A/B, A/S/O/C/SOS at 5–60 WPM, audio latencies, counter wrap,
first element after idle, GAP waveforms, controls, decoding, score, correction
tones, LCD framebuffer/SPI refresh, persistence and missing services. An actual
V0.2 negative control still reproduces the old A→ET timing failure.

| Resource | Measured size |
|---|---:|
| Overlay, including BSS | **4,076 / 4,096 bytes** |
| BSS included above | **160 bytes** |
| Code/constants/alignment excluding BSS | **3,916 bytes** |
| Read-only assets | **1,488 / 3,840 bytes** |
| Saved configuration | **12 / 16 bytes** |
| Complete package, including 64-byte header | **5,628 bytes** |

There is **20 bytes of overlay margin**. Compiler/version changes require remeasurement.
Assets are strings and font-derived command bitmaps, never executable code.
25 used API pointers are checked individually; no dependency on a 384-byte-only
service exists. The application is self-contained and independent of PR #699.
Only UV-K5 V3 has been targeted; compatibility with other hardware is not claimed.

Known limits: fixed-WPM manual decoder (no automatic speed adaptation), target
length at most five, exercise answer at most sixteen, TX history keeping the
latest 33–48 characters with whole-line scrolling. The known USB electrical
first-element issue after idle (#683) is not fixed here. Flash/LCD latency can
extend gaps at high speeds. Real stack use and instrumental RF measurements have not been established;
reported hardware trials are described above. See [VALIDATION.md](VALIDATION.md).

## Credits and future direction

- **F4LPF:** idea, functional design, ergonomics and hardware testing.
- **OpenAI Codex:** implementation and automated tests.
- **ChatGPT:** technical assistance, design and contribution preparation.
- CW USB / CW Keyer engine and USB handling adapted from work by **Armel F4HWN,
  Johan Denoyer F4WAT and Sébastien F4LPF**; resident Fusion services/fonts remain
  unchanged. Existing copyright notices and Apache-2.0 licensing are retained
  (see the repository's `LICENSE`).

This is the first generation. Possible future work may use three independent
overlay apps: **CW Trainer RX** (Koch/Farnsworth/groups/callsigns), **CW Trainer TX**
(keying/decoding), and **CW Trainer Virtual QSO** (normal QSO and contest RUN/S&P).
No split is implemented, and no schedule or delivery commitment is implied.
