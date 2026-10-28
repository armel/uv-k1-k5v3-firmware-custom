# Sonde RS41

Receive-only overlay application for standard Vaisala RS41 radiosondes. It
samples the BK4829 AF output on PA4, validates RS41 STATUS and position
sub-blocks, and displays the serial number, position, altitude, vertical
speed, the horizontal speed and heading, and the sonde's battery voltage.

## Status

Version 0.6 implements the common 320-byte RS41 frame at 4800 bit/s, in its
classic GPS layout and in the newer multi-GNSS layout, and opens GeoNav on
`MENU`. The parser, CRC path,
normal RF polarity, and inverted RF polarity have synthetic host tests, and
`test/model_rx.py` runs the demodulator through a model of the radio's RAW
audio path. Reception of the classic layout on a UV-K1 or UV-K5 V3 has been
validated with the generated Flipper Zero files in both RF polarities, and
real RS41 signals have been decoded in the field. The multi-GNSS layout is
checked on synthetic frames only: no reception on a radio yet.

To fit the 4 KiB overlay, this first version deliberately does not implement:

- Reed-Solomon error correction;
- PTU temperature, humidity, and pressure conversion;
- extended or encrypted RS41 sub-blocks;
- the multi-GNSS layout's other blocks: date and time, satellites, and the
  `0x96` block (likely an authentication tag, not checked).

Consequently, a clean signal is required. A valid STATUS block is mandatory;
an invalid position block still counts as a received frame but shows no
position.
Each valid received frame triggers two short flashes of the green LED. When
enabled with key `5`, it also triggers one short decode beep; the Foxhunt signal
icon in the status bar indicates that the beep is active.

## Radio setup

1. Tune the VFO to the RS41 frequency in FM wide mode. Operational weather
   sondes are normally found in the 400.15–406 MHz meteorological band.
2. Launch **Sonde RS41**.
3. If a nearby sonde overloads the receiver, use key `3` to increase front-end
   attenuation.

The receive path is:

```
RS41 RF -> BK4829 RAW, wide IF -> AF bypass -> PA4 ADC at 19.2 kHz
        -> 2 hysteresis slicers, each with its DPLL -> hard-bit sync
        -> de-whitening -> CRC -> telemetry
```

The RAW audio path cuts the low frequencies (corner near 1 kHz, as found with
POCSAG RX), so the NRZ level droops back to zero within a few bits and a plain
sign decision fails. Each slicer flips its level when the signal passes 2/8 or
4/8 of its peak and holds it in between. The sync word is matched on hard
bits, either polarity, with up to 6 wrong bits out of 64.

The samples are timed by the MCU's internal oscillator, which can be off by
a few percent from one radio to another. Each slicer's DPLL therefore follows
the bit rate as well as the phase (second order, within +/-5 %).

The audio path also differs between radios. One radio, which decodes the M10
and M20 files perfectly, read two or three isolated wrong bits per RS41
STATUS block, always just after a run and a single opposite bit: a lower
audio low-pass rounds such bits off (the model reproduces it with a 2.7 kHz
corner instead of 3.3 kHz). The samples get a light edge lift before the
slicers, y + (y - previous) / 2: in the model it reads more frames through a
2.7 kHz low-pass and in heavy noise, and loses none at 3.3 kHz. On that
radio the Flipper file went from at most 1 frame in 10 to 10 in 10.

Model (`python3 -B App/apps/sonders41/test/model_rx.py`): the Flipper files
of both layouts are read correctly for an audio corner anywhere from 30 Hz to
1.5 kHz (the bad-CRC one is rejected), with a clock error up to +/-3 % (the
phase-only DPLL failed from 2 %) and moderate noise; receiver noise alone
gives no false sync.

While a frame is read, keys are polled only where a held sample cannot touch
a parsed block (3 bytes of margin): bytes `0x68`–`0x9D` and `0xCE`–`0x10E`.
The longest stretch without a poll is the 96 bytes after the sync, 160 ms, as
before the multi-GNSS layout.

