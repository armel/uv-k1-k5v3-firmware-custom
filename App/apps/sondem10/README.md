# Sonde M10

Receive-only overlay application for standard Meteomodem M10 radiosondes. It
samples the BK4829 AF output on PA4, validates the M10 checksum, and displays
the serial number, GPS position, altitude, outside temperature, relative
humidity, vertical speed, horizontal speed, and heading.

## Status

Version 0.6 implements the standard M10 `0x9F` telemetry frame, outside
temperature and humidity conversion, and the GPS velocity, and opens GeoNav
on `MENU`. The parser and soft-input path have a synthetic host
test. Live reception has also been validated in the field with an operational
M10 radiosonde transmitted from Trappes, France.

Not implemented yet:

- M10+ `0xAF` frames;
- periodic `0x49` satellite-level frames.

## Radio setup

1. Tune the VFO to the M10 frequency in FM wide mode. Operational weather
   sondes are normally found in the 400–406 MHz meteorological band.
2. Launch **Sonde M10**.
3. If a nearby sonde overloads the receiver, use key `3` to increase front-end
   attenuation.

The application uses the same proven receive path as Sonde M20:

```
M10 RF -> BK4829 RAW, wide IF -> AF bypass -> PA4 ADC at 19.2 kHz
       -> edge DPLL -> differential Manchester -> checksum -> telemetry
```

M10 nominal timing is about 9615 chips/s, so 19.2 kHz provides just under two
samples per chip. The decoder DPLL tracks this small difference.

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

- raw synchronization header `0x99994C99`;
- differential Manchester;
- frame lengths `0x64`, `0x66`, and `0x76` with type `0x9F`;
- M10 linear 16-bit checksum;
- Trimble GPS packet `0x20` position and altitude;
- standard five-byte M10 serial number.
- Shibaura PB5-41E thermistor temperature, converted with a compact lookup
  table and integer interpolation;
- GPS velocity east / north / up at `0x04` / `0x06` / `0x08` (big-endian
  int16, 1/200 m/s, as `m10mod`): vertical speed (`VS`), horizontal speed
  (`HS`, whole m/s) and heading (`HDG`, degrees from north, a table-free
  `atan` within 0.3 degree), with a valid GPS fix;
- relative humidity from the capacitive sensor, as `m10mod`: the ratio of its
  555 timer count (24-bit LE at `0x35`) to the 55 %RH reference count
  (`0x32`), RH = (ratio - 0.8955) / 0.002, plus `m10mod`'s corrections below
  0 C and -30 C from the outside temperature, clamped to 0..100 %, in integers
  (within 0.8 %RH of `m10mod`'s floating point, rounding included).

Screen body, four fixed rows: the position (4 decimals, about 11 m, so it
always fits one row), `ALT …m VS±…`, `HS …m/s HDG …`, `T …C RH …%`.

Rows 2 to 4 hold the same kind of data in every sonde app (M10, M20, RS41);
a missing field leaves its row blank.

Frames with an invalid GPS fix still count as valid received frames and show
their serial number, but no position is displayed.

Each valid received frame triggers two short flashes of the green LED. When
enabled with key `5`, it also triggers one short decode beep; the Foxhunt signal
icon in the status bar indicates that the beep is active.

## Build

From the repository root:

```bash
./compile-app.sh sondem10
```

The result is `build/Apps/SondeM10.app` and must fit the 4 KiB overlay. The
firmware must include the overlay-app loader. At present, the repository's
plain `Fusion` preset leaves that loader disabled; use the project's intended
overlay-enabled firmware configuration when installing the app.

Host test:

```bash
App/apps/sondem10/test/run_tests.sh
```

Synthetic Flipper Zero bench files can be generated without using a real M10:

```bash
python3 -B App/apps/sondem10/test/flipper_m10.py App/apps/sondem10/test/flipper
```

Tune the radio to 433.650 MHz FM wide for this bench test. The generated files
are synthetic SRD-band transmissions, not recordings of an operational sonde.
Use only a frequency permitted by the Flipper's configured region.

## Version

Current: **v0.6**.
