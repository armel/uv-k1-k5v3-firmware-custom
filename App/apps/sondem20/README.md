# Sonde M20: on-radio Meteomodem M20 decoder

Goal: an overlay app that decodes Meteomodem M20 radiosonde frames on the
UV-K1 / UV-K5 v3 and shows the serial number, GPS position, altitude,
temperature and relative humidity when present.

Status:

| Step | State |
|---|---|
| Audio path an app can sample (PA4 ADC) | **Done** (same path as EPIRB 406 / APRS RX) |
| Softin + checkM10 decoder (`decm20.c`), host-tested | **Done** (`test/run_tests.sh`) |
| Flipper Zero bench frames | **Generated** (`test/flipper/`), radio validation pending |
| AF bypass (REG_47 AF=9) required for 9600-chip Manchester | **Done** — stock FM AF low-passes ~3–4 kHz and wipes 1-chip runs |
| Mid-sample DPLL (2 ADC samples/chip @ 19.2 kHz) | **Done** — needed for real UV-K1 captures (~9620 chip/s) |
| Radio app (`sondem20_app.c`) | **v1.0** decodes a live M20 on a UV-K1 (ID, lat/lon, alt, T, P); **v1.1** adds the GPS velocity (VS, HS, heading), radio validation pending; **v1.2** adds the optional decode beep; **v1.3** shows the relative humidity instead of the pressure, rows in the common sonde order |

## Using the app

1. Set the VFO to the sonde frequency, **FM, wide bandwidth** (e.g. 432.500 MHz
   for a stock M20 on 70 cm).
2. Launch **Sonde M20**. The speaker is off by default; decoding uses the BK4829
   AF output on PA4 either way.
3. Default front-end attenuator is **27 dB** (A27). If the sonde is very close
   and frames fail, raise ATT further with key `3`. Avoid
   `BYP+` (maximum gain) on a strong signal.

Each valid received frame triggers two short flashes of the green LED. Key `5`
adds one short decode beep; the Foxhunt signal icon indicates that it is active.

| Screen | Content |
|---|---|
| Status bar | `Sonde M20` title; decode-beep icon while enabled; speaker icon while the speaker is on, temporarily replaced by `F` when the reverse modifier is armed |
| Fixed line 0 | Serial number in bold (`YMM-n-bnnnn`, same form as auto_rx / SondeHub) |
| Body | Four fixed rows, the same in every sonde app: latitude / longitude (4 decimals, about 11 m); `ALT …m VS±…`; `HS …m/s HDG …`; `T …C RH …%` when available |
| Dotted separator | Line at y=40 |
| Fixed rows | RX frequency (and ±5 kHz offset), live RSSI; `FRAME n` and current ATT |

Keys (UV-K5 and UV-K1):

| Key | Action |
|---|---|
| 1 | Speaker on/off, saved on exit |
| 2 / `F`+2 | Tune +5 / −5 kHz from the VFO, up to ±50 kHz |
| 3 / `F`+3 | Cycle front-end attenuator forward / backward (0 / 6 / 15 / 27 dB / BYP / BYP+) |
| 4 | Back to the VFO frequency |
| 5 | Decode beep on/off, saved on exit |
| EXIT | Quit (frequency offset restored) |

`F` is a latched modifier: press and release `F`, then press the number key;
the two keys do not need to be held together.

## Signal path on the radio

```
M20 RF --> BK4829 (RAW: no HPF300 / LPF3K / de-emphasis; REG_43 wide)
       --> AF output mode 9 (bypass) --> audio circuit --> PA4
       --> ADC ch4 @ 19.2 kHz (2 samples per Manchester chip)
```

- **RAW receive** is set by the app (REG_2B bits 10/9/8, REG_73 bit 4).
- **AF=9 (bypass)** is mandatory. With the normal FM AF path the BK4829 audio
  is low-passed around 3–4 kHz. M20 Manchester at 9600 chips/s needs ~10 kHz of
  AF bandwidth; without bypass, 1-chip runs disappear, the `0x4520` head may
  still lock, and the CRC always fails.
- **REG_43** is forced to the widest IF filter for the session and restored on
  exit.
- **PA4 bias**: the app holds the pin at mid-scale with the MCU DAC (buffer off,
  code 2048), same approach as EPIRB 406.
- While a frame is being collected (`ST_DATA`), the app does not poll the keypad
  or refresh the LCD: a key scan is far longer than the 52 µs sample budget.
