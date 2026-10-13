# SelCall: 5-tone selective calling (ZVEI-1/2, CCIR-1/2), on demand, per channel

Resident firmware feature (`ENABLE_FEAT_SELCALL`, on in the Labs and Custom
presets): each channel can hold a selcall type and two 5-digit codes, sent on
demand from a programmable key, typically to open a repeater's logic (e.g. a
"test" and a "production" code). Nothing is sent automatically.

## Use

1. Assign **SELCALL 1** and/or **SELCALL 2** to a programmable key (menu F1Shrt,
   F1Long, F2Shrt, F2Long or M Long).
2. On the channel, set the menus (category Channels):
   - **SelCal**: OFF / ZVEI-1 / ZVEI-2 / CCIR-1 / CCIR-2
   - **SC CD1**: code 1, 5 digits typed on the keypad (leading zeros kept)
   - **SC CD2**: code 2
   To clear a code, step it to OFF with UP/DOWN (OFF comes after 99999 and
   before 00000).
3. Press the key: the radio keys up with the channel's normal TX settings
   (frequency, power, CTCSS/DCS), waits 300 ms, sends the 5 tones and unkeys.
   Double beep and no TX if the channel has no selcall type or no such code.

During a selcall burst the DTMF PTT-ID and the roger beep are not sent (the
burst only opens the repeater logic); the CTCSS/DCS tail is. The DTMF side-tone
setting (D ST) also controls the selcall side tone.

## Tones

No gap between tones. A digit equal to the previous tone is replaced by the
repeat tone.

| Digit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | Repeat | Tone |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ZVEI-1 (Hz) | 2400 | 1060 | 1160 | 1270 | 1400 | 1530 | 1670 | 1830 | 2000 | 2200 | 2600 | 70 ms |
| ZVEI-2 (Hz) | 2400 | 1060 | 1160 | 1270 | 1400 | 1530 | 1670 | 1830 | 2000 | 2200 | 970 | 70 ms |
| CCIR-1 (Hz) | 1981 | 1124 | 1197 | 1275 | 1358 | 1446 | 1540 | 1640 | 1747 | 1860 | 2110 | 100 ms |
| CCIR-2 (Hz) | 1981 | 1124 | 1197 | 1275 | 1358 | 1446 | 1540 | 1640 | 1747 | 1860 | 2110 | 70 ms |

Example: `11223` in ZVEI-1 is sent as 1060, 2600, 1160, 2600, 1270 Hz; in CCIR
as 1124, 2110, 1197, 2110, 1275 Hz.
Sources: sigidwiki "ZVEI Selcall" and "CCIR Selcall", Wikipedia "CCIR (selcall)";
digits and repeat tones match multimon-ng's decoder tables (`demod_zvei1.c`,
`demod_zvei2.c`, `demod_ccir.c`).

## Storage

One 8-byte record per channel in each config bank, at physical address
`0x00B000 + index * 8` (3 sectors, `0x00B000-0x00DFFF`, free in every bank: the
bank uses `0x0000-0x886E`, `0x9000-0x90E8` and `0xA000-0xA178`):

- index 0-1023: memory channels; 1024 + band * 2 + vfo: the 14 VFOs
- `[0]` type (0 off, 1 ZVEI-1, 2 ZVEI-2, 3 CCIR-1, 4 CCIR-2), `[1..3]` code 1
  (24-bit LE), `[4..6]` code 2, `[7]` reserved; erased flash (`0xFF`) = off, no
  code, so existing configurations need no migration.

The serial link reaches the records through the EEPROM-compatible map
(`driver/eeprom_compat.c`): addresses **`0xD000-0xF06F`** map to `0xB000-0xD06F`
of the active bank.

- On the radio: saving a VFO to a memory copies the VFO's record, deleting a
  channel clears it, a factory reset erases all records.
- Records are per config bank, like the channels (`SetCfg`).
- The original CHIRP driver does not touch the area (it uploads up to `0xA178`):
  it neither shows nor erases the selcall settings.

## Side-key actions

`ACTION_OPT_SELCALL_1 = 24` and `ACTION_OPT_SELCALL_2 = 25` are appended to the
persisted action enum (never renumber).