## Keys

| Key | Action |
|---|---|
| `1` | Speaker on/off |
| `2` / `F`+`2` | Tune +5 / -5 kHz, up to +/-50 kHz |
| `3` / `F`+`3` | Cycle attenuation forward / backward |
| `4` | Restore the VFO frequency |
| `5` | Decode beep on/off, saved on exit |
| `MENU` | Open GeoNav (bearing, distance from the APRS position) once a position is decoded; its `EXIT` comes back here with the last frame |
| `EXIT` | Quit |

## Supported frame

- 64-bit RS41 synchronization word;
- 320-bit alternating preamble;
- 64-byte repeating whitening mask;
- common 320-byte frame;
- CRC-16/CCITT-FALSE STATUS (`0x79`, at `0x39`) and position sub-blocks;
- two frame layouts, sharing the STATUS block:
  - classic: GPS position `0x7B` at `0x112`;
  - multi-GNSS firmware (u-blox M10, `fw 0x50dd` in `rs41mod`), seen on
    sondes flown from MW51 stations: the GPS blocks are replaced by a `0x82`
    position block at `0xA1`, with the same ECEF position and velocity first.
    It is read only when the classic block's ID is missing at `0x112`, so a
    classic frame is decoded exactly as before;
- eight-character sonde serial number;
- ECEF position converted to WGS84 latitude, longitude, and height above
  the ellipsoid, in 32-bit integers: within about 3 m horizontally and 4 m in
  height of an exact conversion (median 0.5 m and 1.3 m, checked over random
  positions);
- ECEF velocity projected on the local vertical (`VS`, m/s, positive
  while climbing);
- the same velocity projected on the local east and north, in integers
  without trigonometry: horizontal speed (`HS`, whole m/s) and heading
  (`HDG`, degrees from north);
- battery voltage, STATUS byte `0x45` in 0.1 V (as `rs41mod`).

Screen body, four fixed rows: the position (4 decimals, about 11 m, so it
always fits one row), `ALT …m VS±…`, `HS …m/s HDG …`, `BAT …V`.

Rows 2 to 4 hold the same kind of data in every sonde app (M10, M20, RS41);
a missing field leaves its row blank.

## Build

From the repository root:

```bash
./compile-app.sh sonders41
```

The result is `build/Apps/SondeRS41.app` and must fit the 4 KiB overlay. The
firmware must include the overlay-app loader. The repository's plain `Fusion`
preset leaves that loader disabled; use the project's intended overlay-enabled
Fusion configuration when installing the app.

Host test:

```bash
App/apps/sonders41/test/run_tests.sh
```

Synthetic Flipper Zero bench files can be generated without a real RS41:

```bash
python3 -B App/apps/sonders41/test/flipper_rs41.py App/apps/sonders41/test/flipper
```

| File | Layout | Expected display |
|---|---|---|
| `rs41_valid.sub` | classic | `S1234567`, 48.8000N 2.3000E, ALT about 1200 m VS +5.2, HS 9 HDG 110, BAT 2.9 V |
| `rs41_inverted.sub` | classic | the same, inverted RF polarity |
| `rs41_badcrc.sub` | classic | nothing: FRAME must not increment |
| `rs41_badpos.sub` | classic | `S1234567`, BAT 2.9 V, FRAME increments, no position |
| `rs41_gnss_valid.sub` | multi-GNSS | `X7654321`, 45.2000N 5.7000E, ALT about 2500 m VS -12.3, HS 7 HDG 303, BAT 3.1 V |
| `rs41_gnss_inverted.sub` | multi-GNSS | the same, inverted RF polarity |
| `rs41_gnss_badpos.sub` | multi-GNSS | `X7654321`, BAT 3.1 V, FRAME increments, no position |

Tune the radio to 433.650 MHz FM wide for this bench test. The generated files
are synthetic SRD-band transmissions, not recordings of an operational sonde.
Use only a frequency permitted by the Flipper's configured region.

## Version

Current: **v0.6**.
