# APRS TX: on-radio APRS position beacon (work in progress)

Sends one APRS position frame (AX.25 UI frame, Bell 202 AFSK 1200 bauds) on the
TX VFO at each press of PTT or MENU. Companion of [APRS RX](../aprsrx/README.md),
which is also the test receiver.

| Step | State |
|---|---|
| Frame and bit stream model (`test/tx_model.py`) | **Done**: identical to the RX app's AX.25 reference, decoded by the RX model |
| Radio app (`aprstx_app.c`) | **v0.1 works on the radio** (2026-09-30); v0.2 edits the position; v0.3 adds the SSID and path; v0.4 adds the corrected 20x20 bitmap and the 48-symbol selector; v0.5 keeps the source fixed while the frame body scrolls; v0.6 fixes the UP/DOWN direction |
| On-air test: APRS RX on a second radio, FT3D | **Done** (2026-09-30, v0.1: frames received by a UV-K1 running APRS RX and by the FT3D, first try, default level 66 and twist 0) |

## Station settings (`gen_assets.py`, then rebuild)

| Setting | Default |
|---|---|
| Source | the boot-message callsign (API `boot_callsign`, 1-6 letters/digits) + `SSID` = 7 (0-15, 0 = none): the **default**, until one is chosen on the radio |
| `DEST` | `APZK5` (APZ = experimental software) |
| `PATH` | `1` = `WIDE1-1`, an index in `PATHS`: `DIRECT` (no digipeater), `WIDE1-1`, `WIDE1-1,WIDE2-1`; the **default**, until one is chosen on the radio |
| `LAT`, `LON` | `4850.90N`, `00216.25E`: the **default** position, until one is edited on the radio (key 3) |
| `SYMBOL` | `/[` (person): the default, until one of the 48 symbols is chosen on the radio |
| `COMMENT` | `UV-K5/K1 F4HWN Firmware` (43 characters at most) |

Frame sent: `F4HWN-7>APZK5,WIDE1-1:!4850.90N/00216.25E[UV-K5/K1 F4HWN Firmware` (68 bytes,
~0.79 s on the air: 50 ms of tone settle, 40 flags = 267 ms, the frame, 3 flags).
If the boot message is not a plain callsign (empty, more than 6 characters once
spaces are dropped, or with a `/`), the app shows `No boot callsign` and does not
transmit.

In the normal view, the TX frequency is aligned left below the separator and
the selected APRS symbol is shown as a 20x20 bitmap at x=108, y=33-52. The 48-symbol
Yaesu bitmap set is stored in the app assets; the editor keeps the full width.

## Keys (UV-K5 and UV-K1)

