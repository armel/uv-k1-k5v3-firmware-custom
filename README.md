# Quansheng UV-K1 / UV-K5 V3 — CAT Edition (F4HWN 6.0.0 Base)

> [!WARNING]
> **IMPORTANT WARNING / DISCLAIMER**  
> **Install and use this firmware entirely at your own risk!**  
> Firmware modifications carry the inherent risk of device malfunction or bricking. The authors and contributors assume no responsibility or liability for any hardware damage, data loss, or violation of local radio communications laws and regulations. Before flashing, it is strongly recommended to back up your radio's calibration data (e.g. using UV Studio).

> [!IMPORTANT]
> **Installation & Flashing:**  
> To install this firmware, download the **`f4hwn.cat.bin`** file (located in the main folder of this repository) and flash it to your radio using **[UV Studio](https://armel.github.io/uvstudio/#dump-calib)**. Remember to back up your calibration data first via the [Dump Calibration tool](https://armel.github.io/uvstudio/#dump-calib) before flashing!

---

## Project Overview

This project is based on the official **[F4HWN custom firmware](https://github.com/armel/uv-k1-k5v3-firmware-custom)** (v6.0.0 for the **PY32F071** microcontroller).

It is extended with a dedicated **CAT** variant (`f4hwn.cat` / preset `CAT`), which introduces:
- **Extended Kenwood CAT Protocol** for full remote transceiver control from a computer (UART / USB VCP).
- **Asynchronous hardware scanner** and background RSSI measurements (BK4819) without blocking the radio user interface.
- **Default settings optimized for PC/CAT operation**:
  - **Battery Saver disabled** (`BATTERY_SAVE = 0`) — the receiver does not enter cyclic sleep mode, ensuring instantaneous response to RF signals and CAT commands.
  - **Single VFO mode** (`DUAL_WATCH = DUAL_WATCH_OFF`) — prevents the radio from switching bands in the background during remote control.
  - Standard UART baud rate: **38400 baud** (8N1).
- **Multiboot support**: 100% native support for the **multiboot** mechanism is preserved (UART commands `0x0720`–`0x0728` and flash partitioning). This enables safe and convenient firmware changes, rolling back to previous releases, or flashing multiple firmware versions.

---

## CAT Commands Quick Reference

All commands are transmitted as ASCII text strings over the serial port and must be terminated with a semicolon (`;`).

| Command | Example / Format | Response | Description |
|---|---|---|---|
| **FA** | `FA;`<br>`FA00145000000;` | `FA[11 digits Hz];`<br>*(none)* | **VFO A Frequency**: read (without argument) or set frequency (11 digits in Hz, e.g. `00145000000;` = 145.000 MHz). |
| **FB** | `FB;`<br>`FB00433000000;` | `FB[11 digits Hz];`<br>*(none)* | **VFO B Frequency**: read or set VFO B frequency (11 digits in Hz). |
| **FR** | `FR0;`<br>`FR1;` | *(none)* | **Active VFO Selection**: `FR0;` switches to VFO A, `FR1;` switches to VFO B. |
| **TX** | `TX;` | *(none)* | **Force Transmit (PTT ON)**: switches the transceiver to transmit mode. |
| **RX** | `RX;` | *(none)* | **Return to Receive (PTT OFF)**: stops transmitting and returns to receive mode. |
| **MO** | `MO1;`<br>`MO0;` | *(none)* | **Monitor (Open Squelch)**: `MO1;` opens squelch, `MO0;` restores normal squelch operation. |
| **MD** | `MD;`<br>`MD4;` / `MD5;` / `MD2;` | `MD[mode];`<br>*(none)* | **Modulation Mode**: `4` = FM, `5` = AM, `2` = USB. Without parameter, returns active VFO's current modulation. |
| **PC** | `PC[0-7];` e.g. `PC6;` | *(none)* | **Transmitter Output Power**: `0`=Low1, `1`=Low2, `2`=Low3, `3`=Low4, `4`=Low5, `5`=Mid, `6`=High, `7`=User. |
| **OF** | `OF;` | *(none)* | **Disable Subtones**: turns off CTCSS/DCS on active VFO. |
| **CT** | `CT0885;`<br>`CT0670;` | *(none)* | **Set CTCSS Tone**: 4 digits in tenths of Hz (e.g. `0885` = 88.5 Hz, `0670` = 67.0 Hz). |
| **DT** | `DT023;`<br>`DT047;` | *(none)* | **Set DCS Code**: 3 digits in octal notation (e.g. `023`, `047`). |
| **SQ** | `SQ[0-9];` e.g. `SQ5;` | *(none)* | **Squelch Level**: from `SQ0;` (open) to `SQ9;`. |
| **OS** | `OS0;` / `OS1;` / `OS2;` | *(none)* | **Shift Direction (Offset)**: `0` = none (simplex), `1` = positive offset (+), `2` = negative offset (-). |
| **OV** | `OV00000600000;` | *(none)* | **Frequency Offset Value**: 11 digits in Hz (e.g. `00000600000;` = 600 kHz). |
| **IF** | `IF;` | `IF[freq11]00000[mod][tx];` | **Transceiver Status**: returns current frequency, modulation, and TX/RX status. |
| **S1** | `S1;` | `S1,[vfo],[dbm],[sq];` | **Instant RSSI Measurement**: immediate signal strength (dBm) and squelch status (0/1) for active VFO. |
| **SM** | `SM[freq11];` | `SM[freq11],[dbm],[sq];` | **Fast Spot Frequency Measurement**: measures signal level on specified frequency without permanently altering VFO settings. |
| **RD** | `RD1;`<br>`RD0;` | Asynchronously:<br>`RR[vfo],[dbm];` | **Auto RSSI Reporting**: `RD1;` enables periodic `RR` packets every ~200 ms (or upon signal level change), `RD0;` disables. |
| **SL** | `SL[idx2][freq11];` | `SL_OK;` | **Scanner List Definition**: writes frequency into scanner list cell (indices `00` to `24`, up to 25 channels). |
| **SC** | `SC[count2];` e.g. `SC03;` | Asynchronously:<br>`SR,[dbm],[sq],...;` | **Start Hardware Scanner**: asynchronously measures defined channels and returns complete results vector. |
| **SCF** | `SCF[freq11][,ticks];` | Asynchronously:<br>`SQ[freq],[sq],[dbm],[noise],[glitch];` | **Single Hardware Measurement (SCF)**: detailed measurement of signal strength, noise, and glitches directly from BK4819. |
| **DTMF** | *(automatic)* | Asynchronously:<br>`RD[char],[dbm];` | **DTMF Tone Reporting**: when a DTMF character is received, the radio automatically sends an `RD` packet with the decoded character and RSSI level. |

### Testing and Diagnostics
A Python script is provided to test and demonstrate all CAT commands:
```powershell
python tools\cat_tester.py COM16 38400
```

---

# Stats

![Alt](https://repobeats.axiom.co/api/embed/ecdd86aa536b716f088339a0c5ee734558f78c28.svg "Repobeats analytics image")

# F4HWN firmware port for the UV-K1 and UV-K5 V3 using the PY32F071 MCU

This repository is a fork of the [F4HWN custom firmware](https://github.com/armel/uv-k5-firmware-custom), who was a fork of [Egzumer custom firmware](https://github.com/egzumer/uv-k5-firmware-custom). It extends the work done for the UV-K5 V1, based on the DP32G030 MCU, and adapts it to the newer UV-K1 and UV-K5 V3 built around the PY32F071 MCU. It is the result of the joint work of [@muzkr](https://github.com/muzkr) and [@armel](https://github.com/armel).

A big thanks to DualTachyon, who paved the way by releasing the very first open-source [firmware](https://github.com/DualTachyon/uv-k5-firmware) for the UV-K5 V1. None of this would have been possible without that initial work !

# A note for developers who intend to fork this project

This firmware is distributed under the Apache 2.0 License, carrying forward the original copyright of DualTachyon, whose work laid the foundation for the UV-K5 open-source ecosystem.
If you create a fork or a derived version, **we strongly encourage you to keep your work open source**.

Keeping your fork open:

- aligns with the intent and spirit of the Apache 2.0 License
- supports the amateur-radio and embedded-development community
- avoids unnecessary fragmentation
- allows others to study, audit and improve the firmware

It is also very much in line with the **ham spirit**: sharing knowledge, experimenting together and helping each other, rather than closing things off or claiming them as your own.

Maintaining an open-source fork is the best way to help build a healthy and sustainable ecosystem for everyone.

> [!WARNING]
> EN - THIS FIRMWARE HAS NO REAL BRAIN. PLEASE USE YOUR OWN. Use this firmware at your own risk (entirely). There is absolutely no guarantee that it will work in any way shape or form on your radio(s), it may even brick your radio(s), in which case, you'd need to buy another radio.
Anyway, have fun.
>
> _FR - CE FIRMWARE N'A PAS DE VÉRITABLE CERVEAU. VEUILLEZ UTILISER LE VÔTRE. Utilisez ce firmware à vos risques et périls. Il n'y a absolument aucune garantie qu'il fonctionnera d'une manière ou d'une autre sur votre (vos) radio(s), il peut même bousiller votre (vos) radio(s), dans ce cas, vous devrez acheter une autre radio. Quoi qu'il en soit, amusez-vous bien._

> [!NOTE]
> EN - About CHIRP, as with many other firmwares, you need to use a dedicated driver. The matching CHIRP driver is now bundled with each release of this repository, so you can download the firmware and its driver together from the [Releases page](https://github.com/armel/uv-k1-k5v3-firmware-custom/releases).
>
> _FR - A propos de CHIRP, comme pour beaucoup d'autres firmwares, vous devez utiliser un pilote dédié. Le driver CHIRP correspondant est désormais fourni avec chaque release de ce dépôt, ce qui permet de récupérer ensemble le firmware et son pilote depuis la page des [Releases](https://github.com/armel/uv-k1-k5v3-firmware-custom/releases)._

> [!CAUTION]
> EN - I recommend backing up your calibration data with [UV Studio](https://armel.github.io/uvstudio/#dump-calib) immediately after flashing this firmware. It is a good habit to adopt.
>
> _FR - Je recommande de sauvegarder vos données de calibration avec [UV Studio](https://armel.github.io/uvstudio/#dump-calib) juste après avoir flashé ce firmware. C'est un bon réflexe à adopter._

# Donations

Special thanks to Jean-Cyrille F6IWW (3 times), Fabrice 14RC123, David F4BPP, Olivier 14RC206, Frédéric F4ESO, Stéphane F5LGW (2 times), Jorge Ornelas (4 times), Laurent F4AXK, Christophe Morel, Clayton W0LED, Pierre Antoine F6FWB, Jean-Claude 14FRS3306, Thierry F4GVO, Eric F1NOU, PricelessToolkit, Ady M6NYJ, Tom McGovern (4 times), Joseph Roth, Pierre-Yves Colin, Frank DJ7FG, Marcel Testaz, Brian Frobisher, Yannick F4JFO, Paolo Bussola, Dirk DL8DF, Levente Szőke (2 times), Bernard-Michel Herrera, Jérôme Saintespes, Paul Davies, RS (3 times), Johan F4WAT, Robert Wörle, Rafael Sundorf, Paul Harker, Peter Fintl, Pascal F4ICR (2 times), Mike DL2MF (3 times), Eric KI1C / F4WFS (3 times), Phil G0ELM, Jérôme Lambert, Eliot Vedel, Alfonso EA7KDF, Jean-François F1EVM, Robert DC1RDB (2 times), Ian KE2CHJ, Daryl VK3AWA, Roberto Brunelli, Robert Boardman, Stephen Oliver, Nicolas F4INE, William Bruno, Daniel OK2VLK, Tayler Chew, Peter DL7RFP, Philippe Kopp, Rune LA6YMA, Jeremy Luna, Steef Wagenaar (2 times), Zhuo BG7SGA, Jamie M0JLB, Antoine LIBERT, Vince K0DKR, Julia DF7JA, Ken 2E0UMK, Victor TI2SYS, Tobi DG9LAY, Deaglan K4DFQ, Catherine PALMER, Brian WA6JFK, Stéphane Hintzy, Roger F1HCN, Marcin Kusaj, Flavio Cottarelli, Bob N1MLZ, Carlos EA1IJ, Brian M7YLF, Giuseppe IT9LLH, 邓 月 and Jon M1JRH for their [donations](https://www.paypal.com/paypalme/F4HWN). That’s so kind of them. Thanks so much 🙏🏻

## Table of Contents

* [Project Overview](#project-overview)
* [CAT Commands Quick Reference](#cat-commands-quick-reference)
* [Main features and improvements from F4HWN](#main-features-and-improvements-from-f4hwn)
* [Main Features from Egzumer](#main-features-from-egzumer)
* [Manual](#manual)
* [Compiling and Building from Docker](#compiling-and-building-from-docker)
* [Flashing the Firmware with UV Studio](#flashing-the-firmware-with-uv-studio)
* [Credits](#credits)
* [Other sources of information](#other-sources-of-information)
* [License](#license)

## Main features and improvements from F4HWN

### Fusion edition

Fusion is the generic reference edition for the UV-K1 and UV-K5 V3. It is intended
for everyday use and is the base inherited by the specialized editions. It includes:

- Fagci's spectrum analyzer,
- broadcast FM radio and VOX,
- [UV Studio](https://armel.github.io/uvstudio/) with integrated K5Viewer screen mirroring, screenshots and remote keyboard control,
- advanced RX audio profiles and Audio Scope,
- automatic RX/TX activity logging with RF Log,
- multiboot support.

Specialized presets extend Fusion for specific uses:

- **Transfer** adds AirCopy and BEAM wireless channel transfer.
- **FieldOps** adds first-responder controls, Fox Hunt and Morse Beacon support.
- **Labs** is the experimental edition. It carries the broad feature selection of the
  other releases and adds the overlay-apps platform (apps loaded from external Flash and
  run in a 4 KiB RAM overlay) — the newest, least-settled work. Expect rough edges. It is
  not a strict superset of every other edition: features may be exchanged between releases
  to preserve stability and memory headroom.
- **Custom** remains a manually configured build based directly on the hidden technical default.

### Radio and signal handling

- Reworked output-power levels:
  - `Low 1`: below approximately 20 mW,
  - `Low 2`: approximately 125 mW,
  - `Low 3`: approximately 250 mW,
  - `Low 4`: approximately 500 mW,
  - `Low 5`: approximately 1 W,
  - `Mid`: approximately 2 W,
  - `High`: approximately 5 W,
  - `User`: configurable through `SetPwr`.
- S-meter calibrated according to the [IARU Region 1 recommendation for VHF/UHF](https://hamwaves.com/decibel/en/):
  - fixed S0 to S9+ values replace the former EEPROM S-meter thresholds,
  - Classic and Tiny display styles are available.
- Configurable 12.5 kHz or 6.25 kHz narrow-FM bandwidth.
- Per-channel TX lock.
- Adjustable RX audio volume.
- Advanced RX audio profiles for FM and AM reception.
- Regional frequency-lock profiles for amateur bands, PMR446, FRS, GMRS and MURS.
- Support for 1600, 2200 and 3500 mAh battery profiles.

### Spectrum analyzer

- Channel names displayed in the spectrum view.
- Persistent spectrum settings.
- Faster and smoother spectrum rendering.
- Improved behavior when freezing the spectrum or disconnecting USB-C.
- Spectrum analyzer state can be restored automatically at startup.

### Scanning

- Support for up to 24 named scan lists.
- Each memory channel can be assigned to:
  - `OFF`,
  - one scan list from `01` to `24`,
  - `ALL`.
- The `ALL` list scans every channel except those assigned to `OFF`.
- Automatic selection of the next valid list when the requested list is empty.
- Direct scan-list selection while scanning:
  - `00` selects `ALL`,
  - `01` to `24` select the corresponding list.
- Long press on `MENU` while scanning to exclude the current memory channel.
- Up to 64 frequency exclusions.
- Very fast scanning mode, reaching approximately 150 frequencies per second.
- Scan progress, RSSI and detected CTCSS/DCS information.
- Configurable scan resume behavior.
- Scan state can be restored automatically at startup.

### User interface

- Improved VFO screen with:
  - Classic and Tiny S-meter styles,
  - Classic and Tiny frequency-information layouts,
  - `MAIN ONLY`, `DUAL` and `CROSS` display modes,
  - RX activity indication on the active VFO,
  - optional RX LED blinking,
  - squelch, monitor, step and CTCSS/DCS information,
  - last-RX indication,
  - RX and TX timers.
- Improved status bar with updated fonts and icons.
- Menu index remains visible while editing an entry.
- Improved frequency and memory-channel input.
- Improved audio-level display.
- AirCopy progress percentage and gauge.
- Smooth backlight fading.
- Manual backlight controls for quickly switching between minimum and maximum brightness.
- Configurable contrast and inverted-display mode.
- Configurable navigation layout for the different radio models.
- Improved power-on message and optional startup logo.
- System-information pages for:
  - firmware version and build information,
  - battery information,
  - Flash and SRAM usage,
  - project and documentation QR codes.

### Audio and transmission controls

- Classic and OnePush PTT modes.
- Configurable timeout-timer alerts:
  - disabled,
  - sound,
  - visual,
  - sound and visual.
- Configurable end-of-transmission alerts using the same modes.
- Audio Scope during RX and TX.
- Improved Audio Scope behavior with OnePush PTT, DTMF and the 1750 Hz tone.
- Configurable automatic deep-sleep timeout.
- Quick actions for:
  - RX mode,
  - main-VFO-only display,
  - PTT,
  - wide/narrow bandwidth,
  - 1750 Hz tone,
  - mute,
  - RX audio profile,
  - maximum power,
  - offset removal.

### Fox Hunt and Beacon

- Dedicated Fox Hunt receiver with:
  - calibrated S-meter display,
  - scrolling signal-history graph,
  - peak, minimum and trend indicators,
  - selectable RF attenuation,
  - silent, Geiger-style and received-audio modes,
  - long-press `F` keypad lock (attenuation stays adjustable with the arrow keys).
- Independent Morse Beacon transmitter with:
  - `MOE`, `MOI`, `MOS`, `MOH`, `MO5` and `MO` identifiers,
  - optional callsign identification,
  - configurable TX and idle periods,
  - live TX and idle countdowns,
  - persisted Beacon settings,
  - interactive control during transmission,
  - shared long-press `F` keypad lock,
  - TX-lock, modulation and battery-safety checks.
- Fox Hunt and Beacon screens are mirrored to UV Studio's integrated K5Viewer.

### RF Log

- Automatic logging of RX and TX activity when RF Log is enabled.
- Logs stored in the radio's external Flash memory.
- Recorded information includes:
  - RX or TX direction,
  - frequency and channel,
  - channel name,
  - activity duration,
  - S-meter level,
  - battery voltage.
- On-radio history with RX/TX filtering and detailed views.
- Live RF Log dashboard and history access through UV Studio.

### Connectivity and data transfer

- Live screen streaming to [UV Studio](https://armel.github.io/uvstudio/) over a USB serial connection.
- Screenshot capture and download.
- Remote keyboard control through the integrated K5Viewer.
- Automatic reconnection after a USB disconnect.
- RF Log monitoring, analytics and CSV export.
- Firmware flashing, calibration backup and restore, and boot-logo management from the same interface.
- BEAM transfer of complete channel settings between compatible radios.
- Improved AirCopy interface and progress reporting.

### Settings and controls

- New or extended menu entries:
  - `SetPwr`: configurable User output power,
  - `SetPTT`: Classic or OnePush PTT,
  - `SetTOT`: timeout-timer alert,
  - `SetEOT`: end-of-transmission alert,
  - `SetCtr`: display contrast,
  - `SetInv`: inverted display,
  - `SetLck`: keypad or keypad-and-PTT lock,
  - `SetMet`: S-meter style,
  - `SetGUI`: VFO information style,
  - `SetRxA`: RX audio profile,
  - `SetTmr`: RX and TX timers,
  - `SetOff`: automatic deep-sleep delay,
  - `SetNFM`: narrow-FM bandwidth,
  - `SetVol`: RX audio volume,
  - `SetScn`: scan mode,
  - `SetNav`: radio-specific navigation layout.
- Improved `PonMsg`, `BackLt`, `TxTOut`, `ScnRev` and `KeyLck` menus.
- Full VFO state restoration with a long press on `EXIT`.
- Squelch changes made with `F + UP` or `F + DOWN` are persisted.

### Keyboard shortcuts and assignable actions

- `F + UP` or `F + DOWN`: adjust the squelch level.
- `F + F1` or `F + F2`: adjust the frequency step.
- `F + 8`: temporarily switch the backlight between minimum and maximum brightness.
- `F + 9`: return to the configured backlight strategy.
- Configurable short- and long-press actions include:
  - RX mode,
  - main-VFO-only display,
  - virtual PTT,
  - wide/narrow bandwidth,
  - 1750 Hz tone,
  - mute,
  - RX audio profile,
  - maximum power,
  - offset removal,
  - BEAM,
  - RF Log,
  - Fox Hunt,
  - Beacon.

### Reliability and optimization

- Improved squelch and S-meter behavior.
- Fixed DTMF overlay issues.
- Fixed scan-range limits.
- Cleaner startup display.
- Removed PWM-related audio noise.
- Improved serial and K5Viewer key handling.
- Improved VFO persistence and restoration.
- Extensive code refactoring and memory optimization.
- DTMF calling and the scrambler remain disabled in Fusion.
- Legacy AM Fix support has been removed.

## Main features from Egzumer:
* many of OneOfEleven mods:
   * long press buttons functions replicating F+ action
   * fast scanning
   * channel name editing in the menu
   * channel name + frequency display option
   * shortcut for scan-list assignment (long press `5 NOAA`)
   * scan-list toggle (long press `* Scan` while scanning)
   * configurable button function selectable from menu
   * battery percentage/voltage on status bar, selectable from menu
   * longer backlight times
   * mic bar
   * RSSI s-meter
   * more frequency steps
   * squelch more sensitive
* fagci spectrum analyzer (**F+5** to turn on)
* some other mods introduced by me:
   * SSB demodulation (adopted from fagci)
   * backlight dimming
   * battery voltage calibration from menu
   * better battery percentage calculation, selectable for 1600mAh or 2200mAh
   * more configurable button functions
   * long press MENU as another configurable button
   * better DCS/CTCSS scanning in the menu (`* SCAN` while in RX DCS/CTCSS menu item)
   * Piotr022 style s-meter
   * restore initial freq/channel when scanning stopped with EXIT, remember last found transmission with MENU button
   * reordered and renamed menu entries
   * LCD interference crash fix
   * many others...

 ## Manual

Up to date manual is available in the [Wiki section](https://github.com/armel/uv-k1-k5v3-firmware-custom/wiki)

## Radio performance

Please note that the Quansheng UV-Kx radios are not professional quality transceivers, their
performance is strictly limited. The RX front end has no track-tuned band pass filtering
at all, and so are wide band/wide open to any and all signals over a large frequency range.

Using the radio in high intensity RF environments will most likely make reception difficult,
especially in AM mode. The receiver simply does not have a great dynamic range, so stronger
signals can easily cause distortion, desensitization and poor AM audio.
This is fundamentally a hardware limitation: firmware can improve behavior at the margins, but
it cannot overcome the front-end design of the radio.
In practice, AM reception will degrade first and most severely, while FM reception is generally
more tolerant and should remain more usable.

But, they are nice toys for the price, fun to play with.

## Compiling and Building from Docker

This project provides a Docker-based build system for the UV-K1 and UV-K5 V3.
Everything is handled through the `compile-firmware.sh` helper script. Fusion is
the default generic preset, while specialized builds are generated in their own
`build/<Preset>` directories.

### Prerequisites

- Docker installed on your system
- Bash environment (Linux, macOS, WSL, Git Bash on Windows)

### Build Script Overview

The script `compile-firmware.sh`:

1. Builds the Docker image (`uvk1-uvk5v3`) if it does not already exist.
2. Configures the selected preset with `cmake --fresh`.
3. Builds the firmware and outputs matching `.elf`, `.bin` and `.hex` files.
4. Displays Flash and RAM usage; `All` keeps the individual build logs quiet.

### Usage

```bash
./compile-firmware.sh [Preset] [extra CMake options]
```

The default preset is **Fusion**. Available presets are:

- **Custom**
- **Fusion**
- **Transfer**
- **FieldOps**
- **Labs**
- **Max**
- **CAT** (Kenwood CAT remote control edition)
- **All** (Fusion, Transfer, FieldOps, Labs, Max, and CAT)

Examples:

```bash
./compile-firmware.sh
./compile-firmware.sh CAT
./compile-firmware.sh Fusion
./compile-firmware.sh Transfer
./compile-firmware.sh FieldOps
./compile-firmware.sh Labs
./compile-firmware.sh All
```

### Passing Additional CMake Options

You can pass extra configuration options after the preset name.  
These are forwarded directly to `cmake --preset` inside the container.

Examples:

```bash
./compile-firmware.sh FieldOps -DENABLE_VOX=OFF
./compile-firmware.sh Fusion -DENABLE_FEAT_F4HWN_GAME=ON
./compile-firmware.sh Fusion -DSQL_TONE=600
```

To prepare the rolling development firmware:

```bash
./compile-firmware.sh Fusion -DDEV=ON
```

This keeps the regular build output in `build/Fusion` and also updates
`archive/f4hwn.fusion.development.bin`. The development build is identified as
`DEV` in the firmware information screen. Publishing the updated archive file
remains an explicit Git operation.

### Notes

- The first run may take a few minutes while Docker builds the base image.
- Each build runs inside Docker, so your host environment remains clean.

## Flashing the Firmware with UV Studio

You can flash the UV-K5 V3 and UV-K1 directly from your web browser using the Web Serial-based [UV Studio](https://armel.github.io/uvstudio/#dump-calib).

UV Studio combines firmware flashing, calibration maintenance, boot-logo management, K5Viewer and RF Log in a single interface. It requires no application installation, server or account. Use a desktop browser with Web Serial support, such as Chrome, Brave, Edge, Opera or Firefox 151+.

> [!IMPORTANT]
> **Flashing the CAT Firmware:**
> 1. Download the **`f4hwn.cat.bin`** file (available in the root of this repository or in `build/CAT/`).
> 2. Open [UV Studio](https://armel.github.io/uvstudio/#dump-calib).
> 3. **Back up your calibration first!** Go to [Dump Calibration](https://armel.github.io/uvstudio/#dump-calib) with the radio in normal mode and download your `calibration.dat` file.
> 4. Put your radio into **DFU mode (flash mode)** (hold PTT while turning the power knob on).
> 5. Load and flash the downloaded **`f4hwn.cat.bin`** file using UV Studio.

## Steps to flash the firmware

- Download the **`f4hwn.cat.bin`** file.
- Open [UV Studio](https://armel.github.io/uvstudio/#dump-calib) in your browser.
- Connect your radio to your computer using a compatible USB programming cable (USB-C or Baofeng/Kenwood style double-jack USB cable).
- Make sure your radio is in **DFU mode (flash mode)** (power on while holding PTT).
- Load your local **`f4hwn.cat.bin`** firmware file.
- Click on `Flash Firmware`, then select the serial port associated with your radio.
- The progress bar will guide you through the flashing steps.

Once finished, your radio restarts with the new firmware.

## Steps to dump or restore calibration data

[UV Studio](https://armel.github.io/uvstudio/) can also dump and restore calibration data, which is highly recommended. It is best to create a dump immediately after installing the F4HWN firmware, and to restore it before installing another firmware or returning to the stock firmware.

### Dump

- Open the [Dump Calibration](https://armel.github.io/uvstudio/#dump-calib) view in UV Studio.
- Power on your radio in **normal mode**.
- Click `Dump Calibration Data`.

When the process is complete, click `Download calibration.dat` to save the file to your computer.

> [!NOTE]
> A good practice is to rename your calibration file using the serial number of your radio, which you can find on the label on the back of the device once you remove the battery. This helps avoid mixing up calibration files when you own multiple units.

### Restore

- Open the [Restore Calibration](https://armel.github.io/uvstudio/#restore-calib) view in UV Studio.
- Power on your radio in **normal mode**.
- Select your `calibration.dat` file on your computer.

Click `Restore Calibration Data` and wait until the process fully completes.

## Other sources of information

- [k1-teardown](https://github.com/armel/k1-teardown) 

## Credits

Many thanks to various people:

* [Muzkr](https://github.com/muzkr)
* [Mrkusypl](https://github.com/mrkusypl)
* [Andrej](https://github.com/Tunas1337)
* [Egzumer](https://github.com/egzumer)
* [OneOfEleven](https://github.com/OneOfEleven)
* [DualTachyon](https://github.com/DualTachyon)
* [Mikhail](https://github.com/fagci)
* [Manuel](https://github.com/manujedi)
* @wagner
* @Lohtse Shar
* [@Matoz](https://github.com/spm81)
* @Davide
* @Ismo OH2FTG
* [OneOfEleven](https://github.com/OneOfEleven)
* @d1ced95
* and others I forget

## License

Copyright 2023 Dual Tachyon
https://github.com/DualTachyon

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
