# AF Scope: an oscilloscope and a tuner for the received audio

AF Scope looks at the demodulated RX audio, the BK4829 AF output that APRS RX,
AIS RX, SSTV or the radiosonde apps decode. It is the time half of an instrument whose other half is
[AF Spectro](../afspectro/README.md) (spectrum, sonogram, Bell 202 twist):
`MENU` on the last view hands off to it, and back. It is not an RF analyser
(Spectrum3D is): it sees
`antenna -> BK4829 -> demodulated audio -> PA4 -> ADC`, nothing before.

| Step | State |
|---|---|
| Measurement and screens modelled (`test/model.py`, `test/render.py`) | **Done**: counter at every timebase, TUNE 300 Hz to 6 kHz, FSK pairs, trigger edges, audio path per mode, screens drawn from the model |
| v0.1 (one app, TUNE / SCOPE / SPECTRO) | Built and run on the radio; split in two because of the 4 KiB overlay |
| v0.2 (scope and tuner, hand-off to AF Spectro) | Built and run on the radio; the speaker played the LCD traffic as a flutter |
| v0.3 (AM path, LCD sent with the samples while the speaker is on, lock / clip / speaker icons) | Built and run on the radio: no flutter, LCD intact after leaving |

## Views

| View | Screen |
|---|---|
| SCOPE | The trace over the screen (5 lines), triggered on a rising or falling zero crossing, divisions every 32 px; 0.8, 1.7, 3.3, 6.7 or 13 ms per division (the last two hold 2 or 4 samples per pixel, drawn from their minimum to their maximum: peak detect); gain AUTO, x1, x2, x4, x8, x16; persistence. Under it the frequency counter (`1750Hz`, `1227 / 2079Hz` for an FSK such as Bell 202, 4 digits with leading zeros, blank without three crossings), peak-to-peak and DC, then the RF frequency, the timebase, the gain, the trigger edge (`/` or `\`) and `PERS` |
| TUNE | The frequency of a steady tone in big digits (19.2 kHz, 33 ms blocks, averaged over about four of them), its stability (`STAB +-n`: the last block's distance to that average); a needle against the nearest known tone (`1750 CALL TONE -2`), +-50, 20 or 10 Hz full scale; the period folded over two cycles; RF frequency, RSSI, peak-to-peak. `----` when the audio is not one steady tone (voice, noise, an FSK, two tones) |

The two bottom lines keep every field in its own columns, the numbers
right-aligned, so nothing moves when a value changes length: `       0750Hz  PP
1088  DC    +9`: DC on the right edge, PP midway (frequencies on 4 digits with leading zeros; the field holds
an FSK pair, `1227 / 2079`; blank without a reading; PP on 4 characters, DC on 5, blanks before
shorter values; a pair above 9999 Hz pushes the line one character over), then ` 145.500 3.3ms/d AUTO / PERS` (the RF frequency up to
`1296.000`, the timebase and gain names padded to one width); under TUNE,
` 145.500  RSSI  -87dBm   PP  812`.

Status bar: `AF SCOPE`, `RAW` after it when the filters are off, then left of
the battery a lock while held (else a clipped wave while samples reach the
ADC limits) and the speaker icon while it is on. The demodulation the VFO set
(FM, AM, USB) is not shown: it does not change in the app.

## Keys (UV-K5 and UV-K1)

| Key | Action |
|---|---|
| MENU | SCOPE, TUNE, then AF Spectro |
| UP / DOWN | SCOPE: timebase (up: shorter); TUNE: needle span (up: narrower). UV-K1: LEFT / RIGHT, as `SET_NAV` |
| * | Hold: the screen freezes |
| 1 | Audio path STD / RAW |
| 2 | Speaker on / off (in STD it follows the squelch; in RAW it plays all, the squelch bit staying closed) |
| 3 | SCOPE gain: AUTO, x1, x2, x4, x8, x16 (x1: the ADC's full scale over the trace height; each step doubles) |
| 4 | Trigger edge: rising / falling |
| 5 | Persistence on / off: the traces add up |
| 0 | Clear the persistence |
| EXIT | Quit; view, path, speaker, timebase, gain, edge, persistence and span are saved |

A new view, timebase, path, gain or edge also clears the persistence.

## What it is for

- **Look at a signal**: a call tone, a DTMF digit, a CW beat note, an AFSK
  packet (the 1200 and 2200 Hz cycles and their levels), the edges of a data
  burst at 13 ms per division, clipping, DC, what RAW keeps that STD filters.
- **Read a frequency**: the counter on any trace; TUNE for a steady tone to a
  hertz (a 1750 Hz call tone, a CW pitch, a DTMF digit).
- **Persistence**: overlaid traces show jitter, the spread of an FSK, the
  stability of a level.

## Audio path

`STD` keeps the VFO's AF output and filters: USB for USB (CW and SSB beat
notes; if the squelch was closed at launch the AF output was muted and FM is
assumed), else FM, which AM also uses: an AM VFO has the AM demodulator on
(REG_31 bit 0), read at launch. `RAW` turns the RX audio filters off (REG_2B:
HPF 300 Hz, LPF 3 kHz, de-emphasis) and the AFC (REG_73) and, in FM only,
selects the discriminator bypass (REG_47 AF 9, as the radiosonde apps): in
AM, RAW keeps the AM audio and only drops the filters. The registers are
restored on exit, and the loader re-runs `RADIO_SetupRegisters`. The
hand-off to AF Spectro carries the path and the speaker.

## How it measures

A block is taken at 38.4, 19.2 or 9.6 kHz with nothing else running, except,
while the speaker is on, the screen: its 1048 bytes then go to the LCD with
the samples, 5 after each one at 9.6 kHz, 3 at 19.2 kHz, 1 at 38.4 kHz
(the resident lcd_feed, API level 3), the LCD traffic spread evenly instead
of an 11 ms burst between blocks, which the speaker played as a flutter (the
"helicopter"). The screen then refreshes after every block, as with the
speaker off, except at 1.7 ms per division (every two blocks) and 0.8 ms
(every five, 33 ms). K5Viewer gets a frame every 16 screens (viewer_update),
since blit_full, which sends one, is not used then; with the speaker off it
follows every screen.

The app first drove SPI1 itself, and v0.3's first builds broke the rule that
lcd_feed now keeps in one place (each byte waited out, RXNE, before A0 may
switch): a write queued in SPI1's FIFO under a switching A0 turns pixel data
into LCD commands. A garbled screen until the next power-up, then, with
several bytes per sample checked on BSY (not yet set just after a write), the
whole screen shaking by a pixel (data taken as a start-line command).

SCOPE takes 128 samples for the trigger, then 128 points of 1, 2 or 4
samples, depending on the timebase; TUNE takes 640 samples at 19.2 kHz. The
rising zero crossings (hysteresis pp/8, linear
interpolation to 1/256 sample) give the mean period; a second pass sums the
periods more than 1/8 longer and shorter than it. With fewer than 1/8 of them
the block is one steady tone; else the two are an FSK pair, each the mean of
its periods. In the model the TUNE reading is within 0.04 % from 300 Hz to
6 kHz, and a 1750 Hz tone of 120 ADC codes peak to peak in noise still reads
within 2 Hz.

**Accuracy.** The audio frequencies are as good as the MCU clock: the PY32
runs on its internal HSI oscillator, about +-1 %, so 1750 Hz may read 1733 to
1767 Hz on a given radio, the same offset on every reading. Relative readings
(the needle between two tones heard on the same radio, the stability) are not
affected.

## Not in v0.3

No protocol identification or decoding, no spectrum (AF Spectro), no
CTCSS / DCS reading (written, then dropped: the firmware's scanner has it),
no RF waterfall (Spectrum3D), no calibrated deviation or voltage, no
recording to flash, no TX, no USB streaming.

## Model

`test/model.py` transcribes the measurement with the C's integer arithmetic
and reads the C's tables and constants from `afscope_app.c`. It checks: TUNE on tones from 300 Hz to 6 kHz,
a weak tone, no reading on DTMF, AFSK, voice and hiss; the counter on tones
at every timebase; Bell 202 as a pair near 1200 / 2200 Hz; the rising and
falling triggers; clipping; the AF output and filters for FM, AM and USB in
STD and RAW. `model.py --png DIR` writes the screens `test/render.py`
draws from the model's state with the firmware's fonts (`../af_lcd.py`).
