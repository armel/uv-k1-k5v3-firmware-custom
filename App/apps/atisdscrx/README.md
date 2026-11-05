# ATIS DSC RX (overlay app)

Receives the **VHF DSC** (Digital Selective Calling, ITU-R M.493) of marine
**channel 70** (156.525 MHz) and shows each call: what it is (distress,
distress acknowledgement or relay, urgency, safety, routine, test), who sent it
(MMSI, country), to whom, and for a distress the nature, the time and the
position. A new distress or urgency call is signalled three times: the
screen inverted, the green LED and a beep. Receive only.

It is the companion of [ATIS RX](../atisrx/README.md): ATIS is a DSC call with
format specifier 121, sent on the working channels of the inland waterways.
The two apps share the modulation, the ten-unit code and the receive path;
ATIS DSC RX reads the variable-length calls of channel 70.

## Status

| Part | State |
|---|---|
| Call format (`test/dsc.py`) | Built from ITU-R M.493 (annex 1); an ATIS sequence built with it is bit for bit the one of `atisrx/test/atis.py` (ETSI EN 300 698 annex C) |
| Receive model (`test/model_dsc.py`) | ATIS RX's channel and front end with the DSC sequence decoder, in the standard FM audio path the app reads: the 7 test calls decoded from the Flipper files, with +-4 kHz offset, +-1 % clock, noise up to 3000 Hz rms, sine FSK from -6 to +4.2 dB of twist; the bad error check rejected; nothing out of 60 s of noise. The samples captured on a UV-K1 (`test/captures/`) check the slicers: 99.5 to 100 % of the bits right |
| Radio app (`atisdscrx_app.c`) | v0.10, 3656 B of code. Host test (`test/host_test.py`, the app compiled for the host): the 7 calls shown as expected, three alerts for a distress or urgency call, none otherwise, two calls in a row, nothing from the bad error check or 30 s of noise, 93 to 94 of 96 calls with the radio's key-scan and blit gaps |
| On the radio | **Tested on a UV-K1** with a LimeSDR sending sine FSK on 433.650 MHz (-56 dBm, radio 1 m away): v0.10 decoded 10/10 distress calls at each of 1.5, 2.5 and 3.5 kHz of deviation (v0.9, slicers 5:4, 4:3, 3:2: distress acknowledgement 10/10, urgency 10/10, routine 9/10, distress 9/10). The three alerts seen through K5Viewer |
| On air | Not tested yet with a real DSC call on channel 70 |

### The slicer weights

ATIS RX decides each bit with three weights between the 1300 and 2100 Hz tone
magnitudes, 2:3, 1:1 and 3:2, which suit the model. The UV-K1's audio is not
the model's: a diagnostic build captured the ADC samples of real calls
(`test/captures/`, 9.6 kHz, 8-LSB steps, about 0.16 LSB per Hz of deviation,
the 2100 Hz tone 1.3 to 3 dB down), and on them only 3:2 gave the right bits
(98 to 99 %), 1:1 70 to 78 % and 2:3 about 60 %: a decoder resting on one
slicer, which lost 4 calls in 10. The right decisions lie around 4:3; over six
captures (1.5 to 3.5 kHz of deviation), 5:4, 4:3 and 7:5 keep 99.5 to 100 % of
the bits right in every one, while 6:5 and 3:2 fall to 84 % in one of them. ATIS
DSC RX runs 5:4, 4:3 and 7:5. The flat RAW path of the model no longer decodes with them; the app does
not use it. ATIS RX shares the front end and may well gain from the same
weights on a UV-K1.

## Use

1. Tune the VFO (FM) to **156.525 MHz** (channel 70), or to **433.650 MHz**
   for the Flipper Zero bench files.
2. Launch ATIS DSC RX. `WAIT` shows in the status bar, the frequency and
   `CH 70` on the screen, until a first call.
3. Each call comes to the front of the list; a distress or urgency call
   flashes the screen and the LED and beeps, three times (the beep is heard
   whatever key 1 says).

The call view, for a distress acknowledgement:

```
DISTRESS ACK      3   (bold title, calls received, "2/6" once several are kept)
002275300 France      the sender (here a coast station) and its country
TO ALL SHIPS          or TO <MMSI>, TO AREA; nothing for a distress alert
MAYDAY 227006480      the vessel in distress (acknowledgement, relay)
SINKING 14:35         nature of distress, time UTC (--:-- when unknown)
43 18N 005 22E        position, degrees and minutes (POSITION ? when unknown)
-73dBm  heard 1  last 00m12
```

