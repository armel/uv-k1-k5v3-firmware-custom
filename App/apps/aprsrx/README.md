# APRS RX: on-radio APRS receiver (work in progress)

Goal: an overlay app that receives APRS (AX.25 UI frames, Bell 202 AFSK 1200
bauds) on the UV-K1 / UV-K5 v3 and keeps the last five frames, shown one at a time.

Status:

| Step | State |
|---|---|
| Hardware demodulation by the BK4829 FSK block | **Dead end** (2026-09-25, FT3D on 144.800: the chip has no Bell 202 demodulator) |
| Audio path an app can sample | **Done** by EPIRB 406: RX audio on PA4, ADC channel 4 at 9.6 kHz |
| Integer demodulator, modelled on synthetic audio (`test/model_rx.py`) | **Done** (see below) |
| Flipper Zero test transmitter (`test/flipper_aprs.py`) | **Done**, files in `test/flipper/` |
| Radio app (`aprsrx_app.c`) | **v0.1 receives the Flipper frames on the radio**; v0.3 decodes Mic-E (on air: F5RAV via F1PRY-14); v0.4 decodes continuously with 3 slicers (on air: F1PRY-14 via F5KTR-3); v0.5 shows standard positions; v0.6 adds the speaker key; v0.7 adds the corrected 20x20 APRS symbol bitmaps; v0.8 saves the speaker setting; v0.9 scrolls long compact frames; v0.10 fixes the UP/DOWN direction |
| Bench test with the Flipper on 433.650 MHz | **Done** (2026-09-30, v0.1: `_long` 10/10 in STD; `_badfcs` not shown) |
| Real station: FT3D beacon on 144.800 MHz | **Done** (2026-09-30, v0.1: Mic-E frame `>TXUPX9` received, FCS good, = 48°50.89' N 2°16.25' E) |
| Mic-E decoding (`test/mice.py`: spec encoder vs the app's decoder) | **Done** in the model: 6 cases + the FT3D frame |

## Using the app

1. Set the VFO to the APRS frequency, **FM**: 144.800 MHz, or 433.650 MHz for the
   Flipper test files.
2. Launch **APRS RX**.
3. The speaker is off by default (v0.6): the decoder does not need it, only the
   BK4829 AF output (tested on the radio: PA4 joins the audio before the
   amplifier). Key 1 turns it on to listen to the channel, as FoxHunt's audio
   key; since v0.8 the app keeps it as you left it.

Since v0.4 the app decodes **continuously**, as a TNC does: no RSSI trigger,
the FCS and a UI-frame check (control 0x03, PID 0xF0) sort frames from noise.
Up to v0.3, captures started 10 dB above a tracked noise floor; on a busy
144.800 that floor crept up (−132 → −104 dBm), so weak stations were never
sampled. Several frames in one transmission are now received too.

Keys and the screen cost sample time (the key scan alone is ~400 µs), so they
are served every 50 ms only while no slicer is inside a preamble (two
back-to-back flags) or a frame whose first bytes look like a callsign (busy
~4 % of the time on noise, in the model), and after 3 s of busy at the latest.
The screen is redrawn after a new frame, a key, or every 5 s.

The RAM history holds five frames, newest first; a new frame replaces the
oldest when the history is full. The screen shows one frame at a time: its
callsign in the large bold font, fixed on line 0, and its body scrolled under it
(y = 8 to 30). At the right of the callsign, once several frames are kept, a capsule
tells which frame is shown: `2/5`, the second newest of five kept (`1` is the
newest). Left of it (or alone at the edge with a single frame), a second capsule
shows the count of frames received. The symbol and RSSI are those of the frame shown. A new frame is
shown at once, from its first row. UP/DOWN held scrolls the body and stops at
its first / last row; pressing the key again there shows the newer (UP) or older
(DOWN) frame from its first row, so holding a key never leaves the frame. Body
rows occupy eight pixels in normal mode (3 rows at rest) or six in compact mode
(4 rows). History is lost on exit; no external-flash journal is used.

| Screen | Content |
|---|---|
| Status bar | `APRS RX` title, then a `WAIT` capsule until the first frame, the speaker icon while the speaker is on, then ▲ while rows or a newer frame are hidden above and ▼ while rows or an older frame are hidden below (x = 72-76 as in APRS TX, the speaker icon at x = 59-68; both drawn in one asset read) |
| Lines 0-3 | One frame: source call in bold, fixed on line 0, with two capsules at the right (the frames received, then with several frames kept `2/5`, frame shown / frames kept, 1 = newest; x >= 89); under it the body in the small font (18 characters a row, non-ASCII shown as `.`), 3 rows at a time, scrolled pixel by pixel with UP/DOWN: `>DEST,DIGI*,...` (as many path entries as fit two rows), then the info field, whole. For Mic-E (most Yaesu/Kenwood beacons, v0.3): latitude `48 50.89N`, longitude `002 16.25E`, speed km/h, course and symbol, the message type (`Off Duty`, `En Route`...), then altitude and comment without the device markers; for an uncompressed position (`!` `=` `/` `@`, v0.5): latitude, longitude, then the timestamp if any, the symbol (table + code) and the comment. Compressed positions stay raw text. A new frame is shown at once. Key * switches to the compact view (below) |
| Lines 4-6 | Under a dotted separator (y=31), the frequency drawn as the main screen draws it (big digits up to the kHz, the last two in the small font), aligned left. A matching 20x20 bitmap from the 48-symbol Yaesu set is shown at x=108, y=33-52, on the right for decoded Mic-E and uncompressed positions; an overlay symbol (table `0`-`9` or `A`-`Z`, as `L&` or `D&`) without a bitmap of its own (`E0`, `YY`) shows the alternate-table symbol it is drawn over (`\&`), the overlay character staying readable in the body text; the area stays blank for an unknown or unavailable symbol |
| Bottom row y=49 | `-89dBm`: RSSI at the end of the frame shown (the count of frames received moved to the capsule on line 0). (v0.4-v0.5 also showed frames per slicer, `sl a/b/c`: on air the outer slicers found frames too; dropped in v0.6 to fit the speaker key) |

Keys (UV-K5 and UV-K1):

| Key | Action |
|---|---|
| UP/DOWN (held) | Scroll the body of the frame shown 1 px per 50 ms slot, according to `SET_NAV` (UV-K1: LEFT/RIGHT), down to its last row |
| UP/DOWN (pressed again at the first / last row) | Show the newer / older frame, from its first row |
| * | Normal / compact view, saved on exit, returns to the first row of the frame shown. The callsign stays large and bold on line 0. Body rows use the normal font (18 characters) or 3x5 font (32 characters), continuous 1 px scrolling |
| 1 | Speaker on/off, saved on exit (v0.8; off by default); FoxHunt's speaker icon in the status bar while on |
| 2 | Clear all five frames and the counters |
| EXIT | Quit |

The receive path is the firmware's own (STD: 300 Hz high-pass, de-emphasis,
3 kHz low-pass). Up to v0.3, key 1 switched to RAW (filters and AFC off, as
EPIRB 406); every on-air decode was made in STD and the model shows no gain
for RAW, so v0.4 dropped it to fit the 4 KiB overlay.

