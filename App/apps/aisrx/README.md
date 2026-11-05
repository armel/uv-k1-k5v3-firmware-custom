# AIS RX (overlay app)

Receives **AIS at 9600 bit/s** from the current VFO and keeps the four most
recent vessels. Dynamic position reports and static data are merged by MMSI.
The implementation is self-contained in the overlay and uses the resident API
level 3 audio-capture services; it adds no resident AIS mode or menu.

## Status

| Part | State |
|---|---|
| Reference format (`test/ais.py`) | Builds and parses message types 1, 2, 3, 5, 8 (Inland, DAC 200, FI 10), 18, 19 and 24; field layout checked against a real gpsd AIVDM report; HDLC stuffing and CRC-16/X-25 self-tested |
| Receiver model (`test/model_rx.py`) | Radio channel as the other RAW decoders' models (0.065 LSB/Hz, high-pass corner 300-1500 Hz, 4th-order low-pass swept 3.3-8 kHz, +/-1 % clock, noise, both polarities); every case decodes with a low-pass at 5 kHz or above, most at 4 kHz, few at 3.3 kHz; no frame from 20 s of noise |
| Flipper vectors | Twelve AIS `.sub` files (one frame each, as a ship sends one report, checked through the model channel) plus a stock-preset probe at 433.650 MHz |
| Radio app (`aisrx_app.c`) | v0.1. During development: **decodes the Flipper files on the radio** and **receives real AIS on AIS1, 161.975 MHz** (2026-10-20); build 3920 B (176 B free), all app data in one assets block copied onto the stack |

Two design points matter, both found by giving up an early version that
could never receive a real ship:

- **Field bit order.** AIS sends each byte LSB first but numbers its fields
  from the MSB of byte zero (ITU-R M.1371, as AIS-catcher and gr-ais decode
  it). Reading the fields LSB first, with Flipper frames built the same wrong
  way, passes every bench test while a real ship's type 1 reads as type 8.
- **Demodulator.** A sign slicer on the discriminator signal fails on this
  radio: its audio high-pass (near 1 kHz, as found for POCSAG and RS41) makes
  long runs droop through zero and its low-pass flattens single-bit runs. A
  model with a 12 kHz low-pass, a 20 Hz high-pass and 3x the measured gain
  hides both; the realistic model decodes nothing with a sign slicer once the
  high-pass reaches 300 Hz.

## Use

1. Select FM and tune the VFO to **AIS1, 161.975 MHz**, or **AIS2,
   162.025 MHz** (433.650 MHz for the Flipper Zero bench files). The app
   listens to one channel at a time.
2. Launch `AIS RX`. `WAIT` shows in the status bar until a first vessel.
3. A valid CRC report brings that MMSI to the front of the list.

The screen follows ATIS RX and APRS RX:

| Screen | Content |
|---|---|
| Status bar | `AIS RX` title, a `WAIT` capsule until the first vessel, the speaker icon while it is on, then ▲ while rows or a newer vessel hide above and ▼ while rows or an older vessel hide below |
| Line 0 | The vessel name in bold (cut before the capsules), or its MMSI until a static report names it; at its right the frames received and, with several vessels kept, `2/4` (vessel shown / vessels kept, 1 = newest) |
| Lines 1-3 | The body, nothing cut, scrolled as in APRS RX: `227123456 Moored` (the MMSI when line 0 shows the name, never twice, then the navigational status); `48.8566N 2.3522E` (position, four decimals, about 11 m); `SOG 12.3kt COG 088` (speed over ground in knots, course over ground in degrees from 1 kt: below, it is only GPS noise); `HDG 090` (true heading, where the bow points); `>LE HAVRE` (destination); for Inland AIS vessels `ENI 01823456`, `110.0x11.4x2.5m` (length x beam x draught, on the ENI row when compact) and `Loaded 2 cones` (load, blue cones or B-flag); the whole name when line 0 had to cut it. A field the vessel has not sent takes no row. Normal view: 18 characters a row, 8 px apart, 3 rows at rest. Key `*` switches to the compact view, as APRS RX: 32 tiny characters a row, 6 px apart, 4 rows at rest, the heading on the speed row |
| Lines 4-5 | Under the dotted separator, the frequency in big digits, with `AIS` over `1` or `2` at its right on 161.975 / 162.025 MHz (where ATIS RX shows the marine channel) |
| Bottom row | `-89dBm  heard 3  last 02m15`: level, reports from this vessel (up to 999), time since the last one, `02m15` (minutes, seconds) under an hour then `03h07` (hours, minutes); refreshed every 5 s. Before the first vessel, the receive diagnostic below |

