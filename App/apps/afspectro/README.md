# AF Spectro: the spectrum of the received audio

AF Spectro looks at the demodulated RX audio, the BK4829 AF output that APRS
RX, AIS RX, SSTV or the radiosonde apps decode: a spectrum with peak hold and
a sonogram, the two main peaks and their level difference, the Bell 202
twist. It is the spectral half of an instrument whose other half is
[AF Scope](../afscope/README.md) (oscilloscope, tuner): `MENU` on
the last view hands off to it, and back. It is not an RF analyser
(Spectrum3D is).

| Step | State |
|---|---|
| Analysis and screens modelled (`test/model.py`, `test/render.py`) | **Done**: peaks and MIN / MAX at every rate, Bell 202 twist (also between loud hiss), DTMF twist, voice / noise / sweep cases, screens drawn from the model |
| v0.1 (SPECTRO, SONO, twist) | Built and run on the radio |
| v0.2 (AM path, LCD sent with the samples while the speaker is on, lock / clip / speaker icons) | Built and run on the radio: no flutter, LCD intact after leaving |
| v0.2 then (lcd_feed, sonogram rows gated by the squelch, fixed dot grid, scrolling sonograms, RAW ignoring the squelch bit) | Model only; squelch gate + dots seen on the radio in RAW, not yet in STD; scroll not built |

## Views

| View | Screen |
|---|---|
| SPECTRO | Header; the smoothed spectrum from 0 to fs/2 with peak hold (24 px), the axis in kHz, a 16-row sonogram strip (1.3 s) |
| SONO | Header; the sonogram over the screen: 40 rows of 80 ms (3.2 s); the axis |

Both sonograms scroll down, the newest row at the top. They were first
wiped in place (a cursor and a blank "now" line) because a scroll tore while
the LCD went out one byte per sample, 109 ms a screen at 9.6 kHz; with
lcd_feed's 5, 3 or 1 bytes per sample a screen goes out in 22 to 27 ms. A row
stays blank while the squelch is closed: on the radio, the hiss between APRS
packets filled every row as much as a packet did (strongest from 300 to
900 Hz, after the de-emphasis), and the packet could not be told from it.
Gated, a packet is a block of rows, its 1050..2350 Hz plateau and its length
readable. With the squelch at 0, or in RAW, every row is drawn: in RAW the
BK4829's squelch bit stays closed even on a strong APRS packet (the trace on
PA4 shows it, the speaker stays silent), so RAW ignores it, for the speaker
too, as the radiosonde apps do.

Header, in the tiny font and fixed columns, frequencies on 4 digits with
leading zeros: two tones and their level difference on the right edge
(`0697 / 1209Hz         TW  -3.0dB`, high minus low; for Bell 202, `1200 /
2200Hz` and its twist), or the strongest peak and the lowest and highest
steady-tone frequencies since the last clear (`PK 1180Hz   MIN 0410    MAX
1590`: a sweep's range). Above 9999 Hz (19.2 and 38.4 kHz) a field takes one
more character.

Status bar: `AF SPECTRO`, `RAW` after it when the filters are off, then left
of the battery a lock while held (else a clipped wave while samples reach the
ADC limits) and the speaker icon while it is on. The demodulation the VFO set
(FM, AM, USB) is not shown: it does not change in the app.

## Keys (UV-K5 and UV-K1)

| Key | Action |
|---|---|
| MENU | SPECTRO, SONO, then AF Scope |
| UP / DOWN | Range of the bars, 80 / 60 / 40 dB (up: closer), which also sets the sonogram's contrast. UV-K1: LEFT / RIGHT, as `SET_NAV` |
| * | Hold: the screen freezes; the views still change on the held data |
| 1 | Audio path STD / RAW |
| 2 | Speaker on / off (in STD it follows the squelch; in RAW it plays all, the squelch bit staying closed) |
| 3 | Sample rate 9.6 / 19.2 / 38.4 kHz (0-4.8, 9.6 or 19.2 kHz shown) |
| 0 | Clear: peak hold, sonogram, MIN / MAX, twist average |
| EXIT | Quit; view, path, speaker, rate and range are saved |

A new path or rate also starts the analysis over.

## What it is for

- **Set the twist of APRS TX or of the KISS TNC**: with AF Spectro on a second
  radio, an APRS packet shows `1200 / 2200Hz  TW -5.1dB`, the level of 2200 Hz
  against 1200 Hz averaged over about 1.7 s of Bell 202; adjust APRS TX's key
  2 until it reads near 0 dB. In STD it is what a normal receiver sees
  (de-emphasis included); in RAW, what the transmitter sends.
- **See what a signal is made of**: an AFSK plateau, a DTMF pair and its
  twist, a homing beacon's sweep (the sonogram's sawtooth, MIN / MAX its
  range), voice, data bursts.