The five frame buffers and their rotating pointers live on `app_main`'s stack;
the demodulator state lives in `listen`. Frames are copied only once on receipt;
rotation moves pointers, not payloads. Buffers and pointers take 1690 bytes on
the MCU, 1360 bytes more than the former 330-byte buffer (676 more than three
frames), excluding compiler
stack alignment and spill slots. The 4 KiB overlay also holds `.bss`. The screen
texts, symbol bitmaps and the two cos tables are assets. The display loads a
252-byte block containing 36 bytes of UI labels, 88 bytes of Mic-E messages and
the 128-byte Mic-E lookup. Compared with the original label-only read, this adds
212 temporary stack bytes and replaces character-decoding branches and separate
message reads.

The front-end initial state is stored in a 116-byte asset immediately before
the cosine tables, allowing one read to initialize `dem_t`. This replaces the
zeroing and initial-value assignments without adding a RAM buffer. Total assets
occupy 3832 of the 3840 available bytes.

## Demodulator (`test/model_rx.py`, class `Demod`; the C is a transcription)

Per ADC sample (9.6 kHz, 8 samples per bit):

1. DC removal (1-pole tracker, 64 samples).
2. Band-pass biquad centred on √(1200·2200) = 1625 Hz, Q 0.9 (Q14): equal gain
   on both tones, removes the out-of-band noise the box correlators let through.
3. Four sliding correlators over one bit: I/Q at 1200 Hz (8-entry cos table) and
   2200 Hz (48 entries = 11 cycles). The sine is the cos table read 3/4 period
   ahead (+6, +12). Products `>> 8`, so the decision below never overflows int32.
4. Magnitudes `max + 3/8 min`, per-tone peak trackers (attack 1/32, decay
   1/1024): each tone normalised by its own peak, so the twist (pre-emphasis or
   not, de-emphasis or not) does not bias the decision much.
5. **3 slicers** (since v0.4), decision `sign(wa·Mm·Ps − wb·Ms·Pm)` with
   wa:wb = 2:3, 1:1, 3:2, each with its own DPLL, NRZI and HDLC below (Direwolf's
   multi-slicer idea): the AGC does not cancel a strong twist in noise. A frame
   from any slicer is accepted; a copy from another slicer within 1 s is dropped.