| Key | Action |
|---|---|
| UP / DOWN (held) | Scroll the body 1 px per 50 ms (UV-K1: LEFT / RIGHT), down to its last row |
| UP / DOWN (pressed again at the first / last row) | The newer / older vessel, from its first row, up to four kept; holding a key never leaves the vessel |
| * | Normal / compact view, saved on exit |
| 1 | Speaker on / off (saved, off by default: the decoder does not need it). Not gated by the squelch, as POCSAG RX, EPIRB 406 and the sonde apps on the same RAW path: the hiss is heard between bursts, and each burst as a short buzz |
| 2 | Clear the list and the counters |
| EXIT | Quit and restore the BK4829 receive registers |

AIS values which mean “not available” are not promoted to valid positions.
Keys are read every 50 ms, whatever the squelch, except while a lane is
inside a training sequence or a frame: a key scan takes about 0.4 ms, four
bits at 9600 bit/s. A key redraws at once; a new report or the 5 s refresh
waits for the squelch to close, one second at the latest, so the redraw does
not fall on the next burst. The speaker is not gated by the squelch, as in
the other RAW apps: read every 50 ms, a squelch gate, as ATIS RX uses, would
hardly ever open for a 26 ms AIS burst and key 1 would seem dead.

Until the first vessel, the bottom row is a receive diagnostic:

| Field | Meaning |
|---|---|
| `sync` | Bursts whose 12 training bits and start flag were found |
| `crc` | Frames with a valid CRC, any message type (the same count as the capsule on line 0) |

## Receiver path

The application samples the BK4829 discriminator on PA4 at 38.4 kHz (four
samples per bit) through the AF bypass (`REG_47` AF = 9, the path that lets
Sonde M20 decode 9600 chip/s), with the AFC, the receive HPF300/LPF3K and
de-emphasis off and the widest IF filter; every changed register is restored
on exit.

1. DC removal.
2. Equaliser `y = 7 x[n-2] - 3 (x[n] + x[n-4])`, taps half a bit apart: gain
   1 at DC, 2.8 at 2.4 kHz, 7 at 4.8 kHz. It restores the single-bit runs
   the receive low-pass flattens.
3. Two lanes, each a hysteresis slicer (the level flips beyond 1/8 or 3/8 of
   the peak and holds in between, whatever the high-pass droop) with its own
   transition DPLL (gain 1/4, follows +/-1 % of MCU clock error).
4. NRZI, HDLC flag after 12 training bits, zero de-stuffing, CRC residue; the
   first lane with a valid frame ends the burst for both.

Supported messages:

- 1, 2, 3: Class A position reports, with the navigational status (`Underway`, `Anchored`, `Disabled` (not under command), `Restrict` (restricted manoeuvrability), `Draught` (constrained by draught), `Moored`, `Aground`, `Fishing`, `Sailing`, `Towing`, `Pushing`, `AIS-SART`) and the true heading
- 5: Class A static and voyage data
- 18: Class B position report, with the true heading
- 19: extended Class B position report and name
- 24: Class B static data (part A name, 160 or 168 bits; part B call sign as
  a fallback)
