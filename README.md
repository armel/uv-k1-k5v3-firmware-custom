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
> EN - I recommend to backup your calibration data with [uvtools2](https://armel.github.io/uvtools2/) just after flashing this firmware. It's a good reflex to have. 
>
> _FR - Je recommande de sauvegarder vos données de calibration avec [uvtools2](https://armel.github.io/uvtools2/) juste après avoir flashé ce firmware. C'est un bon réflexe à avoir._

# Donations

Special thanks to Jean-Cyrille F6IWW (3 times), Fabrice 14RC123, David F4BPP, Olivier 14RC206, Frédéric F4ESO, Stéphane F5LGW (2 times), Jorge Ornelas (4 times), Laurent F4AXK, Christophe Morel, Clayton W0LED, Pierre Antoine F6FWB, Jean-Claude 14FRS3306, Thierry F4GVO, Eric F1NOU, PricelessToolkit, Ady M6NYJ, Tom McGovern (4 times), Joseph Roth, Pierre-Yves Colin, Frank DJ7FG, Marcel Testaz, Brian Frobisher, Yannick F4JFO, Paolo Bussola, Dirk DL8DF, Levente Szőke (2 times), Bernard-Michel Herrera, Jérôme Saintespes, Paul Davies, RS (3 times), Johan F4WAT, Robert Wörle, Rafael Sundorf, Paul Harker, Peter Fintl, Pascal F4ICR (2 times), Mike DL2MF (3 times), Eric KI1C (2 times), Phil G0ELM, Jérôme Lambert, Eliot Vedel, Alfonso EA7KDF, Jean-François F1EVM, Robert DC1RDB (2 times), Ian KE2CHJ, Daryl VK3AWA, Roberto Brunelli, Robert Boardman, Stephen Oliver, Nicolas F4INE, William Bruno, Daniel OK2VLK, Tayler Chew, Peter DL7RFP, Philippe Kopp, Rune LA6YMA, Jeremy Luna, Steef Wagenaar (2 times), Zhuo BG7SGA, Jamie M0JLB, Antoine LIBERT, Vince K0DKR, Julia DF7JA, Ken 2E0UMK, Victor TI2SYS, Tobi DG9LAY, Deaglan K4DFQ, Catherine PALMER, Brian WA6JFK and Stéphane Hintzy for their [donations](https://www.paypal.com/paypalme/F4HWN). That’s so kind of them. Thanks so much 🙏🏻

## Table of Contents

* [My Features](#main-features)
* [Main Features from Egzumer](#main-features-from-egzumer)
* [Manual](#manual)
* [Compiling and Building from Docker](#compiling-and-Building-from-docker)
* [Flashing the Firmware with UVTools2](#flashing-the-firmware-with-uvtools2)
* [Credits](#credits)
* [Other sources of information](#other-sources-of-information)
* [License](#license)

## Main features and improvements from F4HWN:

* Fusion is now the reference edition of the project:
    * all-in-one firmware for UV-K1 and UV-K5 V3,
    * spectrum analyzer made by Fagci,
    * commercial FM radio support,
    * Vox and Aircopy support,
    * screenshots and K5Viewer support,
    * advanced RX audio profiles and Audio Scope,
    * first-responder oriented options,
    * small breakout game,
* improve default power settings level: 
    * Low1 to Low5 (<~20mW, ~125mW, ~250mW, ~500mW, ~1W), 
    * Mid ~2W, 
    * High ~5W,
    * User (see SetPwr),
* improve S-Meter (IARU Region 1 Technical Recommendation R.1 for VHF/UHF - [read more](https://hamwaves.com/decibel/en/)),
   * S-Meter (S0/S9) Level EEPROM settings that were introduced in the Egzumer firmware are now ignored and replaced by hardcoded values to comply with the IARU Recommendation.     
* improve bandscope (Spectrum Analyser):
    * add channel name,
    * add save of some spectrum parameters,
* improve UI: 
    * menu index is always visible, even if a menu is selected,
    * s-meter new design (Classic or Tiny), 
    * MAIN ONLY screen mode, 
    * DUAL and CROSS screen mode, 
    * RX blink on VFO RX, 
    * RX LED blink, 
    * Squelch level and Monitor,
    * Step value,
    * CTCSS or DCS value,
    * KeyLock message,
    * last RX,
    * move BatTxt menu from 34/63 to 30/63 (just after BatSave menu 29/63),
    * rename BackLt to BLTime,
    * rename BltTRX to BLTxRx,
    * improve memory channel input,
    * improve keyboard frequency input,
    * add percent and gauge to Air Copy,
    * improve audio bar,
    * add backlight fading,
    * add Audio Scope on TX,
    * and more...
* new menu entries and changes:
    * add SetPwr menu to set User power (<20mW, 125mW, 250mW, 500mW, 1W, 2W or 5W),
    * add SetPTT menu to set PTT mode (Classic or OnePush),
    * add SetTOT menu to set TOT alert (Off, Sound, Visual, All),
    * add SetCtr menu to set contrast (0 to 15),
    * add SetInv menu to set screen in invert mode (Off or On),
    * add SetEOT menu to set EOT (End Of Transmission) alert (Off, Sound, Visual, All),
    * add SetMet menu to set s-meter style (Classic or Tiny),
    * add SetLck menu to set what is locked (Keys or Keys + PTT),
    * add SetGUI menu to set font size on the VFO baseline (Classic or Tiny),
    * add SetRxA menu to select RX audio profiles,
    * add TXLock menu to open TX on channel,
    * add SetTmr menu to set RX and TX timers (Off or On),
    * add SetOff menu to set the delay before the transceiver goes into deep sleep (Off or 1 minute to 2 hours),
    * add SetNFM menu to set Narrow width (12.5kHz or 6.25kHz),
    * add SetVol menu to adjust RX audio volume,
    * rename BatVol menu (52/63) to SysInf, which displays the firmware version in addition to the battery status,
    * improve PonMsg menu,
    * improve BackLt menu,
    * improve TxTOut menu,
    * improve ScnRev menu (CARRIER from 250ms to 20s, STOP, TIMEOUT from 5s to 2m)
    * improve KeyLck menu (OFF, delay from 15s to 10m)
    * add HAM CA F Lock band (for Canadian zone),
    * add PMR 446 F Lock band,
    * add FRS/GMRS/MURS F Lock band,
    * add SetNav hidden menu to select the navigation layout according to the radio model,
    * remove blink and SOS functionality, 
    * remove AM Fix menu (AM Fix is ENABLED by default),
    * add support of 3500mAh battery,
* improve status bar:
    * add SetPtt mode in status bar,
    * change font and bitmaps,
    * move USB icon to left of battery information,
    * add RX and TX timers,
* improve channel scanning:
    * support up to 24 scan lists,
    * each memory channel can be assigned to `OFF`, to one list (`01` to `24`), or to `ALL`,
    * `ALL` scans every channel except those set to `OFF`,
    * named scan lists are shown in the UI and status bar when available,
    * if the selected scan list is empty or invalid, the firmware automatically jumps to the next valid one,
* add resume mode on startup (scan, spectrum analyzer and FM radio),
* improve VFO persistence and restore behavior:
    * save the Squelch level adjusted with F + UP or F + DOWN,
    * restore the full VFO state on long press of EXIT,
* new actions:
    * RX MODE,
    * MAIN ONLY,
    * PTT, 
    * WIDE NARROW,
    * 1750Hz,
    * MUTE,
    * POWER HIGH,
    * REMOVE OFFSET,
* new key combinations:
    * add the F + UP or F + DOWN key combination to dynamically change the Squelch level,
    * add the F + F1 or F + F2 key combination to dynamically change the Step,
    * add F + 8 to quickly switch backlight between BLMin and BLMax on demand (this bypass BackLt strategy),
    * add F + 9 to return to BackLt strategy,
    * add long press on MENU, in * SCAN mode, to exclude the current memory channel,
    * add direct scan list selection while scanning with two digits (`00` = `ALL`, `01` to `24` = scan list).
* many fix:
    * squelch, 
    * s-meter,
    * DTMF overlaying, 
    * scan range limit,
    * clean display on startup,
    * no more PWM noise,
    * K5Viewer/serial key handling,
    * spectrum freeze on USB-C unplug,
    * Audio Scope behavior in OnePush mode and after DTMF/1750,
    * and more...
* enabled AIR COPY
* disabled ENABLE_DTMF_CALLING,
* disabled SCRAMBLER,
* remove 200Tx, 350Tx and 500Tx,
* unlock TX on all bands needs only to be repeat 3 times,
* code refactoring and many memory optimization,
* stream the live screen of the Quansheng K5 to K5Viewer and capture screenshots over a USB-to-Serial cable,
* and more...

## Main features from Egzumer:
* many of OneOfEleven mods:
   * AM fix, huge improvement in reception quality
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

This project provides a Docker-based build system to compile the Fusion firmware for the UV-K1 and UV-K5 V3. Everything is handled through the `compile-with-docker.sh` helper script.

The documented build output is generated inside `build/Fusion`, using the CMake presets defined in `CMakePresets.json`.

### Prerequisites

- Docker installed on your system
- Bash environment (Linux, macOS, WSL, Git Bash on Windows)

### Build Script Overview

The script `compile-with-docker.sh` performs the following actions:

1. Builds the Docker image (`uvk1-uvk5v3`) if it does not already exist.
2. Removes any previous `build` directory to ensure a clean configuration.
3. Runs CMake using the `Fusion` preset inside the Docker container.
4. Builds the firmware and outputs `.elf`, `.bin` and `.hex` files.

### Usage

```bash
./compile-with-docker.sh Fusion [extra CMake options]
```

### Documented Preset

- **Fusion**

### Examples

Build Fusion:

```bash
./compile-with-docker.sh Fusion
```

### Passing Additional CMake Options

You can pass extra configuration options after the preset name.  
These are forwarded directly to `cmake --preset` inside the container.

Examples:

```bash
./compile-with-docker.sh Fusion -DENABLE_SPECTRUM=ON
./compile-with-docker.sh Fusion -DENABLE_FEAT_F4HWN_GAME=ON -DENABLE_NOAA=ON
./compile-with-docker.sh Fusion -DSQL_TONE=600
```

### Notes

- The first run may take a few minutes while Docker builds the base image.
- Each build runs inside Docker, so your host environment remains clean.

## Flashing the Firmware with UVTools2

You can flash the UV-K5 V3 and UV-K1 directly from your web browser using the cross-platform WebSerial-based [UVTools2](https://armel.github.io/uvtools2/).

It works on Chrome, Chromium and Edge (desktop versions), and does not require installing any driver or software on your computer.

## Steps to flash the firmware

- Open UVTools2 in [flash](https://armel.github.io/uvtools2/?mode=flash) mode (or click the Flash Firmware tab).
- Connect your radio to your computer using a compatible USB programming cable (USB-C or Baofeng/Kenwood like double jack USB cable).
- Make sure your radio is in **DFU mode (flash mode)**.
- Select the firmware .bin file on your computer. 
- Click on `Flash Firmware`, then select the serial port associated with your radio.
- The progress bar will guide you through the flashing steps.

Once finished, your radio restart with the new firmware.

## Steps to dump or restore calibration data

[UVTools2](https://armel.github.io/uvtools2/) can also dump and restore calibration data, which is highly recommended. It’s best to create a dump right after installing F4HWN firmware, and to restore it before installing another firmware (or when returning to the stock firmware, for example).

### Dump

- Open UVTools2 in [dump](https://armel.github.io/uvtools2/?mode=dump) mode (or click the Dump Calib tab).
- Power on your radio in **normal mode**.
- Click `Dump Calibration Data`.

When the process is complete, click `Download calibration.dat` to save the file to your computer.

> [!NOTE]
> A good practice is to rename your calibration file using the serial number of your radio, which you can find on the label on the back of the device once you remove the battery. This helps avoid mixing up calibration files when you own multiple units.

### Restore

- Open UVTools2 in [restore](https://armel.github.io/uvtools2/?mode=restore) mode (or click the Restore Calib tab).
- Power on your radio in **normal mode**.
- Select your calibration.dat file on your computer.

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
