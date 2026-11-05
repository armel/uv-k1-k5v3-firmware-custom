# SSTV: send the boot logo, receive pictures (work in progress)

Overlay app for the Labs firmware (needs the `sysinfo` capability for
`sys_storage_read`). It receives **14 SSTV modes**, picked by the VIS: Robot
36 and 72, Martin M1 and M2, Scottie S1, S2 and DX, PD50, PD90, **PD120**,
PD160, **PD180**, PD240 and PD290 (the ISS sends PD120 or PD180); it sends the
boot logo in any of them (key 1).

| Step | State |
|---|---|
| Modes (`test/modes.py`, Dayton paper as MMSSTV / QSSTV / slowrx) | 14 modes; lengths = the published ones (PD120 126.1 s, Martin M1 114.3 s, ...) |
| Model: TX schedule, radio channel, integer receiver (`test/model.py`) | **Done** (sweep below) |
| TX sequence vs the PySSTV reference encoder (Robot36 class) | **Same** VIS and length (36.910 s); 0.8 % of the time differs, at pixel edges (128 vs 320 columns) |
| Radio app (`sstv_app.c`) | v0.7; progressive reception writes one LCD page with the resident `blit_page` (API level 3, no K5Viewer frame) so K5Viewer cannot block audio sampling; v0.6 used an app-local SPI1 page writer for the same reason |
| Last picture kept across launches | v0.4, **works on the radio** (2026-10-13): needs a firmware with `APP_FLAG_SAVE_SCREEN` (older firmware runs the app without keeping the picture). The loader copies the screen left at exit over the `PIC` asset block (the last 1024 B) and updates the asset CRC; an update of the app erases it |
| Sync re-acquisition | v0.5 (2026-10-13, issue #647): **not tested on the radio**, not run in `emu.py` yet. The reporter's QSSTV WAV (Robot 72) through the model, sender clock 0.5 % and 1 % fast: lost at line 20 before, whole picture after. Model, 25 ms of black between the VIS and the first line: Robot 36 lost at line 21 before, 0 px wrong after (14 modes: 0 px, 17 px in PD50 / PD90) |
| The built app in Unicorn (`test/emu.py`) vs the model | v0.5: REG_71 writes = the model's tones (Robot 36, Scottie S1, PD120); pictures identical pixel for pixel (Robot 36, Martin M1, Scottie S1, PD120). v0.6 and v0.7 still need a run here and radio validation |
| On the radios | v0.1 (2026-10-03): a picture starts, then nothing (see below); **v0.2 works** (Robot 36, 2026-10-03); v0.3 (all modes) not tested yet |

v0.1 timed everything from differences of SysTick readings, which lose 10 ms
whenever a service runs longer than the 10 ms SysTick period: a full blit is
~11 ms (LCD SPI at 48 MHz / 64 = 750 kHz, 7 pages of 128 B). The first picture
redraw put the receiver 96 samples late, at the edge of the sync window, and
the picture was lost a few lines later; the TX lines carrying a redraw were
10 ms too long. v0.2 reads an absolute clock: the 10 ms SysTick interrupt count
(`ticks_ms`) times 480000 plus the position in the period. `test/emu.py`
charges 1.5 ms per LCD page and serves `ticks_ms` at its 10 ms resolution: v0.1
fails there (919 wrong pixels), v0.2 matches the model.

## Using the app

1. Set the VFO, **FM**, on a simplex frequency (low power for the first tests).
   For the ISS: 145.800 MHz, wide FM (Doppler ±3.5 kHz), during an announced
   ARISS SSTV event.
2. Launch **SSTV**. It listens at once (the speaker as last left, off the first
   time: the decoder does not need it, as in APRS RX). A known VIS starts a
   picture, drawn row by row over the whole screen, status line included.
3. MENU sends the boot logo in the TX mode (key 1). The logo is shown
   while it goes out, each row inverted once sent. EXIT aborts. PTT is
   deliberately ignored by the SSTV app and never starts a transmission; this
   also rejects spurious PTT events caused by cable/UART activity. The long
   modes keep the transmitter on for minutes (Scottie DX 269 s, PD290 289 s):
   low power.

| Key | Action |
|---|---|
| MENU | Send the boot logo in the TX mode |
| PTT | Ignored; never starts an SSTV transmission |
| 1 / F then 1 | Next / previous TX mode (Robot 36 -> ... -> PD290 -> Robot 36), shown in the left capsule; saved |
| F | Arm the next key's backward direction (1), as APRS TX; F icon in the status bar (x = 70) while armed; F again disarms |
| 2 | Rendering: **1-bit** (threshold, sharp for the logo; default) / **dither** (4x4 Bayer, for photos), from the next row on; right capsule; saved |
| 3 | Speaker on / off (off by default: the decoder does not need it); saved |
| EXIT | Leaves a picture being received, then the picture view, then the app. On quitting, the last picture (even a partial one) is saved and is back under key 4 at the next launch, with its mode on the status line |
| 4 | Picture / info screen, once a picture came (before: `No picture yet`) |
| EXIT | From the info screen: quit. During a TX or while a picture is being received: abort it and return to the info screen. From the picture view: return to the info screen |

Info screen: `WAIT` in the status bar (x = 40, as APRS RX) until a picture
comes, the speaker icon at x = 59 while on; on line 0, 3x5 capsules: the TX
mode (key 1) on the left, the rendering (`1-BIT` / `DITHER`, key 2) on the
right; the status with the mode of the picture (`PD120 123/248` (periods: two
lines each in PD), `PD120 OK`, `PD120 lost`), or `Logo sent`, `TX denied`,
`TX aborted`, `RX aborted`, `No picture yet`; the RX frequency; the keys.

## Modes

| Mode | VIS | Length | Period | Scans read (weight) |
|---|---|---|---|---|
| Robot 36 | 8 | 36 s | sync 9, porch 3, Y 88, sep 4.5, porch 1.5, chroma 44 | Y |
| Robot 72 | 12 | 72 s | sync 9, porch 3, Y 138, 2 x (sep 4.5, porch 1.5, chroma 69) | Y |
| Martin M1 / M2 | 44 / 40 | 114 / 58 s | sync 4.862, then G B R, 146.432 / 73.216 each, 0.572 gaps | G 2, B 1, R 1 |
| Scottie S1 / S2 / DX | 60 / 56 / 76 | 110 / 71 / 269 s | sep 1.5, G, sep 1.5, B, sync 9, porch 1.5, R (138.24 / 88.064 / 345.6) | R 1, G 2, B 1 |
| PD50 / 90 / 120 / 160 / 180 / 240 / 290 | 93 / 99 / 95 / 98 / 96 / 97 / 94 | 50 / 90 / 126 / 161 / 187 / 248 / 289 s | sync 20, porch 2.08, Y0, R-Y, B-Y, Y1 | Y0 2, Y1 2 |

Times in ms. Scottie sends one more 9 ms sync after the VIS. Each mode is one
112-byte record in the assets (`test/modes.py` `record()`, `mode_t` in the C):
the period's segments for the TX, and for the RX the scan offsets from the
sync end, their weights, the period and the pixel step.

## TX

The boot logo is read where `ui/welcome.c` reads it (external flash 0x011008,
1024 B, LCD layout: 8 pages of 128 columns, bit set = dark pixel). Each scan
carries one logo row in 128 equal columns (scan / 128: whole cycles at 48 MHz,
e.g. 45600 for PD120), the 64 rows spread over the periods (`racc += A`, a new
row each time `racc >= B`: 4/15 for Robot, 1/4 for Martin and Scottie, 8/31
for PD120). Dark = black (1500 Hz), clear = white (2300 Hz); every colour scan
carries the same row and the chroma is neutral (1900 Hz): grey. An erased logo
sector (no logo uploaded) sends black.

As APRS TX: `tx_set_params`, REG_51 = 0 (no CTCSS/DCS), `tx_tone(1900)` (tone
path on, mic off, 50 ms settle, level 66), then each tone is started by writing
REG_71 at its time. REG_71 is written only when the frequency changes, in case
a write resets the tone phase. The row inversion (one LCD page, ~1.5 ms) and
the key scan are done in each sync (4.862 ms at least).
VIS: 1900 Hz 300 ms (1300 ms from v0.5: 1 s of lead-in, so that a receiver's
squelch or CTCSS decoder, or a repeater's key-up, does not eat the VIS), 1200 Hz 10 ms, 1900 Hz 300 ms, start bit 1200 Hz 30 ms,
7 bits LSB first + even parity (1100 Hz = 1, 1300 Hz = 0), stop 1200 Hz 30 ms.

## RX

The RX audio is sampled as in APRS RX and EPIRB 406: PA4 (biased by the DAC),
ADC channel 4, 9.6 kHz, continuously.

- Band-pass (APRS RX's biquad, 4x its gain), then per sample
  `p = y1 (y0 + y2)` and `q = y1^2`: for a tone of angular step w, p / q = 2 cos w
  whatever its level, so no AGC is needed. R = 256 P / Q: 1100 Hz 380,
  1200 Hz 362, 1500 Hz 285, 1900 Hz 164, 2300 Hz 33.
- P and Q smoothed (1/8 per sample) flag the leader (1800-2020 Hz), the sync
  (below ~1360 Hz) and the VIS bits (below ~1210 Hz), by comparisons only.
- VIS: a 20 ms sync run begun right after a 40 ms leader is the start bit; each
  bit is a vote over its middle 20 ms; the byte (parity included) is looked up
  in the mode records.
- Period: the **end** of the sync (1200 -> 1500 Hz in every mode, whatever
  comes before it) within ±10 ms of the prediction starts the period. Its scans
  start at their offsets (less 6.8 samples of filter delay, tuned on Robot 36),
  scaled to the tracked period. A late sync restarts the period; a sync near
  the *next* prediction cuts the current period short (a fast sender clock, or
  a period that ran on the prediction); no sync: the prediction (20 in a row:
  lost). After 3 periods on the prediction a sync **anywhere** restarts the
  period (v0.5, issue #647: a sender whose first line is not where the VIS
  says left every sync out of the window, a slanted picture lost after 20
  lines); it does not clear the count, only a sync in the next window does, so
  noise alone still ends as lost. The count starts at 3, so the first sync is
  taken anywhere too: QSSTV sends no sync pulse after the VIS stop bit, its
  first line starts 9 ms before the standard position (88 samples early for a
  window of 96: lost as soon as the sender's clock is 0.5 % faster than the
  radio's, measured on the reporter's WAV). The period is tracked (1/8 of each error, ±6 %), and the pixel step
  follows it.
- Pixel: lum = 285 - 256 Σp / Σq over its samples, 0..255, times the scan's
  weight into the line buffer (sum 4). Periods are averaged per screen row,
  then thresholded at 128 (1-bit) or against a 4x4 Bayer matrix (dither).
- Only the luminance is decoded: colour pictures come out grey.

The keys and the screen take sample time (key scan ~400 µs, one LCD page
~1.5 ms): in a picture they are served after each period's last scan, before
its sync ends (5.4 ms in Martin), one page at most. The app writes that page
with `blit_page`, which sends no K5Viewer frame (an app-local SPI1 writer
did it before the service existed). The resident `blit_line` also forces a
complete K5Viewer frame; its blocking UART transfer at 38400 baud is much longer than a
sync gap and previously produced black bands whenever the viewer was connected.
Out of a picture, housekeeping runs every 50 ms unless a VIS may be on: a leader,
a start bit begun right after one, or the VIS bits (3 s at most). Up to v0.3 a
sync run alone held them off too: receiver noise, mostly low-pitched, reads as
a sync half the time, so half the slots were skipped and a normal key press
(60-120 ms) was often missed (`test/emu.py` key test). In a picture the keys are
read once per period: hold a key up to 0.5 s in PD120, 1 s in Scottie DX.

### Model sweep (`test/model.py`, the test logo)

Channel: 300 Hz high-pass, 750 µs de-emphasis, 3 kHz low-pass, 3 kHz deviation
~ 195 LSB at the ADC, as in the APRS RX model. Wrong pixels of 8192.

Every mode, standard path: **0** in 1-bit (dither 122-507: the logo's 1-pixel
strokes rendered grey). PD120 / Martin M1 through the cases (1-bit):
std 0 / 0, no de-emphasis 0 / 0, TX clock +1 % 47 / 26, -1.5 % 78 / 57,
deviation 1.5 kHz 0 / 0, SNR 20 dB 0 / 0, 12 dB 0 / 0, 6 dB 3 / 0. The built
app, Martin M1 at -1.5 %: 163 (the slots taken by the keys and the screen in
its 5.4 ms gap).

Robot 36:

| Case | 1-bit | dither | sync error (samples) |
|---|---|---|---|
| standard path | 0 | 267 | -2..2 |
| no de-emphasis | 0 | 289 | -6..2 |
| TX clock +1 % | 5 | 241 | -2..28 (period 1455) |
| TX clock -1.5 % | 31 | 291 | -46..2 (period 1419) |
| deviation 1.5 kHz | 0 | 265 | -2..2 |
| SNR 20 dB | 0 | 299 | -2..2 |
| SNR 12 dB | 0 | 484 | -2..3 |
| SNR 6 dB | 28 | 915 | -4..5 |

Dither errors are the logo's 1-pixel strokes rendered grey: expected.

At 6 dB the VIS itself is marginal: decoded in 4 of 12 noise seeds without the
1 s lead-in, 3 of 12 with it (same noise luck). The table above is from before
the lead-in; with it, the seed of the "SNR 6 dB" case falls on a missed VIS.

## Test plan

1. **TX into a phone**: send the logo, listen on a second radio (speaker on),
   decode with the Robot36 app (Robot 36 only) or MMSSTV / QSSTV on a PC. Without
   a radio: `test/model.py wav logo.wav PD120 [dump.bin]` writes the same
   transmission as a WAV (any mode name of `test/modes.py`).
2. **Radio to radio**: SSTV on both, MENU on one, each TX mode (key 1). The logo
   should come back pixel for pixel in 1-bit.
3. **RX from other software**: a PD120 WAV (above, or a recorded ISS pass)
   played into a radio (VOX, or acoustic coupling) to the app.
4. **RX with K5Viewer**: repeat the same signal with the double-jack cable and
   K5Viewer connected. The image must still draw progressively on the radio;
   the viewer receives the completed image at the end, without black bands.

If the RX picture is shifted left or right on the radio, Y_OFF (22) is the knob;
if the VIS is missed, check the audio level at PA4 (the APRS RX `pp` reading,
~300 expected).

## Files

| File | Role |
|---|---|
| `sstv_app.c` | the app |
| `gen_assets.py` | texts, status table, mode names and records, dither thresholds, speaker icon |
| `test/modes.py` | the 14 modes and their asset records |
| `test/model.py` | TX schedule, channel, receiver model, sweep, WAV writer |
| `test/emu.py` | runs the built `sstv.elf` in Unicorn against the model |