For an urgency, safety, routine or test call, the last row is the first
telecommand and the proposed channel (`F3E SIMPLEX CH16`).

| Key | Action |
|---|---|
| UP / DOWN | The newer / older call (UV-K1: LEFT / RIGHT), up to 6 kept |
| 1 | Speaker on / off (saved, off by default; while on, the squelch gates it) |
| 2 | Clear the list |
| EXIT | Quit |

The country comes from the first three digits of the MMSI (after the `00` of a
coast station or the `0` of a group). The table (`MIDS` in `test/dsc.py`, 59
entries: the European coasts and the main flags of convenience) was typed from
memory of the ITU list, as ATIS RX's: **check it before trusting a country
name**. The nine digits are always exact.

## Testing with a Flipper Zero

The Flipper cannot reach channel 70, and a DSC call must never be sent there
for a test: a false distress alert is an offence. The bench files play the
calls on 433.650 MHz:

```sh
python3 test/flipper_dsc.py test/flipper
```

| File | Expected on the radio | Alert |
|---|---|---|
| `dsc_distress.sub` | `DISTRESS`, `227006480 France`, `SINKING 14:35`, `43 18N 005 22E` | yes |
| `dsc_dist_nopos.sub` | `DISTRESS`, `UNSPECIFIED --:--`, `POSITION ?` | yes |
| `dsc_dist_ack.sub` | `DISTRESS ACK` from `002275300`, `TO ALL SHIPS`, `MAYDAY 227006480`, nature, position | yes |
| `dsc_urgency.sub` | `URGENCY` to all ships, `F3E SIMPLEX CH16` | yes |
| `dsc_safety.sub` | `SAFETY` to all ships, `F3E SIMPLEX CH67` | no |
| `dsc_routine.sub` | `ROUTINE`, `TO 228123456`, `F3E SIMPLEX CH72` | no |
| `dsc_test.sub` | `TEST`, `TO 002275300` | no |
| `dsc_badecc.sub` | Nothing: wrong error check | no |

The MMSIs are made up. As with ATIS RX, the Flipper sends square-wave tones: a
pass proves the decoder, not the reception of a type-approved radio. Measured
with a LimeSDR, the Flipper's preset deviates about +-4.8 kHz, not 2.38: the
frames are bit-exact (-20 ppm clock), but stronger than a marine radio sends.

## How it decodes

The front end is ATIS RX's, unchanged: PA4 audio on ADC channel 4 at 9.6 kHz,
band-pass, 1300 / 2100 Hz sliding correlators over one bit, per-tone AGC, three
slicers with their DPLL, each hunting the phasing sequence (DX 125 / RX
111..104).

A call has a variable length, so each slicer then reads symbols until an end
of sequence (117 acknowledgement requested, 122 given, 127 other) shows at
character 8 or later: from its DX copy, or from its RX copy when the DX one
failed its check bits. The call ends with the RX copy of the error check, 7 (or
2) symbols later. The earliest such end wins: the DX tail (`EOS ECC EOS EOS`)
sends an end of sequence two characters on before the RX copy of the real one
comes in, and the model caught that case decoding a wrong call. Each character
is then taken from its DX copy, else its RX copy; there must be no earlier end
of sequence, a known format specifier (102, 112, 114, 116, 120, 123) and a
valid error check. The fields are read from the stored call when it is drawn.

## Files

- `atisdscrx_app.c`, `gen_assets.py`, `build.sh`, `app.ld`: the app.
- `test/dsc.py`: the call format (build, decode, fields), self-tested.
- `test/model_dsc.py`: the receive model (imports ATIS RX's channel and front end).
- `test/flipper_dsc.py`: the Flipper `.sub` files (`test/flipper/`).
- `test/host_test.py`: the app compiled for the host (Linux) and run on the
  model's samples; run it after any change to the decoder or the screen.
- `test/captures/`: ADC samples captured on a UV-K1 during distress calls
  (1.5, 2.5 and 3.5 kHz of deviation), little-endian 16-bit, checked by
  `test/model_dsc.py`.

## Credits

Receive path from ATIS RX by Armel F4HWN.