| Key | Action |
|---|---|
| PTT or MENU | Send one frame |
| UP / DOWN (held) | Scroll the frame body above the separator 1 px per 40 ms loop, according to `SET_NAV` (UV-K1: LEFT/RIGHT), while the source callsign stays fixed in bold on line 0 as in APRS RX. In the status bar (x = 72-76), ▲ while rows are hidden above and ▼ while rows are hidden below; while F is armed its icon (x = 70-78, centred on them) takes their place, and they are not drawn while transmitting (the `TRANSMIT` capsule ends at x = 72). The body uses the small font, 18 characters a row, 3 visible rows at a time: `>DEST,WIDE…` (a row breaks after a comma), latitude, longitude, then symbol and comment |
| 1 / F then 1 | Tone level (deviation) up / down: REG_70 gain 10-127, step 4, default 66 (the firmware's tone gain) |
| 2 / F then 2 | Twist `tw` up / down, -4..+8: 2200 Hz gain = level × (8 + tw) / 8 (-6..+6 dB) |
| F | Arm the next key's down direction (1, 2), as FoxHunt's F; icon in the status bar while armed |
| * | Scroll view / compact view (saved): compact shows the source in bold, then the path, the position `48 50.90N 002 16.25E`, the symbol and the comment in the tiny 3x5 font, 32 characters a row, without scroll |
| 3 | Edit the position, SSID, path and symbol |
| EXIT | Quit (level, twist, position, SSID, path, symbol and view are saved) |

### Editor (v0.3)

```
LAT  48 50.90 N
LON 002 16.25 E
SSID 7
PATH WIDE1-1
SYMB /[

0-9, * change, F+* back
UP/DN move  MENU ok  EXIT
```

The field under the cursor is drawn in bold: a digit or the N/S, E/W letter of
the position, or the whole SSID, path or symbol value. Type the 13 digits in a row, as a
frequency: `485090` then `0021625` (degrees, minutes, hundredths of a minute: the
APRS format, read as is from a GPS or aprs.fi in degrees-minutes). The cursor
skips the separators and N/S, and goes on from the latitude to the longitude;
UP/DOWN also stop on N/S and E/W, then take it on to SSID, PATH and SYMB.
When SYMB is selected, both shortened help rows remain visible and the edited
20x20 symbol is previewed at x=108, the same position as on the normal screen.

| Key | Action |
|---|---|
| 0-9 | Digit at the cursor, then the next one (on a position digit) |
| * | N/S (latitude line), E/W (longitude line), next SSID 0-15 (SSID), next path (PATH), or next symbol (SYMB) |
| F then * | Previous SSID, path or symbol (the `F` icon in the status bar while F is armed, as in FoxHunt; F again disarms it) |
| UP / DOWN | Move the cursor: 6 digits, N/S, 7 digits, E/W, SSID, PATH, SYMB |
| MENU | Check (degrees ≤ 90 / 180, minutes < 60) and keep: the frame is rebuilt; `Invalid position` otherwise |
| EXIT | Cancel |

Paths: `DIRECT` (heard only by the stations and iGates in range), `WIDE1-1` (one
repeat by a nearby digipeater), `WIDE1-1,2-1` (`WIDE1-1,WIDE2-1`: the usual
mobile / portable path, two repeats).

Everything is saved on exit (`cfg_save`, 13 bytes: magic 0xA8, level, twist, 13
digits and the hemispheres in nibbles, `0x80 | path << 4 | SSID`, then the view:
14 compact, anything else scroll, then the symbol index). An older config with
byte 12 erased uses the default person symbol. A v0.2 config (10 bytes: byte 10 erased)
keeps its position and takes the default SSID and path; an 11-byte config
(byte 11 erased) opens in the scroll view; a v0.1 config (magic 0xA7) is
ignored: defaults.

## How it transmits

As the Beacon app keys MCW: `tx_set_params` (carrier + PA on the TX VFO),
REG_51 = 0 (no CTCSS/DCS under the AFSK), `tx_tone(1200)` (tone path on, mic ADC
off, 50 ms settle). Then each bit is timed from SysTick, 40000 cycles at 48 MHz
(exact), and at each NRZI transition the tone frequency REG_71 (1200 Hz = 12389,
2200 Hz = 22714, the driver's `scale_freq`) and gain REG_70 are rewritten: two
SPI writes, a few tens of µs out of 833. `tx_mute` + `tx_end` restore RX.

## Unknowns for the first on-air test (all fine with the defaults on 2026-09-30)

1. **Phase continuity** when REG_71 is rewritten (an NCO keeps it, probably).
   The RX model decodes even with a phase reset at every transition (3/3 clean,
   2/3 with noise in STD); a hardware TNC such as the FT3D may be stricter.
2. **Twist.** The tone probably enters after the pre-emphasis, so the signal is
   flat and a receiver with de-emphasis sees 2200 Hz ~5 dB low. If the FT3D
   misses frames that APRS RX decodes, raise `tw` (key 2): +4 is +3.5 dB.
3. **Deviation** at level 66: aim for ~3 kHz; 1 up, F then 1 down.

## Test plan

1. Simplex test frequency (not 144.800 for the first frames), low power.
2. Receivers: APRS RX on a second radio (its per-slicer counters `s a/b/c`
   show the twist: frames only on the space-favouring slicer = 2200 Hz low) and
   the FT3D (APRS on band B, same frequency).
3. PTT, check both; adjust level and twist; then 144.800.

`test/tx_model.py` (needs `../aprsrx/test`): frame from the real assets vs
`ax25.build`, bit stream vs `ax25.hdlc_bits`, bad boot callsigns refused, an
edited position (south/west) vs `ax25.build`, every path with SSID 0, 15 and 9
and a selected symbol vs `ax25.build`, the config round trip (and a v0.2 config
falling back to the default SSID, path and symbol), the editor rows and bold column, the editor keys (13
digits typed in a row, cursor stops, `*` and F then `*` on each field; the
UP/DOWN keys through `nav_dir()`, with SET_NAV on and off), the position limits,
then
AFSK (continuous phase or reset, tw 0/+4, TX path twist 0/+5 dB) decoded by the
RX model through its RAW and STD audio paths, with and without noise.
