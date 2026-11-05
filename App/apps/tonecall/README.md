# ToneCall

ToneCall sends a sequential tone call (selective call, "selcall") on the current channel: five tones or more sent back to back, in ZVEI-1, ZVEI-2, CCIR-1 or CCIR-2. The call is chosen among 4 presets that the user defines in the app itself. No code is built in: the user stays responsible for what is sent, on frequencies where they are allowed to transmit.

## Where it is used

The main use is **Canal E**, the Alpine emergency channel (161.300 MHz, simplex, CTCSS 123 Hz on TX: SDIS 74 / GRA in Haute-Savoie, Rega in Switzerland, the Italian side). `21414` raises an alarm at the dispatch centre (acknowledged by 3 long tones), `21301` is a coverage test (2 long tones). Rega accepts ZVEI-1 or ZVEI-2. The SDIS 74 rule: the code is sent **once**, at the start of the call, repeated only if nobody answers, and **never with the PTT during the conversation**. ToneCall follows it: the call is a separate action, then the conversation goes on with the PTT on the channel.

5-tone sequences are not used to open amateur repeaters (1750 Hz or CTCSS are); German/Austrian fire brigades still alert pagers and sirens with ZVEI (receive only for us); CCIR is mostly professional PMR.

## Using it

1. Tune the channel the call goes on, with its TX CTCSS/DCS (123 Hz for Canal E).
2. Open ToneCall (`F` + `7`, pick its slot), or with a side key set to `TONE CALL` (see below).
3. `UP`/`DOWN` (UV-K5 V3) or `LEFT`/`RIGHT` (UV-K1) selects a preset; `MENU` or `PTT` sends it.

The arrow keys follow the radio: `UP` / `DOWN` on the UV-K5 V3, `LEFT` / `RIGHT` on the UV-K1 (the firmware's `SetNav` setting).

| Screen | Key | Action |
|---|---|---|
| List | `UP` / `DOWN` (UV-K1: `LEFT` / `RIGHT`) | Select a preset |
| List | `MENU` / `PTT` | Send the selected preset |
| List | `F` | Edit the selected preset |
| List | `EXIT` | Quit |
| Listen | `MENU` / `PTT` | Send the same preset again |
| Listen | `UP` / `DOWN` (UV-K1: `LEFT` / `RIGHT`) | Back to the list |
| Listen | `EXIT` | Quit, back to the channel |

The list shows the TX frequency on top, with the channel's TX tone on the right (`CT123.0`, or `D023N` / `D023I` for DCS) when it has one, then the 4 presets (number, type, code) with the selected one highlighted. The last selected preset, whether sent or not, is restored at the next launch.

A call keys up with the channel's normal TX settings (frequency, power, CTCSS/DCS), leaves 300 ms of carrier, sends the tones with no gap and unkeys. The app then stays in RX on the same channel with the speaker following the carrier squelch, so the station's acknowledgement is heard ("RX" while the squelch is open). This listen screen does not apply the channel's RX CTCSS/DCS filter: any carrier that opens the squelch is heard. This does not affect the intended Canal E setup, which uses 123.0 Hz on TX and no RX tone. `TX OFF` means the radio refused to transmit (TX lock, band, battery).

## Launching it from a side key

ToneCall declares a shortcut, like FM, Fox Hunt, Beacon and BEAM: any assignable key action (menu `F1Shrt`, `F1Long`, `F2Shrt`, `F2Long`, `M Long`) can be set to `TONE CALL`, and that key then opens the app directly. The action shows `N/A` in the menu while ToneCall is not installed, and the key gives the double error beep. Requires a firmware that knows the ToneCall shortcut ID; older firmware simply ignores it and the app stays reachable from the Apps menu.

## Editing a preset

`F` on the list opens the selected preset.

| Key | Action |
|---|---|
| `0`-`9` | Add a digit (up to 8, leading zeros kept) |
| `F` | Delete the last digit |
| `*` | Next type: ZVEI-1, ZVEI-2, CCIR-1, CCIR-2 |
| `MENU` | Keep the preset, back to the list |
| `EXIT` | Discard the changes |

An empty code makes the preset empty. The presets are written to flash when the app exits, and only if something changed.

## Storage: 4 presets in 16 bytes

An overlay app's own config area holds `APP_CONFIG_LOCAL_MAX` (16) bytes. The presets are packed to fit it, with no resident code and no second app:

- A preset is one number below 2^29: `1 + type x 111111110 + index`, where `index = (10^n - 10) / 9 + code` for a code of `n` digits. Length (leading
  zeros included) and digits share the number; `0` is an empty preset. 
- The 16 bytes are 4 little-endian 32-bit words, one preset each in bits 28:0. The 4 x 3 top bits hold the last selected preset (2 bits) and a 10-bit magic; erased flash, or a config from an older ToneCall, reads as 4 empty presets.

Decoding uses subtractions only (the Cortex-M0+ has no divide instruction and the app links no division). Names are not stored: a free 5-character label would cost 25 bits and halve the preset count.

Updating ToneCall in the same slot keeps the presets: the loader carries the config area over the slot erase (`APP_SlotErase`).

## Files

- `tonecall_app.c`: the app (list, editor, call, listen screen).
- `tonecall_seq.c/.h`: tone sequence builder and preset packing, no hardware dependency.
- `gen_assets.py`: the assets (CTCSS/DCS tables for the TX tone display).

## Tests

```sh
# tone sequences and preset packing (host)
cd App/apps/tonecall && cc -o /tmp/test_tonecall test/test_tonecall_seq.c tonecall_seq.c && /tmp/test_tonecall
# build
./compile-app.sh tonecall
```

On air, `rtl_fm` + `multimon-ng` decode the calls:

```sh
rtl_fm -f <freq>M -M fm -s 22050 - | multimon-ng -t raw -c -a ZVEI1 -a ZVEI2 -a CCIR -
```

multimon-ng prints the repeat tone as `E` (11223 shows as 1E2E3) and cannot tell CCIR-1 from CCIR-2; `test/measure_tones.py` measures the tone durations on a recording.

Requires firmware v6.1.0 or later (app API level 2); an older firmware refuses to launch the app. The channel's TX tone next to the frequency comes from the overlay BEAM service (`beam_get`), built only into the **Labs** edition: in the other editions the tone is just not shown, the rest of the app works the same.
