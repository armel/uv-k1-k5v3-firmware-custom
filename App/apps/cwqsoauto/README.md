# CW QSO Auto

**Experimental QNM1 named-message branch:** see [FORMAT-NAMES.md](FORMAT-NAMES.md)
for the current format, build/tests and controls. The PR release documentation
below describes the older archived binary, not the new embedded experimental APP.

Standalone Morse overlay application for compatible F4HWN UV-K5 V3 firmware
(ABI 1, API level 3). The contribution targets the `v6.3.0` app archive.
Hardware validation reported by the author covers **UV-K5 V3**, not UV-K1.

[Notice française](README.fr.md) · [Architecture](ARCHITECTURE.md) ·
[Tests and hardware status](TESTING.md) · [Offline editor](editor.html)

## Personalize and install

1. Download `editor.html` and open the **local file** in a recent Chrome, Edge
   or Firefox browser. Opening an HTML source preview on GitHub is not enough.
   The single HTML contains the exact reference `.app`; no separate application
   file, server, account, library download or network connection is needed.
2. Replace **F4XXX** with your own callsign in messages 01 and 02; replace
   **SEB** with your name where required. These are initial values, not runtime
   identity settings or restrictions on personalized text. Review all messages
   before an actual QSO.
3. Edit any of the 16 fields. Allowed text is uppercase `A–Z`, `0–9`, space,
   `/`, `?`, and the exact token `{RST}`. Each field has **64 encoded bytes**:
   `{RST}` occupies one byte. Empty messages disable that message. Leading,
   trailing and doubled spaces are rejected. No automatic uppercasing,
   transliteration or truncation is performed.
4. Click **Vérifier et télécharger**. Keep `CWQSOAuto-personnalise.app` as your
   backup. Importing it into this same HTML restores all 16 texts. Closing the
   page does not automatically save work; it does not read messages back from
   the radio.
5. Exit any running radio app, connect the radio to the computer, and open
   [UV Studio](https://armel.github.io/uvstudio/). In its Applications / Apps
   view, connect to the radio's serial port, choose the local `.app` file, and
   install it using UV Studio's application controls. Use a compatible
   F4HWN firmware; this is an **application**, not a firmware `.bin` to flash.
   Disconnect the computer before using the USB-C paddle adapter.
6. Open **CW QSO Auto** in the radio's Apps menu. Verify the selected message,
   your callsign, speed and frequency, then test the controls below.

The unpersonalized reference is also distributed separately as
[`archive/apps/v6.3.0/CWQSOAuto.app`](../../../archive/apps/v6.3.0/CWQSOAuto.app).
It is byte-identical to the embedded file before any edit.

The editor also accepts the previous exact Bank16 stable, nav16 and preview16
profiles. **An imported old file keeps its old executable and controls; import
is not an upgrade.** To move old messages to this version, open two editor tabs,
load the old compatible file in one, keep the embedded reference in the other,
copy the desired texts, then download from the reference tab. Loading a fresh
default app into the radio does not preserve previously personalized messages.

## Radio controls

| Control | Main QSO screen |
| --- | --- |
| UP / DOWN | Circular selection of messages 01–16 |
| PTT | Send the selection once; a held PTT does not repeat or queue another send |
| SIDE 1 | Start message 01 immediately, then repeat with a three-second pause after each completed message |
| SIDE 2 | Stop repetition; an already started message finishes normally; a paused repetition sends nothing further |
| EXIT / paddle | Cancel automatic transmission and return to RX through the normal shutdown path; release controls before another request |
| Digits | Shift the three displayed RST digits; initial RST is `599` at app entry |
| MENU | Open CW SETTINGS |

SIDE 1 presses during repetition do not accumulate requests. PTT during the
pause sends the current selection and ends repetition. Messages 15 (`AGN?`)
and 16 (`?`) are now reached by normal navigation/PTT, not SIDE shortcuts.
The first three selections keep their fixed QSO labels; messages 04–16 show a
bounded content preview. `*` represents an RST token and `>` indicates more
text; the stored/transmitted message is not truncated.

There is no five-second arming gesture or LOCK/READY indicator. Controls
already held at entry are ignored until a qualified release. Manual STRAIGHT,
AUTO, IAMB A and IAMB B operation is retained. The USB-C paddle uses TIP/DIT
and RING/DAH contacts, with reversal available in settings.

CW SETTINGS keeps its existing controls: `1` STRAIGHT/PADDLE, `2` WPM (5–60),
`3` receive/sidetone pitch, `4` break-in, `5` paddle reversal, `6` crystal
correction, `8` AUTO/A/B, `0` speaker, UP/DOWN frequency tuning. `F` selects
decrease for the next numeric setting adjustment. MENU or EXIT returns from
settings; EXIT from idle QSO leaves the app. RST is separate from these saved
CW settings.

## Build and integration

From a complete checkout of the firmware repository:

```sh
./compile-app.sh cwqsoauto
```

The existing script discovers `App/apps/cwqsoauto/build.sh` automatically and
copies the output to `build/Apps/`. No resident menu registration, application
list change or firmware rebuild is introduced. The prebuilt archive file is
in the same versioned directory as the other official app artifacts; catalogue
discovery can use that directory. No changes to UV Studio are bundled here.

For a native build, set `ARM_CC` to **GNU Arm GCC 13.3.Rel1** and run:

```sh
python3 App/apps/cwqsoauto/build.py
```

The validated result is **4,092 / 4,096 overlay bytes, including 92 B BSS**,
plus **2,282 B assets**; margin is only **4 B**. The `.app` file is 6,438 B.
`app.ld` enforces the real overlay limit; build warnings are errors. Other
compiler versions are not promised to produce the same size or binary.
`release.json` records the approved source, compiler and binary hashes.

## Design and provenance

Messages and the Morse table are Flash assets; the overlay only reads them.
The PC editor changes message payloads and the asset CRC without recompiling
or modifying executable bytes. It neither writes radio Flash directly nor
uses an on-radio configuration editor.

The integrated first-DIT correction prepares silent Tone1, selects AF_ALAM,
and enables the speaker path **before the existing 2 ms PLL wait**. The
calibration cycle, RF ramps and RX restoration remain intact. There is no
extra 10/16 ms preamble, event queue or delayed Morse engine. Early speaker
activation improves the first local tone, with a very light switching click
reported during development; no click-free acoustic guarantee is claimed.

Size optimizations share launch/deadline/control paths and six small
Cortex-M0+ Thumb API wrappers. ABI offsets are compile-time checked; wrappers
retain the resident API boundary and have dedicated linked-instruction tests.
The existing AUTO/A/B paddle memory and RF timing remain covered by the
regression suites. See the architecture and testing documents for limits.

This contribution follows the repository's existing Apache-2.0 terms and
retains the original Armel/F4HWN and Johan Denoyer/F4WAT source attributions.
The root `LICENSE` and `NOTICE` are unchanged; no new license is introduced.