It identifies no protocol: the Bell 202 pair is recognised only to measure
its twist.

## Audio path

`STD` keeps the VFO's AF output and filters: USB for USB (CW and SSB beat
notes), else FM, which AM also uses (the AM demodulator, REG_31 bit 0, is on
for an AM VFO). `RAW` turns the RX audio filters off (REG_2B: HPF 300 Hz, LPF
3 kHz, de-emphasis) and the AFC (REG_73) and, in FM only, selects the
discriminator bypass (REG_47 AF 9, as the radiosonde apps): in AM, RAW keeps
the AM audio and only drops the filters. The registers are restored on exit,
and the loader re-runs `RADIO_SetupRegisters`. The hand-off to AF Scope
carries the path and the speaker.

## How it measures

Blocks of 256 samples are taken at 9.6, 19.2 or 38.4 kHz with nothing else
running, except, while the speaker is on, the screen: its bytes then go to the
LCD with the samples, 5 after each one at 9.6 kHz, 3 at 19.2 kHz, 1 at
38.4 kHz (the resident lcd_feed, API level 3), the LCD traffic spread evenly
instead of an 11 ms burst between frames, which the speaker played as a
flutter (the "helicopter"). The 1048 bytes of a screen then go out within
every frame, as with the speaker off (one byte per sample took two frames at
9.6 kHz). K5Viewer gets a frame every 16 screens (viewer_update), every
screen with the speaker off. Held, the blocks are still taken (not
analysed): they pace the screen. See AF Scope's README for what driving SPI1
from the app cost in v0.2's first builds.

Each block: a Hann window, a 256-point FFT in block floating point (int16, a
shift before any stage that could overflow), levels in tenths of dB. A block
whose second peak (6 bins away at least) lies 15 dB under the first is a
steady tone: its frequency, interpolated on the dB levels, counts for MIN /
MAX. A frame is about 80 ms (3, 6 or 12 blocks). The smoothed spectrum (1/4
of each block) gives the bars, the peaks and the sonogram row, all scaled on
its mean level: a pixel a quarter of the range over it, a sixth on a fixed
grid of one pixel in four (even rows and bins; a checkerboard alternating
per row drew diagonals).

Two tones are two peaks within 30 dB, 12 dB over the mean and 10 dB over the
level between them (a sweep's broad hump is not a pair). Bell 202 AFSK spreads
into a plateau from about 1050 to 2350 Hz with spikes past both tones, not two
lines: it is recognised by the levels at 1200 and 2200 Hz (within 15 dB of
the peak, 6 dB over 800 and 2700 Hz, which voice is not), and its twist is
the energy ratio of the +-2-bin bands around 2200 and 1200 Hz, averaged over
about 1.7 s, only while Bell 202 is heard: the unsquelched hiss between
packets would otherwise pull it towards its own slope (about 1 dB). In the
model it reads the twist within about 1 dB at every rate, packets in loud
hiss included. The hand-off to AF Scope and back keeps the twist average.

## Not in v0.2

No protocol identification or decoding, no oscilloscope or tuner (AF Scope),
no sub-audio reading, no RF waterfall (Spectrum3D), no calibrated levels, no
SINAD (written, then dropped), no recording to flash, no TX, no USB
streaming.

## Model

`test/model.py` transcribes the analysis with the C's integer arithmetic and
reads the C's tables and constants from `afspectro_app.c`. It checks: peaks
and MIN / MAX on tones at every rate (worst 0.03 bin), noise, a 1200 Hz tone
alone, 1200 + 2200 Hz tones, Bell 202 twist 0 / -5.1 / +3 dB at every rate,
continuous and in packets between loud hiss, a DTMF pair and its twist, voice
(never Bell 202), a falling sweep (MIN / MAX, never a pair), clipping.
`model.py --png DIR` writes the screens `test/render.py` draws from the model's state with the
firmware's fonts (`../af_lcd.py`).