- 8, DAC 200, FI 10: Inland AIS ship static and voyage data (European
  vessel number ENI, length, beam, draught, loaded / unloaded, blue cones or
  B-flag), field layout and codes as gpsd's AIVDM. gpsd has seen false
  DAC/FI matches: a message whose ENI is not eight digits, or whose hazard
  or load code is undefined, is ignored

## Can this radio receive 9600 bit/s?

Yes, through the AF bypass: AIS RX received real AIS on AIS1, 161.975 MHz
(2026-10-20). Without the bypass the audio stops near 3.3 kHz (Sonde Lab,
2026-10-04), far too low; the bypass reaches high enough, but how far was
never measured. In the model, 9600 bit/s needs an audio low-pass at 4 kHz
or above. A discriminator level readout and two tone probes (2.4 and 4.8 kHz)
were written to measure it during development; real reception made them
unnecessary and they were dropped for room.

## Flipper Zero bench test

The Flipper Zero cannot transmit on the marine VHF AIS channels. The supplied
vectors therefore use **433.650 MHz**, which is in the CC1101 range. They are a
decoder bench test only. A custom asynchronous CC1101 GFSK preset uses 9.596
kbaud and +/-2.38 kHz deviation, a close but not standards-compliant
approximation of AIS GMSK (9.6 kbit/s, BT 0.4). `probe_ais_stock.sub` uses
the stock `2FSKDev238Async` preset, the one already proven at 9600 chip/s by
the Sonde M20 files.

Regenerate and validate the files from this directory:

```sh
python3 -B test/flipper_ais.py test/flipper
```

Copy the resulting `.sub` files to `subghz/` on the Flipper SD card, tune the
radio to 433.650 MHz FM, launch AIS RX, then send one file. Every AIS file
holds one frame after a 100 ms carrier, as a ship sends one report: each send
adds one to the frames-received capsule and to `heard`. Send a file ten times
to measure reliability: `heard 10` means none was lost.

| File | Purpose |
|---|---|
| `probe_ais_stock.sub` | The Class A frame with the stock 2-FSK preset; if it decodes and `ais_classa.sub` does not, the custom preset is the problem |

Reading the diagnostic row (cleared screen, no vessel yet): `sync` rising
with each send while `crc` stays 0 means the bursts arrive but bits are lost;
`sync 0` means the training or flag never comes through.

| File | Expected result |
|---|---|
| `ais_classa.sub` | MMSI 227123456, Class A position |
| `ais_type2.sub` | Same Class A fixture encoded as message 2 |
| `ais_type3.sub` | Same Class A fixture encoded as message 3 |
| `ais_moored.sub` | No name: `227555000` in bold, then `Moored`, `48.8606N 2.2935E`, `SOG 0.0kt`, `HDG 090` |
| `ais_classb.sub` | MMSI 244987654, Class B position |
| `ais_classb19.sub` | Class B position and `FLIPPER CLASS B` |
| `ais_static5.sub` | `F4HWN TEST`, `227123456`, `>LE HAVRE` (merged with the Class A position when `ais_classa.sub` was sent first) |
| `ais_static24.sub` | `FLIPPER CLASS B` |
| `ais_static24b.sub` | Type 24 part B call sign `F4AIS` (fallback without part A) |
| `ais_inverted.sub` | Valid frame with inverted RF polarity |
| `ais_inland.sub` | Inland AIS for the Class A fixture's MMSI `227123456`: `ENI 01823456`, `110.0x11.4x2.5m`, `Loaded 2 cones`. Message 8 carries no name: send `ais_static5.sub` too and the same vessel shows `F4HWN TEST` |
| `ais_badcrc.sub` | No vessel; from a cleared screen, `sync 1  crc 0` per send |

Do not change the generator to an AIS marine frequency and do not use these
files for over-the-air AIS transmission. Actual AIS transmission is regulated
and must use compliant maritime equipment and an assigned identity.

## Tests

These host tests do not compile the firmware:

```sh
test/run_tests.sh
```

The model is the executable specification for the integer demodulator in
`aisrx_app.c`; update the two together.

## Version

Current: **v0.1**.