## CHIRP driver

`tools/selcall/chirp/f4hwn.chirp.v6.2.0-SelCall.py` is the v6.1.0 F4HWN driver
(the memory layout is unchanged in v6.2.0) with selcall support. Load it in CHIRP
with *File > Load Module*. It keeps the original model name, **UV-K1 & UV-K5 V3
(F4HWN)**, so saved images (`.img`) open in both drivers; the version line
(*About*) ends with `+ SelCall ZVEI/CCIR`.

- Each channel's **Extra** tab (memories and VFOs) shows **SelCall type**
  (OFF / ZVEI-1 / ZVEI-2 / CCIR-1 / CCIR-2), **SelCall code 1** and **SelCall
  code 2** (5 digits, or empty for none; anything else is refused).
- The key-action lists offer **SELCALL 1** and **SELCALL 2**.
- Download reads up to `0xF080` (the selcall window included); upload writes the
  usual area (`0x0000-0xA178`), the calibration if enabled, then the selcall
  window.
- Deleting a channel clears its record, as on the radio; copy and paste of a
  channel carries its selcall settings with the other extras.
- Images saved with the original driver open here, padded with `0xFF` (selcall
  off); the original driver opens images saved with this one and ignores the
  selcall window.
- With a firmware built without `ENABLE_FEAT_SELCALL`, the window reads as `0xFF`
  (off everywhere) and writes are ignored.

## Tests

Host test of the tone sequence builder (`App/app/selcall_seq.c`, no hardware
dependency), 23 checks (plain codes, repeat tone per standard, all-same digits,
leading zeros, invalid type or code, tone durations):

```
cc -I App -o /tmp/test_selcall tools/selcall/test_selcall_seq.c App/app/selcall_seq.c && /tmp/test_selcall
```

CHIRP driver against the CHIRP source (real `bitwise` parser and settings
classes, 19 scenarios, plus cross-compatibility with the original driver):
`tools/selcall/chirp/test_chirp_selcall.py` and `test_compat_original.py`
(setup and usage in their headers).

On air: receive the burst with an RTL-SDR and decode it with multimon-ng:

```
rtl_fm -f 433.650M -M fm -s 22050 - | tee burst.raw | multimon-ng -t raw -c -a ZVEI1 -a ZVEI2 -a CCIR -
```

multimon-ng prints the repeat tone as `E` (`11223` decodes as `1E2E3`) and does
not measure time, so it does not tell CCIR-1 (100 ms) from CCIR-2 (70 ms).
`tools/selcall/measure_tones.py` (numpy) reads the recorded audio and lists each
tone with its frequency and duration, then classifies the burst:

```
tools/selcall/measure_tones.py burst.raw --rate 22050     # or a WAV file
```

Use a code with a doubled digit (e.g. `11223`) to see the repeat tone. Check the
CTCSS with a second radio set to the same tone: its squelch must open during the
burst.

Results on a UV-K1: ZVEI-1, ZVEI-2 and CCIR decoded by multimon-ng; tone
durations measured at 70 ms (ZVEI-1, ZVEI-2, CCIR-2) and 100 ms (CCIR-1) with
the expected repeat tones and about 300 ms of carrier before the first tone;
CTCSS present during the burst. The CHIRP driver was used on the radio to set
the selcall codes, and every scan mode, mixed scan lists included, still works.

## Credits

- Earlier SelCall work by Olivier (@ozitoune) in #45 and #92, and the
  requirements for alert networks (Canal-E, GRA) described by @Yeti-73 in #92.
  This implementation is new code, but it builds on those discussions.
- Tone tables: sigidwiki "ZVEI Selcall" and "CCIR Selcall", Wikipedia "CCIR
  (selcall)", cross-checked against multimon-ng's decoder tables (values only,
  no code taken).
- CHIRP driver: Armel / F4HWN's v6.1.0 driver, itself derived from the work of
  Jacek Lipkowski SQ5BPF, Egzumer and JOC2 (see its header, GPL v2 or later);
  the selcall additions are marked in that header.
