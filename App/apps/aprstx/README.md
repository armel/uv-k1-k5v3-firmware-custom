# APRS TX: on-radio APRS position beacon (work in progress)

Sends one APRS position frame (AX.25 UI frame, Bell 202 AFSK 1200 bauds) on the
TX VFO at each press of PTT or MENU. Companion of [APRS RX](../aprsrx/README.md),
which is also the test receiver.

| Step | State |
|---|---|
| Frame and bit stream model (`test/tx_model.py`) | **Done**: identical to the RX app's AX.25 reference, decoded by the RX model |
| Radio app (`aprstx_app.c`) | **v0.1 works on the radio** (2026-09-30); v0.2-v0.7 add and refine the original editor and display; v0.8 introduces APRS Edit; v0.9 moves the complete beacon-profile editor into APRS Edit; v1.0-v1.2 extend comments to five editor lines and 89 characters; v1.3 adds the editable source callsign; v1.4 changes the default comment; v1.5 adds the beacon timer (key 3), **works on the radio** (2026-10-13); v1.7 adds the round trip to APRS Edit |
| On-air test: APRS RX on a second radio, FT3D | **Done** (2026-09-30, v0.1: frames received by a UV-K1 running APRS RX and by the FT3D, first try, default level 66 and twist 0) |

## Station settings (`gen_assets.py`, then rebuild)

| Setting | Default |
|---|---|
| Source | optional profile callsign (1-6 uppercase letters/digits) + `SSID` = 7 (0-15, 0 = none), editable in APRS Edit; an empty profile callsign uses API `boot_callsign` |
| `DEST` | `APZK5` (APZ = experimental software) |
| `PATH` | `1` = `WIDE1-1`, an index in `PATHS`: `DIRECT` (no digipeater), `WIDE1-1`, `WIDE1-1,WIDE2-1`, `ARISS`; editable in APRS Edit |
| `LAT`, `LON` | `4850.90N`, `00216.25E`: default position, editable in APRS Edit |
| `SYMBOL` | `/[` (person): one of 48 symbols, editable in APRS Edit |
| `COMMENT` | `Portable, UV-K5/K1 F4HWN firmware (github.com/armel)` by default; up to 89 characters, editable with the companion APRS Edit app |

Frame sent: `F4HWN-7>APZK5,WIDE1-1:!4850.90N/00216.25E[Portable, UV-K5/K1 F4HWN firmware (github.com/armel)` (97 bytes,
~0.98 s on the air: 50 ms of tone settle, 40 flags = 267 ms, the frame, 3 flags).
If neither the profile nor boot message supplies a plain 1-6 character uppercase
alphanumeric callsign, the app shows `No callsign` and does not transmit.

APRS Edit changes the callsign, SSID, position, path, symbol and comment without
duplicating the transmitter. APRS TX 1.3 and later store them in one versioned 117-byte
profile asset. Reinstalling APRS TX restores the bundled defaults.

APRS TX 1.4 and later require Labs firmware v6.2.0 or later, the first one whose
configuration proxy lets APRS Edit change this profile: earlier firmware
refuses to launch it (`NOT SUPPORTED`) rather than transmit fixed defaults.

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
| 3 / F then 3 | Beacon interval: off, 1, 5, 15, 30, 60 min (saved) / beacon off. While an interval is set, the time left before the next frame counts down in a status-bar capsule (`4:32`, x = 40, where `TRANSMIT` shows while on the air) and the frame is sent at zero. The countdown restarts from the whole interval at launch, at each change of interval and at each send, PTT / MENU included: nothing goes on the air right at launch |
| 0 | Open APRS Edit. Its top-level `EXIT` saves the profile and returns directly to APRS TX; field-level `EXIT` still cancels only that field. If APRS Edit is absent, TX stays open and shows `No APRS Edit` |
| F | Arm the next key's down direction (1, 2) or the beacon off (3), as FoxHunt's F; icon in the status bar while armed |
| * | Scroll view / compact view (saved): compact shows the source in bold, then the path, the position `48 50.90N 002 16.25E`, the symbol and the comment in the tiny 3x5 font, 32 characters a row, without scroll |
| EXIT | Quit (level, twist, view and beacon interval are saved); the beacon only runs while the app is open |

Paths: `DIRECT` (heard only by the stations and iGates in range), `WIDE1-1` (one
repeat by a nearby digipeater), `WIDE1-1,2-1` (`WIDE1-1,WIDE2-1`: the usual
mobile / portable path, two repeats), `ARISS` (the ISS digipeater RS0ISS, which
ignores the WIDE aliases: set the VFO to 145.825 MHz for a pass; Doppler is about
±3.5 kHz on 2 m and the digipeater is not always active).

APRS TX keeps only its operational settings in ordinary app config: level,
twist, display view and beacon interval (5 bytes, magic `0xAA`). It migrates the
first three from the former `0xA9` and `0xA8` layouts, with the beacon off. Beacon content belongs to the profile edited by
APRS Edit.

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

`test/tx_model.py` (needs `../aprsrx/test`): the versioned profile, frame from
the real assets vs `ax25.build`, bit stream vs `ax25.hdlc_bits`, invalid
callsigns refused, profile callsign precedence, an edited position (south/west), every path with SSID 0, 15
and 9, a selected symbol, the position limits, then
AFSK (continuous phase or reset, tw 0/+4, TX path twist 0/+5 dB) decoded by the
RX model through its RAW and STD audio paths, with and without noise.
