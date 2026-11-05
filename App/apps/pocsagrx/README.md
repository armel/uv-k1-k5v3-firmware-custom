# POCSAG RX (overlay app)

Receives **POCSAG** pager transmissions at 1200 bauds, as the amateur
[DAPNET](https://hampager.de) network sends them, and at 512 bauds, both at
once, and shows the pages: address (RIC), text, function. Key 3 also shows the
rate of the last preamble heard, 2400 bauds included (told, not decoded).

## Status

| Part | State |
|---|---|
| Format (`test/pocsag.py`) | Self-tested: the sync and idle codewords come out of the BCH encoder, pages round-trip, one wrong bit per codeword is corrected, two are detected |
| Receive model (`test/model_rx.py`) | See "Model results" below |
| Radio app (`pocsagrx_app.c`) | v0.2 (three hysteresis slicers). v0.1 on the radio (2026-10-13): with feedback slicers only, the Flipper transmission was heard but nothing decoded, not even a sync codeword. **With the hysteresis slicers, every Flipper file decodes as expected** (`alpha`, `long`, `gap` with its `?` marks, `same`, `two`, ...), and key 3 read `syn 2 ok 0 0 0 32 32` for the five slicers of that build: only the 1/4 and 1/2 hysteresis ones. The feedback slicers and the 1/8 one were then removed and a 3/8 one added: that is v0.2, **checked on the radio the same day** with `alpha` (key 3 counters), `gap` and `same` |
| v0.5: 10 pages, bell mark, Text Lost | 10 pages kept instead of 5 (100 B of stack each: 1000 B for the 10), the "page shown / pages kept" capsule on two digits (`10/10`). Key 4 on shows a bell between the title and the speaker icon in place of the `T` capsule. A page without text shows `Tone Only` in a capsule under the address, or `Text Lost` when its message codewords came but none could be read (v0.4 showed those as tone-only). Not built or checked on the radio yet |
| v0.4: tone-only pages | Key 4 keeps the pages without text (saved, off by default). Model: with it off, the results are unchanged; with it on, the pages with text are the same in all 264 runs, 3 false tone-only pages appear, and 90/90 real tone-only transmissions (alone, two, or before a text page; 1200 and 512 bauds; clock 1 % off) decode exactly. 60 s of noise gives nothing. **Checked on the radio** (UV-K1) with `pocsag_tone.sub` and `pocsag_tonemix.sub`, key 4 on and off |
| v0.3: 512 bauds and preamble rate | A 512-baud lane (8-sample box, its own three slicers) runs beside the 1200 one, and key 3 shows the rate of the last preamble. The 512-baud lane remains model-only (see "Model results"); Flipper files `pocsag512_*.sub` and `pocsag2400_alpha.sub` |
| On air | Not tested: no DAPNET transmitter heard yet |

## What the radio's audio path does to POCSAG

POCSAG keys the carrier frequency with the bits themselves: the discriminator
outputs the bit stream, not tones, with runs of up to 13 equal bits (11 ms).
The radio's audio path cuts the low frequencies, even with its filters off
(RAW), and a high-passed level droops back to zero during a run: a slicer that
compares the level with a threshold reads noise there. The first build did
exactly that, and found nothing.

The decoder therefore runs hysteresis slicers: each flips its level on the pulse
a change of bit leaves, when it passes a fraction of the signal's peak, and
holds in between, whether the level droops or not. Three of them, at 1/4, 3/8
and 1/2 of the peak.

Slicers that put the baseline back from their own decisions (quantized
feedback) were tried first, alone then beside the others: on the radio they
read nothing, and neither did a hysteresis slicer at 1/8 of the peak. In the
model that picture (1/4 and 1/2 reading, feedback and 1/8 not) is the one of an
audio corner around 1 kHz. Where it really is was never measured.

## Use

1. Tune the VFO (FM, wide) to the pager frequency: **439.9875 MHz** for DAPNET
   where a transmitter is in range (see the coverage map on hampager.de), or
   **433.650 MHz** for the Flipper Zero bench files.
2. Launch POCSAG RX.

The screen follows APRS RX: the address in bold with the counters at its right
(pages received, page shown / pages kept), the text under it, the dotted
separator, the frequency in big digits, and the bottom line
`-89dBm  func 3  last 02m15`: level, function (0-3), time since the page came.
A new page wakes the backlight and blinks the green LED twice.

Until a first page, the bottom line already shows what the decoder reads, so
key 3 has no visible effect. After a page, key 3 toggles this diagnostic line
with the page's level and age. The diagnostic line reads
`pre 512 1200:0/0 512:3/96`. First the rate of the last preamble heard (`pre -`
until one): 512, 1200 or 2400 bauds, read from the preamble itself, so a
transmission at a rate the app does not decode (2400) still tells what it is.
Then, for each lane, the sync codewords found by its slicers and the good
codewords of its three slicers (thresholds 1/4, 3/8, 1/2) added up: a whole
`pocsag_alpha.sub` is 32 codewords, 96 for the three. It is refreshed with the
screen (a page, a key, or every 5 s); key 2 resets it.

| Key | Action |
|---|---|
| UP / DOWN | Scroll the text a row when it overflows (normal view); at either end, the newer / older page (UV-K1: LEFT / RIGHT). 10 pages kept |
| * | Normal view (18 characters a row, 3 rows) / compact view (32 tiny characters a row: a whole 80-character page); saved |
| 1 | Speaker on / off (saved, off by default). Not gated by the squelch, as EPIRB 406: the hiss is heard between transmissions, and a transmission as the raw buzz it is |
| 2 | Clear the pages and the counters |
| 3 | After a page: toggle level and age / preamble rate and lane counters. Before the first page, diagnostics are already shown |
| 4 | Keep the pages without text (tone-only pages) on / off (saved, off by default). While on, a bell sits between the title and the speaker icon; such a page shows `Tone Only` in a capsule under the address, or `Text Lost` when characters came but none could be read |
| EXIT | Quit |

During a transmission the keys are read twice a second only, once a second at
512 bauds (see "How it decodes"): hold a key half a second, a second at 512.

## Limits

- **1200 and 512 bauds.** 2400 bauds needs more audio band than the radio has:
  its preamble is told (key 3, `pre 2400`), its pages are not decoded.
- **Function 0 is read as numeric, 1-3 as text**, the usual convention; a
  network using another one shows garbage for those pages.
- **A `?` is a character no slicer could read** (a codeword with two wrong bits
  or more). Three such codewords in a row end the page. If the codeword lost
  was the address of the next page, that page's text follows the `?` marks in
  the same page.
- **A page without text is not shown** (tone-only pages) unless key 4 keeps
  them: a slicer that slipped a bit reads shifted codewords, which are often
  valid ones, and they come out as addresses without text. With key 4 on, the
  model (see "Model results") shows 3 such false pages in 264 runs of its
  cases (two with the clock 1 % off, one at the 1500 Hz high-pass corner), and
  every real tone-only page of its 90 runs. A page whose message codewords all
  came bad shows `Text Lost`; so does a tone-only page whose next codeword (an
  idle one) came bad, as a bad codeword cannot tell which it was.
- **With a clock 1 % off** (the MCU runs on its internal oscillator) the model
  still shows, about once in three transmissions, a wrong character or a junk
  page of a few characters from a slicer that slipped.
- Pages are kept in RAM only.

## Testing with a Flipper Zero

Here the Flipper sends the real thing: direct FSK, as a pager transmitter does
(+-2.38 kHz instead of +-4.5 kHz).

```sh
python3 test/flipper_pocsag.py test/flipper
python3 test/flipper_pocsag.py test/flipper --baud 512
python3 test/flipper_pocsag.py test/flipper --baud 2400 --names alpha
```

| File | Expected on the radio |
|---|---|
| `pocsag_alpha.sub` | `1234567`, `F4HWN: POCSAG RX test on 433.650 MHz` |
| `pocsag_numeric.sub` | `1234560`, `0145 67-89`, func 0 |
| `pocsag_two.sub` | Two pages: `2504` and `165856` |
| `pocsag_long.sub` | `1000001`, a 72-character text (scroll, or `*`) |
| `pocsag_same.sub` | Two pages to the same address `4520`, with different texts: both kept |
| `pocsag_inverted.sub` | Same as alpha: the other discriminator polarity |
| `pocsag_biterr.sub` | Same as alpha: one wrong bit in every other codeword, corrected |
| `pocsag_gap.sub` | The long page with one codeword destroyed: `The q???? brown fox ...` |
| `pocsag_tone.sub` | A tone-only page: `1234567`, func 1, no text. Shown as `Tone Only` with key 4 on, nothing with key 4 off |
| `pocsag_tonemix.sub` | A tone-only page `1234560` (func 0), then `1045677`, `After a tone-only page`: with key 4 off, only the second one |

The same tests at 512 bauds are `pocsag512_*.sub` (3.4 s each): the same
pages are expected, and key 3 shows `pre 512` and the 512 lane's counters.
`pocsag2400_alpha.sub` is not decoded: key 3 shows `pre 2400` only.

Copy them to `subghz/` on the SD card, tune the radio to 433.650 MHz FM, launch
POCSAG RX, open a file and press Send (1.5 s each at 1200 bauds).

## How it decodes

PA4 audio on ADC channel 4 at 9.6 kHz (APRS RX's path), receiver in RAW
(EPIRB 406's setting, AFC off), sample times from the absolute clock SSTV uses
(tick count + position in the SysTick period: a long screen update cannot lose
a period). DC removal, then two lanes side by side, one per baud rate: the sum
of the last 4 samples (half a bit at 1200 bauds) or of the last 8 (512 bauds,
18.75 samples a bit), then per slicer a level that flips when that sum passes
a fraction of its peak; a DPLL on the changes of level, stepping 8 or 18.75
samples a bit; the last 32 bits
against the sync codeword `7CD215D8` and its complement, two wrong bits
allowed; then 16 codewords a batch, BCH (31,21) checked from the syndrome with
one bit corrected, and the next sync codeword.

An address opens a page, message codewords fill it, an idle word or another
address closes it. A slicer that just read a bad codeword is not trusted to
correct the next eight. A bad codeword inside a page does not end it: its 20
bits are skipped and the characters they held are marked.

A transmission is at one rate: only that lane finds its sync codeword. The
rate of a preamble (576 bits or more of 1 0 1 0 ...) is read apart, from the
first 1200-baud slicer: the samples between two of its flips are classed (3-5
for 2400 bauds, 6-10 for 1200, 15-23 for 512), and 32 flips in a row in one
class are a preamble at that rate; noise and data runs do not keep one class
that long.

The slicers of a lane read the same transmission and each gives the same page. Two
copies are the same page when their address codeword came at the same sample,
within half a codeword; the text is not compared, so two pages to the same
address with the same text, or the same beginning, stay two pages. The copies
are merged: a character one slicer lost is taken from another that read it.

Keys and screen take up to 15 ms, during which no sample is read. Outside a
batch they are served every 50 ms. Inside a transmission they wait for the
sync codeword that opens each batch (twice a second), where no page bit is
lost; the bit clocks run on over the missing samples, and the sync codeword not
read for that reason is taken as found.

## Model results (`test/model_rx.py`)

v0.3 (2026-10-19), both lanes, the sweep below run at 1200 and at 512 bauds:

- **the 1200-baud results are unchanged**, page for page, from v0.2's model:
  the 512 lane reads no sync codeword in a 1200-baud transmission, and the
  other way round;
- **512 bauds: every case with an audio corner of 30 Hz and above decodes**
  but one (a corner of 1500 Hz with 1500 Hz rms of noise: one codeword of the
  long page lost, shown `???`); a corner of 10 Hz fails 4 cases of 8. Every
  "other case" passes at 512, 33 of 33 (pager deviation, +/-4 kHz offset,
  +/-1 % clock, noise 3000), and so do the holes;
- **preamble rate**: right in every case of both corner tables, 12 of 12 at
  2400 bauds (corners 30 to 1000 Hz, noise up to 3000 Hz rms, both
  deviations), and nothing in 2 x 60 s of noise.

v0.2, the three slicers at 1200 bauds (pages `alpha`, `long`, `inverted`,
`biterr`; audio corner 10 to 1500 Hz; no noise and 1500 Hz rms):

- **corner of 100 Hz and above: every case decodes**, as do the offset, clock
  and numeric cases run at 1000 Hz, and a 15 ms hole in a sync codeword;
- **corner of 10 or 30 Hz: 7 cases of 16 fail**, the page coming out cut short
  or with `?` marks. That range was the feedback slicers', removed because the
  radio is not there (key 3 showed them reading nothing). A receiver whose
  audio path passed the low frequencies would need them back;
- strong noise (3000 Hz rms) at a 100 Hz corner fails too;
- a hole in the middle of a batch marks the characters of the codeword hit and
  keeps the rest; nothing comes out of 2 x 60 s of noise.

The quick comparison that chose the thresholds (pages `alpha`, `long`, `gap`,
two noise seeds, corners 300 Hz to 2 kHz, noise 0 / 1500 / 3000): 75 cases of
90 with 1/4, 3/8 and 1/2, against 58 with 1/4 and 1/2 only; the difference is
all in the 3000 column.

## Files

- `pocsagrx_app.c`, `gen_assets.py`, `build.sh`, `app.ld`: the app.
- `test/pocsag.py`: the format (encode, decode), self-tested.
- `test/flipper_pocsag.py`: the Flipper `.sub` files.
- `test/model_rx.py`: channel + the integer demodulator the C transcribes; run
  it after any change to the demodulator (several minutes).
