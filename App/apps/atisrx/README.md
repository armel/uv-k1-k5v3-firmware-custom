# ATIS RX (overlay app)

Receives the **ATIS** identification that inland-waterway VHF radios send at the
end of each transmission (when the PTT is released), and shows who was talking:
call sign, 10-digit identity, country. ATIS is mandatory on the European inland
waterways (RAINWAT); it is sent on the working channel itself, not on channel 70.

## Status

| Part | State |
|---|---|
| Signal format (`test/atis.py`) | Built from ETSI EN 300 698 V2.3.1 annex B; reproduces the example of annex C (`PC8075` -> `9244038075`, error check 110) |
| Receive model (`test/model_rx.py`) | Decodes the Flipper files and a sine FSK with -6 to +4.2 dB of twist, +-4 kHz offset, +-1 % clock, in the standard FM audio path the app reads; nothing out of 60 s of noise |
| Radio app (`atisrx_app.c`) | v0.1 decoded the Flipper files on the radio (2026-10-13), but only 7 sequences in 40 from a sine FSK source at 1.5 to 4.8 kHz of deviation (#705). **v0.2** (slicer weights 5:4, 4:3, 7:5) decodes 39 in 40 on 433.650 MHz, and on 144.950 MHz 30/30 in narrow mode, 26/30 in wide mode, 10/10 at 5 kHz of deviation |
| On air | Not tested yet with a real boat (the Flipper sends square-wave tones, not a type-approved radio's signal) |

## Use

1. Tune the VFO (FM) to the channel to watch: a marine VHF working channel, or
   **433.650 MHz** for the Flipper Zero bench files.
2. Launch ATIS RX. `WAIT` shows in the status bar until a first station.
3. Each transmission heard ends with a 0.3 s burst: the station comes to the
   front of the list.

The screen follows APRS RX: the call sign in bold with the counters at its
right (sequences received, station shown / stations kept), the identity and the
country under it, the dotted separator, the frequency in big digits with the
marine channel at its right (`CH 10`) when the frequency is a ship-station one,
and the bottom line `-89dBm  heard 3  last 02m15`: level, times heard since the
launch (or the last clear), time since the last one, `02m15` (minutes, seconds)
under an hour then `03h07` (hours, minutes), refreshed every 5 s.

| Key | Action |
|---|---|
| UP / DOWN | The newer / older station (UV-K1: LEFT / RIGHT), up to 8 kept; marks in the status bar while there is one |
| 1 | Speaker on / off (saved, off by default: the decoder does not need it; while on, the squelch gates it) |
| 2 | Clear the list |
| EXIT | Quit |

## The identity

`9 MID X1X2 X3X4X5X6` (clause B.1.11): `MID` is the country, `X1X2` the second
letter of the call sign (01 = A), `X3..X6` its four figures; the first letter
is implied by the country. The app rebuilds the call sign when `X1X2` is 01-26
and the MID is in its table; otherwise it shows the ten digits (identities
derived from an MMSI).

The table (`MIDS` in `test/atis.py`, 30 entries, mostly RAINWAT countries) was
typed from memory of the ITU list, and the first letter is a guess for the
countries with several call-sign series: **check it before trusting a country
name or a first letter**. The ten digits are always exact.

## Testing with a Flipper Zero

The Flipper cannot reach the marine band, and must not transmit there. The
bench files play the sequence on 433.650 MHz, as the APRS RX ones:

```sh
python3 test/flipper_atis.py test/flipper
```

| File | Expected on the radio |
|---|---|
| `atis_pc8075.sub` | `PC8075`, `9244038075`, Netherlands (the standard's example) |
| `atis_fm1234.sub` | `FM1234`, `9227131234`, France |
| `atis_dc5513.sub` | `DC5513`, `9211035513`, Germany |
| `atis_mmsi.sub` | `9227999123` (no call sign), France |
| `atis_badecc.sub` | Nothing: wrong error check |

Copy them to `subghz/` on the SD card, tune the radio to 433.650 MHz FM, launch
ATIS RX, open a file and press Send. Sending the same file again raises its
`heard` counter; another file adds a station. The Flipper sends each tone as a
square wave (its CC1101 only switches between two frequencies), as a boat does
not: a pass with the Flipper proves the decoder, not the reception of a real
radio (pre-emphasis, deviation).

## How it decodes

1200 bauds, 1300 Hz for a 1 (Y) and 2100 Hz for a 0 (B). APRS RX's chain with
these tones: PA4 audio on ADC channel 4 at 9.6 kHz, band-pass, sliding
correlators over one bit, per-tone AGC, three slicers. Each slicer hunts the
phasing sequence in its last 30 bits (DX 125 / RX 111..104 alternately), which
gives the symbol phase and the position; it then reads the 34 symbols. Each
information symbol is taken from its DX copy, or from its RX copy sent 5
symbols later when the DX one fails its check bits; the format specifier (121),
the end of sequence (127), the digits and the error check must all hold.

The three slicers decide sign(wa * My * Pb - wb * Mb * Py), each tone magnitude
over the other tone's peak, with weights 5:4, 4:3 and 7:5. The former 2:3, 1:1
and 3:2 suit the model, but not a UV-K1's audio: on ADC samples captured on the
radio during real calls (ATIS DSC RX shares this front end; about 0.16 LSB per
Hz of deviation, the 2100 Hz tone 1.3 to 3 dB down), only 3:2 gave the right
bits, at the edge of the good range (84 to 99 %), 1:1 about 70 % and 2:3 about
60 %, and v0.1 decoded 7 sequences in 40 from a LimeSDR. Around 4:3 every
capture keeps 99.5 to 100 % of the bits right, and v0.2 decodes 39 in 40. The
model cannot tell the two sets apart in the standard audio path; with the new
weights its flat RAW path no longer decodes, which the app does not use.

The Flipper's `2FSKDev238` preset deviates about +-4.8 kHz (measured with an
SDR), more than a marine radio usually does: v0.1 still decoded 6 sequences in
10 there, which is why the bench files passed.

The clock recovery pulls 1/8 of the error per transition (1/4 in APRS RX):
with 1/4 the model lost half the noisy sequences, the symbols having few
transitions; 1/8 still follows a +-1 % clock.

## Files

- `atisrx_app.c`, `gen_assets.py`, `build.sh`, `app.ld`: the app.
- `test/atis.py`: the format (encode, decode, call-sign conversion), self-tested.
- `test/flipper_atis.py`: the Flipper `.sub` files.
- `test/model_rx.py`: channel + the integer demodulator the C transcribes; run
  it after any change to the demodulator.
