# POCSAG RX (overlay app)

Receives **POCSAG** pager transmissions at 1200 bauds, as the amateur
[DAPNET](https://hampager.de) network sends them, and shows the pages: address
(RIC), text, function. Since 1.0 the BK4829's own FSK modem demodulates them;
up to 0.9 the app sampled the RX audio and sliced it in software.

## Status

| Part | State |
|---|---|
| v1.0: the BK4829's FSK modem decodes | POCSAG is direct FSK, the modem's "FSK 1.2K" mode: the chip keeps the bit clock, finds the sync codeword and hands over the bits; the app checks BCH and builds the pages. Same screen, keys and saved settings as 0.9, the function-0 alpha choice included. 512 bauds and the preamble-rate readout are gone (see "Limits"). 3372 B instead of 3904. **The decoder was measured on a UV-K1 on a live 1200-baud paging channel** (see "Hardware decoding") with two test builds that ran it behind another screen; this exact build: emulator only so far (`test/emu_rx.py`, every Flipper case below) |
| v0.9: automatic function-0 alpha | Function 0 remains numeric by default, as specified by POCSAG, but is also decoded as 7-bit text. The alpha view replaces the numeric one only when it is clean letters-and-spaces prose with at least three word-like groups, two spaces, enough letters and a clear score margin. The three observed non-standard pages (`332450`, `337265`, `338909`) decode as `CAVSAV sur VTUB`, `CODB sur VTUB`, `EQID sur VTUB`; short, punctuated, numeric or ambiguous non-standard pages remain numeric by design. Unchanged in 1.0 (`test/format_auto.py`) |
| Format (`test/pocsag.py`) | Self-tested: the sync and idle codewords come out of the BCH encoder, pages round-trip, one wrong bit per codeword is corrected, two are detected |
| v0.5: 10 pages, bell mark, Text Lost | 10 pages kept (100 B of stack each: 1000 B for the 10), the "page shown / pages kept" capsule on two digits (`10/10`). Key 4 on shows a bell between the title and the speaker icon. A page without text shows `Tone Only` in a capsule under the address, or `Text Lost` when its message codewords came but none could be read. Unchanged in 1.0 |
| v0.4: tone-only pages | Key 4 keeps the pages without text (saved, off by default). Unchanged in 1.0 |
| Up to 0.9: the software decoder | Hysteresis slicers on the RX audio (PA4 at 9.6 kHz), a 1200 and a 512-baud lane. Checked on the radio with the Flipper files; the 512 lane stayed model-only. See "Before 1.0" |

## Hardware decoding

The radio's audio path cuts the low frequencies, even with its filters off,
and a high-passed NRZ level droops back to zero during a run of equal bits:
0.1 to 0.9 had to work around it (hysteresis slicers, "Before 1.0"). The
BK4829's FSK modem takes the bits before that path. Its set-up follows
Ferrite's POCSAG receiver for the Radtel RT-950, also a BK4829
(`crates/ferrite-radio/src/chip.rs`, https://github.com/FerriteFW/ferrite):

| Register | Value | |
|---|---|---|
| REG_70 | `00C3` | tone 2 on: the FSK rate generator |
| REG_72 | `3065` | 1200 bauds (x 10.32444, as AirCopy) |
| REG_5A / 5B | `7CD2` / `15D8` | the sync word: POCSAG's sync codeword |
| REG_5C | `5625` | the chip's CRC off (AirCopy's `5665`, bit 6 clear) |
| REG_5D | `F7E0` | frame of 2040 bytes: 30 batches, each with the next one's sync codeword |
| REG_5E | `3204` | FIFO almost full at 4 words (AirCopy's) |
| REG_58 | `00C1` | FSK 1.2K receive, modem on |
| REG_59 | `40F8` then `10F8` | RX FIFO cleared, then RX on; preamble 16 bytes, sync 4 bytes |
| REG_3F | `3002` | interrupts: sync found, FIFO almost full, frame done |

The modem needs a preamble before its sync word, so it finds only the first
batch's sync; it then reads on, the next batches' sync codewords included, as
data. The app reads four FIFO words at each almost-full interrupt. A FIFO
word holds the first byte off the air in its low half. After each batch the
next sync codeword tells the polarity (the modem syncs on both); when it is
missing the transmission is over, and the modem is turned off and on (RX off
and on alone does not end its frame). The first four codewords of a frame,
all past help, are a false sync: dropped.

Measured on a UV-K1, 2026-11-27, on a local 1200-baud paging channel, with a
test build that ran this decoder and rotated four modem settings every minute
(the RX audio filters and AFC as found, or off; REG_58 `00C1`, `00C9` or
`00E9`):

| 30 min, live traffic | |
|---|---|
| sync codewords | 60, no false sync |
| codewords | 1440: 1386 right, 42 corrected, 12 past help |
| FIFO reads | 830 almost-full interrupts; the fallback that read words by time was never needed |
| byte order | 1432 valid codewords with each word's bytes swapped, 0 without |

The four settings did not differ beyond what the traffic itself varies by
(the RX audio filters do not touch the modem's input): 1.0 leaves the chip
as RADIO_SetupRegisters set it. On the same channel, the same decoder behind
another screen showed far fewer `?` than 0.9.

## Use

1. Tune the VFO (FM, wide) to the pager frequency: **439.9875 MHz** for DAPNET
   where a transmitter is in range (see the coverage map on hampager.de), or
   **433.650 MHz** for the Flipper Zero bench files.
2. Launch POCSAG RX.

The screen follows APRS RX: the address in bold with the counters at its right
(pages received, page shown / pages kept), the text under it, the dotted
separator, the frequency in big digits, and the bottom line
`-89dBm  func 3  last 02m15`: level (at the transmission's sync), function
(0-3), time since the page came. A new page wakes the backlight and blinks the
green LED twice.

Until a first page, the bottom line already shows what the modem gave, so key
3 has no visible effect. After a page, key 3 toggles this diagnostic line with
the page's level and age. It reads `sync 12 ok 340 fix 9 bad 3`: the sync
codewords the chip found, then the codewords right as read, put right (one
wrong bit) and past help, since the launch or key 2.

| Key | Action |
|---|---|
| UP / DOWN | Scroll the text a row when it overflows (normal view); at either end, the newer / older page (UV-K1: LEFT / RIGHT). 10 pages kept |
| * | Normal view (18 characters a row, 3 rows) / compact view (32 tiny characters a row: a whole 80-character page); saved |
| 1 | Speaker on / off (saved, off by default). Not gated by the squelch: the hiss is heard between transmissions, and a transmission as the raw buzz it is |
| 2 | Clear the pages and the counters |
| 3 | After a page: toggle level and age / the modem's counters. Before the first page, the counters are already shown |
| 4 | Keep the pages without text (tone-only pages) on / off (saved, off by default). While on, a bell sits between the title and the speaker icon; such a page shows `Tone Only` in a capsule under the address, or `Text Lost` when characters came but none could be read |
| EXIT | Quit |

The keys and the screen are served every 50 ms, during transmissions too:
the modem's FIFO holds the bits meanwhile.

## Limits

- **1200 bauds only.** The modem has a 2.4K mode (REG_58 `00C9`, REG_72 at
  2400 bauds, as Ferrite and the RT-950's stock firmware use it): 2400-baud
  POCSAG is a next step, untested here. It has no 512-baud mode, and 0.9's
  512-baud lane was never checked on the air; it is gone, and so is the
  preamble-rate readout of key 3.
- **One modem frame is 30 batches (13.6 s).** The modem syncs again only
  after a preamble: the batches of a longer transmission after the 30th are
  lost. None came in the 30 minutes measured.
- **Function 0 is numeric by default, 1-3 are text**, as specified by POCSAG.
  Function 0 is also tried as 7-bit text: a conservative heuristic selects it
  only when it is clean letters-and-spaces prose and looks clearly more
  plausible than the numeric interpretation (`test/format_auto.py`).
- **A `?` is a character from a codeword that could not be corrected** (two
  wrong bits or more). Three such codewords in a row end the page.
- **A page without text is not shown** (tone-only pages) unless key 4 keeps
  them. A page with more characters lost than read is dropped.
- Pages are kept in RAM only.

## Testing with a Flipper Zero

Here the Flipper sends the real thing: direct FSK, as a pager transmitter does
(+-2.38 kHz instead of +-4.5 kHz: not yet tried with the modem, which was
measured on a pager transmitter's deviation).

```sh
python3 test/flipper_pocsag.py test/flipper
```

| File | Expected on the radio |
|---|---|
| `pocsag_alpha.sub` | `1234567`, `F4HWN: POCSAG RX test on 433.650 MHz` |
| `pocsag_numeric.sub` | `1234560`, `0145 67-89`, func 0 |
| `pocsag_func0alpha.sub` | Three non-standard function-0 alpha pages: `332450` / `CAVSAV sur VTUB`, `337265` / `CODB sur VTUB`, `338909` / `EQID sur VTUB` |
| `pocsag_func0numeric.sub` | Regression guard: `340000` / `46*7(4-(601)8 *(0**2U` must remain numeric, func 0 |
| `pocsag_two.sub` | Two pages: `2504` and `165856` |
| `pocsag_long.sub` | `1000001`, a 72-character text (scroll, or `*`) |
| `pocsag_same.sub` | Two pages to the same address `4520`, with different texts: both kept |
| `pocsag_inverted.sub` | Same as alpha: the other polarity |
| `pocsag_biterr.sub` | Same as alpha: one wrong bit in every other codeword, corrected |
| `pocsag_gap.sub` | The long page with one codeword destroyed: `The q???? brown fox ...` |
| `pocsag_tone.sub` | A tone-only page: `1234567`, func 1, no text. Shown as `Tone Only` with key 4 on, nothing with key 4 off |
| `pocsag_tonemix.sub` | A tone-only page `1234560` (func 0), then `1045677`, `After a tone-only page`: with key 4 off, only the second one |

The `pocsag512_*.sub` and `pocsag2400_alpha.sub` files (`--baud 512`,
`--baud 2400`) are not decoded since 1.0.

Copy them to `subghz/` on the SD card, tune the radio to 433.650 MHz FM, launch
POCSAG RX, open a file and press Send (1.5 s each).

## Emulator (`test/emu_rx.py`)

The built app (`pocsagrx.bin` and its assets) runs on an emulated Cortex-M0
(Unicorn, `pip install unicorn`) against `test/bk4829_fsk.py`, a model of the
modem as above, fed with every file of the table: each page the app shows
(walked with DOWN in the compact view) is compared with the one sent. Also: a
tone-only page with key 4 off (not shown) and 20 s of noise (nothing). Any
write outside the overlay, the stack and the frame buffer, and any character
outside 32-126 handed to a font, fails the run. 1.0: every case passes.

`../app_screenshot.py pocsagrx` feeds the same model (scenario `fsk`).

## Before 1.0

0.1 to 0.9 read the RX audio on PA4 at 9.6 kHz (receiver in RAW: RX HPF300 /
LPF3K / de-emphasis and AFC off) and sliced it in software: a box filter of
about half a bit, then three hysteresis slicers per lane (1/4, 3/8, 1/2 of the
peak), each with its DPLL, BCH check and page assembly, the slicers' copies of
a page merged. The audio path's low cut made the slicers flip on the pulse a
change of bit leaves instead of comparing a level; feedback slicers read
nothing on the radio. A 512-baud lane ran beside the 1200 one (model only),
and the rate of each preamble was told (2400 included). Keys and screen had
to wait for the sync codeword of each batch. That decoder and its channel
model (`test/model_rx.py`) are in the history up to 0.9.

## Files

- `pocsagrx_app.c`, `gen_assets.py`, `build.sh`, `app.ld`: the app.
- `test/pocsag.py`: the format (encode, decode), self-tested; the function-0
  heuristic's reference (`prefer_alpha`).
- `test/format_auto.py`: focused tests for automatic function-0 alpha selection.
- `test/flipper_pocsag.py`: the Flipper `.sub` files.
- `test/bk4829_fsk.py`: the BK4829 FSK modem model.
- `test/emu_rx.py`: the built app against that model, every Flipper case.