- Between frames, the keypad is polled every 40 ms, so a short press is never
  missed. A poll alone neither redraws nor rearms the decoder: the last ADC
  sample is held over its ~0.2 ms and the sample clock keeps running. While a
  sonde is decoded (one frame a second), no poll falls in the 70 ms around the
  next expected header, where a held sample could flip a header bit. The LCD
  is redrawn on a frame or a key, and every 240 ms only without a frame for 2 s.

## Decoder (`decm20.c`)

Streaming, one call per ADC sample, integer only, freestanding. Follows
auto_rx `m20mod` softin:

1. **DC removal**: exponential mean, frozen while a frame is in progress so bias
   drift does not corrupt late bits.
2. **Chip timing**: edge DPLL with mid-sample boundaries (the chip edge often
   falls between the two ADC samples). Zero crossings are interpolated.
3. **Search**: 32-chip rawheader `1001100…` soft correlation, then the next 32
   chips softin to `0x4520` / `0x4320` (body polarity probed via the MSB).
4. **Bits**: Manchester softin `(s2>=s1)`, differential vs previous man bit.
5. **Parse**: checkM10 CRC; SN; lat/lon ×1e6; altitude; thermistor ADC (table
   lookup in the app for °C); relative humidity as `m20mod` `get_RH`: the
   ratio 6.5 (cal + 80000) / (hum + 80000) (`0x2F`, `0x02`, LE; no reading
   from hum 48000 up) over k = 1 - 5.8e-4 (TU - 25), TU from the RH sensor NTC
   at `0x06` (beta 3650, a fourth `TEMP_TAB` row; off the table, where `m20mod`
   reads -273 C, the main temperature instead), then `m20mod`'s cubic, kept
   within -20..120 and clamped to 0..100 %; in integers, within 0.71 %RH of
   `m20mod` over the sensor's range. The pressure (DF9DQ layout, FW ≥ 7) is no
   longer decoded: the row has room for one of the two;
   GPS velocity east / north / up at `0x0B` / `0x0D` / `0x18` (big-endian
   int16, cm/s, as `mXXmod`): vertical speed, horizontal speed and heading
   (a table-free `atan` within 0.3 degree).

## Tests (`test/`)

```
test/run_tests.sh
```

Needs a host C compiler (`gcc` or `clang`) and bash. Builds `host_decm20`, which
checks `decm20_parse` on a crafted CRC-valid frame and a full softin pass
(rawheader + body through `decm20_push`).

### Flipper Zero bench frames

Generate the Sub-GHz RAW files with:

```bash
python3 -B test/flipper_m20.py test/flipper
```

Copy the generated `.sub` files into `subghz/` on the Flipper, tune the radio
to **433.650 MHz FM wide**, launch Sonde M20, then send one file at a time.
The files use the Flipper's asynchronous 2-FSK preset and transmit the M20
differential Manchester chips directly at 9600 chips/s.

| File | Expected result |
|---|---|
| `m20_valid.sub` | `206-1-01234`, `48.80000N 2.30000E`, `ALT 1200m`, `T 24.1C RH 45%`, and `FRAME 1` |
| `m20_inverted.sub` | Same decoded frame with the opposite RF polarity |
| `m20_badcrc.sub` | Rejected; `FRAME` must not increment |

These are synthetic decoder tests on the 433 MHz SRD bench channel, not a
recording of a real radiosonde. Use only a frequency permitted by the
Flipper's configured region.

## Build

**Recommended (Windows / Linux / macOS):** Docker, same as the rest of the
firmware. From the repo root:

```bash
./compile-app.sh sondem20
```

Requires [Docker](https://docs.docker.com/get-docker/). The first run builds the
`uvk1-uvk5v3` image. The result is `build/Apps/SondeM20.app` (≤ 4 KiB overlay).

On Windows use Git Bash, WSL, or any shell that can run the script and talk to
Docker Desktop — you do **not** need a local ARM GCC for this path.

**Optional native build** (no Docker): install

- [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
  (`arm-none-eabi-gcc` / `objcopy` on `PATH`)
- Python 3

then from this directory:

```bash
./build.sh
```

On Windows, run that from Git Bash or MSYS2 so the script and toolchain agree
on paths. Copy the produced `SondeM20.app` into the radio’s Apps slot with your
usual flash tool.

**Host tests only** (no cross-compiler):

```bash
test/run_tests.sh
```

## Version

`APP_VER` in `build.sh` is bumped for every build that goes on a radio and is
stored in the `.app` metadata. The status-bar title stays `Sonde M20`.
Current: **v1.3**.