6. DPLL: 65536 per bit, a bit at each wrap; each transition pulls the phase 1/4 of
   the way toward mid-bit + half a sample (the transition is only seen at the
   next sample: without that half-sample, a +1 % clock failed long frames).
7. NRZI, HDLC (flag, destuffing, abort after 7 ones), CRC-16/X.25 per byte,
   frame accepted on the residue 0xF0B8 **and** as a UI frame: noise brings ~0.7
   candidates per second to the FCS check, so without the UI check a false frame
   would pass about once a day.

### Slicers (`test/variants.py`, 4 seeds, 24 hard cases: noise 3000-4000 Hz, ±1 % clock, twist −9 to +9 dB at noise 2500 Hz, RAW and STD)

| Variant | Frames /96 |
|---|---|
| 1 slicer (v0.1-v0.3) | 57 |
| 3 slicers, weights ×2 | 65 |
| **3 slicers, weights ×1.5 (v0.4)** | **71** |
| 5 slicers (×1.5 and ×2) | 71 |

The gain is on strong twist: sine AFSK at +6 / +9 dB in RAW goes from 0/4 to 4/4.
Noise only (30 s, 2 seeds, 50 and 400 LSB rms): no frame accepted.

### Model results (`test/model_rx.py`, 3 seeds per case, 1 slicer; 3 slicers: all ≥ these)

Channel: discriminator output + white noise (rms over 48 kHz) → radio audio path
(RAW: 5 kHz low-pass; STD: 300 Hz high-pass, 750 µs de-emphasis, 3 kHz low-pass)
→ 12-bit ADC at 9.6 kHz, 0.065 LSB/Hz (EPIRB 406 measurement), optional clock
error. 150 ms of no-carrier noise before and after each burst.

| Case | RAW | STD |
|---|---|---|
| Flipper, clean: `pos`, `long` (233 bytes) | 3/3, 3/3 | 3/3, 3/3 |
| Flipper `badfcs` (must be rejected) | 0 frames | 0 frames |
| Flipper, noise 1500 / 3000 / 4500 Hz | 3/3, 2/3, 0/3 | 3/3, 2/3, 0/3 |
| Flipper, ±4 kHz carrier offset | 3/3 | 3/3 |
| Flipper `long`, ±1 % sample clock | 3/3 | 3/3 (−1 %), 3/3 (+1 %) |
| Sine AFSK (a real station), twist −6 / 0 / +6 dB, noise 1500 | 3/3, 3/3, 2/3 | 2/3, 3/3, 3/3 |

Noise 4500 Hz is Eb/N0 ≈ 9.6 dB: the theoretical frame success of non-coherent
FSK on a 520-bit frame is then ~6 %, so the failures there are expected.
(At ±0.5 % clock, 6/6 in every variant tried.)

## Flipper Zero test transmitter (`test/flipper_aprs.py`)

The Flipper's CC1101 cannot put an audio tone on FM, only switch between two
frequencies (preset `2FSKDev238Async`, ±2.38 kHz). Each AFSK tone is therefore
sent as a square wave: the carrier toggles at twice the tone frequency,
phase-continuous across bits, with edge times rounded from exact values (no
drift). The receiver's discriminator outputs that square wave and its audio
low-pass leaves mostly the fundamental. The Flipper cannot reach 2 m anyway; the
script refuses 144-146 MHz.

```
test/flipper_aprs.py test/flipper [--call F4HWN] [--freq 433650000]
```

writes `aprs_pos.sub`, `_digi`, `_msg`, `_status`, `_long` (233 bytes, 2 s) and
`_badfcs` (one info bit flipped: must not be shown). 100 ms of carrier, 40 flags
(267 ms), the frame, 3 flags, 10 ms of carrier. Copy them to `subghz/` on the
Flipper, set the radio to 433.650 MHz FM, launch the app, then Send one file at
a time, about 1 s apart. (Up to v0.4, UP/DOWN tuned ±5 kHz for a Flipper whose
crystal puts the carrier off the channel; never needed on the bench, dropped in
v0.5 to fit 4 KiB.)

`test/ax25.py` builds the frames (shared by the generator and the model);
`test/mice.py` checks the Mic-E display decoder against a spec encoder, and
the standard position display against real frames;
`test/variants.py` compares demodulator variants on the hard cases (dev tool).

## Bench checklist

- Level reaching the ADC (`pp`, shown up to v0.4, dropped in v0.5 to make
  room): estimated ~300 LSB peak-to-peak for the Flipper (±2.38 kHz at
  0.065 LSB/Hz), **measured 734-778** with the Flipper and **916** with a real
  station (F1PRY-14): ~20 % of full scale, plenty of headroom, and the per-tone
  AGC makes the absolute level irrelevant.
