# Sonde MRZ

Receive-only Fusion overlay application for MRZ-N1 / MP3-H1 radiosondes. It
samples the BK4829 AF output on PA4, validates the frame CRC, and displays the
serial number, GPS position, altitude, temperature, relative humidity,
vertical speed, horizontal speed, and heading.

## Status

Version 0.3 supports the two observed MRZ telemetry layouts:

- ECEF position and velocity, 50-byte logical frame;
- latitude/longitude position, 47-byte logical frame.

The parser and 19.2 kHz input path have synthetic host tests. A radio-path
model also runs the generated Flipper signal through the measured BK4829 audio
low-pass, a swept high-pass corner, noise, and sample-clock error. Synthetic
Flipper Zero files cover both layouts, normal and inverted polarity, and CRC
rejection. The receiver has also decoded an operational MRZ sonde on the
radio.

The compact identifier follows the SondeHub convention: the decimal `0xC` and
`0xD` configuration words are concatenated, reduced to 24 bits, and displayed
as `MRZ` plus six hexadecimal digits. Until both subframes arrive, the screen
shows `MRZ`.

## Radio setup

1. Tune the VFO to the sonde frequency in FM wide mode. Operational sondes
   normally use the 400–406 MHz meteorological band.
2. Launch **Sonde MRZ** from the Fusion application list.

The receive path is:

```
MRZ RF -> BK4829 RAW, wide IF -> AF bypass -> PA4 ADC at 19.2 kHz
       -> 2 hysteresis slicers with second-order DPLLs
       -> Manchester 2400 chips/s -> CRC -> telemetry
```

The RAW audio path is high-passed, so each adaptive slicer holds its FSK level
between thresholds at 2/8 or 4/8 of the tracked peak. Its DPLL follows both
phase and chip rate within +/-5%, matching the proven RS41 receive structure.
The raw sync is `0x999999999AA` (44 chips), matched on hard decisions in either
polarity with at most two wrong chips. A wider window is unsafe because the
alternating training sequence matches the repetitive MRZ header four chips
early. Manchester uses `10` for a logical one and `01` for zero. CRC-16 uses
reflected polynomial `0xA001`, initial value `0xFFFF`, and a little-endian
result. A telemetry update is normally repeated six times; the app counts and
flashes only once per UTC second.

The model reproduces why the original integrate-and-dump receiver was
unreliable: it reads 0 of 12 CRC-valid frames through every tested audio
corner. The two-slicer receiver reads 12 of 12 in both polarities from 30 Hz
to 1.5 kHz high-pass, and across +/-3% sample-clock error; at the heaviest
modelled noise point it reads at least 11 of 12 and both UTC updates.
Twenty seconds of modelled receiver noise alone produce no false sync.

The integer ECEF conversion also reuses each CORDIC pass for both angle and
vector magnitude. This removes the square-root routines and uses a common
power-of-two scale for the position and velocity projections. The WGS-84 model
keeps position within about 4 m through the supported -1 to 80 km altitude
range, with vertical speed within 0.1 m/s on its test grid.

## Keys

| Key | Action |
|---|---|
| `1` | Toggle the speaker |
| `2` | Tune +5 kHz; `F` then `2` tunes -5 kHz, up to +/-50 kHz |
| `3` | Cycle 0/6/15/27 dB and the two bypass gains; `F` then `3` cycles backwards |
| `4` | Reset the frequency offset |
| `5` | Toggle the valid-frame beep |
| `F` | Arm/disarm the reverse action for the next key |
| `MENU` | Open GeoNav once a position is decoded; `EXIT` in GeoNav returns here |
| `EXIT` | Quit |

The footer recalls the tuned frequency, any temporary offset, RSSI, frame
count, and front-end attenuation. Speaker, beep, and attenuation settings are
saved per application; the default attenuation is 27 dB. The ECEF conversion,
radio setup, DPLL decoder, UI, and controls all remain self-contained in the
4 KiB overlay.

## Decoded fields

- UTC time, used internally to collapse the six repeated frames;
- ECEF `x/y/z` in centimetres or latitude/longitude in microdegrees;
- altitude in centimetres;
- ECEF velocity in centimetres per second, projected to vertical speed,
  horizontal speed, and heading;
- lat/lon-layout horizontal speed and heading; that layout has no vertical
  speed field, so `VS` is shown as zero;
- temperature in centi-degrees Celsius and relative humidity in centi-percent;
- serial/configuration words from subframes `0xC` and `0xD`.

Satellite count and the date are not displayed. Version 0.3 uses the direct
temperature and humidity fields and does not attempt a raw-sensor calibration
fallback.

## Build and tests

From the repository root, the user can build the overlay with:

```bash
./compile-app.sh sondemrz
```

It produces `build/Apps/SondeMRZ.app` and must fit the 4 KiB overlay area. The
app requires application API level 3 and the Fusion overlay loader.

Host test:

```bash
App/apps/sondemrz/test/run_tests.sh
```

Radio-path model:

```bash
python3 -B App/apps/sondemrz/test/model_rx.py
```

Compact ECEF position and velocity model:

```bash
python3 -B App/apps/sondemrz/test/model_ecef.py
```

Generate the six synthetic Flipper files:

```bash
python3 -B App/apps/sondemrz/test/flipper_mrz.py App/apps/sondemrz/test/flipper
```

Tune the radio to 433.650 MHz FM wide for this bench test. The generated files
are synthetic SRD-band transmissions, not recordings of an operational sonde.
Their test position is `49.7559 N, 2.3522 E`, approximately 100 km north of
Paris, which makes GeoNav distance and bearing checks meaningful.
Use only a frequency permitted by the Flipper's configured region.

Start with the Flipper several metres from the radio. A bench transmitter
placed beside the antenna can overload the receiver; key `3` selects more or
less front-end attenuation without leaving the app. A valid file contains two
distinct UTC updates (six RF repeats each), therefore the expected final
counter is `FRAME 2`, not 12.

## Version

Current: **v0.3**.
