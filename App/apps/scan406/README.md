# Scan 406: find a 406 MHz beacon, then hand it to EPIRB 406

Scan 406 watches every Cospas-Sarsat 406 MHz channel a beacon can transmit
on. When it hears a burst, it raises an alarm, and once the alarm is
acknowledged it tunes the VFO to that channel and opens EPIRB 406, which
decodes the next burst. A first-generation beacon repeats about every 50 s.

EPIRB 406 on its own listens on one frequency: 406.031 MHz with the wide filter
covers the 406.025 to 406.040 MHz channels. Beacons type approved since 2025
transmit on 406.076 MHz, outside that window. Scan 406 covers both.

Needs a Labs firmware v6.3.0 or later (app API level 3: `handoff`,
`vfo_stage`), with EPIRB 406 installed for the hand-off.

## Channels

From C/S T.012 (406 MHz Frequency Management Plan), Annex H, Table H.2, the
channel centres scanned in this order:

| Channel | MHz | Status |
|---|---|---|
| B | 406.025 | first channel, closed to new type approvals in 2002 |
| C | 406.028 | closed in 2007 |
| D | 406.031 | closed on 1 July 2025 |
| F | 406.037 | closed in 2012 |
| G | 406.040 | closed in 2017 |
| J, K | 406.049, 406.052 | available for future assignments |
| N, O | 406.061, 406.064 | available for future assignments |
| R | 406.073 | available for future assignments |
| S | 406.076 | open since 1 January 2025 |

Closed channels still carry beacons: a closed channel only stops new type
approvals. Not scanned: A (406.022 MHz, system beacons: orbitography and
reference) and the channels reserved "not to be assigned" (E, H, I, L, M, P, Q).

## Using it

1. Launch Scan 406 from the Apps menu. Whatever VFO is set, it scans the
   channels above.
2. Each channel has a bar: its highest level above its own noise floor since
   the last frame, 1 pixel per dB. The dotted line is the trigger (10 dB). The
   channel's kHz above 406.000 MHz is written under its bar.
3. A burst raises the alarm: a beep every second, the flashlight and the green
   LED blinking, the screen inverted. Any key acknowledges it, and that key
   does nothing else. Nothing is received while the alarm is on.
4. After the alarm:
   - **AUTO** (the default), first-generation burst: the VFO goes to the
     channel, FM wide, and EPIRB 406 opens and waits for the next burst. EXIT
     in EPIRB 406 goes back to the main screen, with the VFO on that channel.
   - **WATCH**, or EPIRB 406 not installed: the scan goes on. The burst stays
     on the screen with its age.
   - Second-generation burst: counted and shown, the scan goes on (EPIRB 406
     does not decode them).

| Key | Action |
|---|---|
| * | AUTO / WATCH (saved) |
| 0 | 406 MHz / TEST 433 MHz (saved) |
| MENU | the help page: these keys (no scanning while it is shown) |
| any | acknowledges an alarm; leaves the help page |
| EXIT | quit |

The status bar shows the mode (`AUTO` or `WATCH`) and `TEST` in TEST mode.
The two bottom rows:

- the last burst: its kind (see below), its channel and how long ago it ended,
  for example `GEN1 OK 406.076 12S AGO`. The age stops at 999 s, so the row
  always fits the screen. `NO BEACON YET` until the first burst.
- `BURSTS GEN1 n GEN2 n`: the bursts counted since launch, first and second
  generation.

The labels say GEN1 / GEN2 rather than 1G / 2G: in the screen's tiny font, a G
looks like a 6, and "1G 0 2G 0" read "16 0 26 0".

## How a burst is classified and verified

Each channel takes about 3 ms (frequency written to REG_38/39, RX restarted,
RSSI read), so the 11 channels take about 35 ms. Each channel keeps its own
noise floor. It follows a fall at once and a rise slowly (1/16 per pass, about
0.6 s). A reading 10 dB above it parks the receiver on that channel.

A first-generation burst starts with 160 ms of plain carrier, then 15 bits of
bit sync and 9 bits of frame sync. A burst is detected within about 35 ms of
its start, so the app has time to switch to listening. It captures the RX audio
on PA4 at 9.6 kHz (RAW receive, as EPIRB 406) and feeds EPIRB 406's own decoder
(`../epirb406/dec406.c`, included: one decoder for both apps). Then it checks
the BCH codes of C/S T.001:

| Shown | Meaning |
|---|---|
| `GEN1 OK` | frame sync found, BCH-1 (bits 25 to 106) correct and, on a long message, BCH-2 (bits 107 to 144) too: a genuine first-generation frame |
| `GEN1 ERR` | frame sync found, a BCH check fails: a weak signal, a carrier lost in the middle of the frame, or a badly coded beacon |
| `GEN1 CUT` | frame sync found, the frame outlasts the 600 ms capture |
| `TST` after it | the frame sync is the self-test one (011010000 instead of 000101111) |
| `GEN1 ?` | no frame sync, but a first-generation length (0.3 to 0.75 s): not verified |
| `GEN2` | no frame sync, 0.75 to 1.3 s: second generation (1 s), not decodable here |

Without a frame sync, the burst is timed until RSSI falls back 5 dB above the
floor. While it searches for the frame sync, the app reads RSSI every 10 ms
between two samples, as EPIRB 406 reads the keys, so a short blip ends the
search at once. It gives up 300 ms after the trigger.

| Duration, no frame sync | Meaning | Action |
|---|---|---|
| under 0.3 s | a blip | ignored |
| 0.3 to 0.75 s | `GEN1 ?` | alarm, then hand-off in AUTO |
| 0.75 to 1.3 s | `GEN2` | alarm, counted |
| 1.3 s and more | a continuous carrier (radiosonde, interferer) | the channel's floor takes its level, no alarm |

Every GEN1 (`OK`, `ERR`, `CUT`, `?`) raises the alarm and, in AUTO, is handed to
EPIRB 406. A self-test frame is a real beacon being tested by its owner: it
raises the alarm too, marked `TST`.

The channels are 3 kHz apart, closer than the receive filter is wide, so a
strong beacon can also be heard on the neighbouring channels. The first channel
of the scan order that hears it wins, so the VFO can end up 3 kHz below the
beacon. EPIRB 406's host tests decode with a ±5 kHz offset, and keys 4 / 6 there
tune by 5 kHz. To be confirmed on the bench (receive window measurement).

## TEST mode, on 433 MHz

Key 0 switches the scan to the same channels on 433 MHz, so it can be tested
with a generator without ever transmitting on 406 MHz. 406.0xx MHz becomes
433.619 + xx kHz:

| 406 MHz | B .025 | C .028 | D .031 | F .037 | G .040 | J .049 | K .052 | N .061 | O .064 | R .073 | S .076 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| TEST, MHz | 433.644 | 433.647 | 433.650 | 433.656 | 433.659 | 433.668 | 433.671 | 433.680 | 433.683 | 433.692 | 433.695 |

`TEST` shows in the status bar, and the last burst is shown on its 433 MHz
frequency. In AUTO, the hand-off tunes EPIRB 406 to that 433 MHz channel. With
the LimeSDR: `App/apps/epirb406/test/limesdr/txall.py --freq 433.695e6` for
channel S, for instance.

## On the radio

UV-K1, F4HWN Labs, in TEST mode, with a LimeSDR transmitting the T.001 test
frames on the 433 MHz channel frequencies (`txall.py --freq ...`): every test
passes. A real beacon has not been heard yet.

## Size

2380 B of code in the 4 KiB overlay (the decoder and the checks: about 870 B,
the help page: about 80 B), plus the read-only assets (the screen texts and
the help page). The channels are a table of
11 bytes (kHz above 406.000 MHz). The noise floors start at 0 (the loader
zeroes the overlay) and fall to the first readings, so there is no first-pass
flag. The flashlight is driven directly on PC13 (GPIOC ODR, an output from
boot): there is no API service for it. Its state is restored after each alarm.

## Tests

`test/run_tests.sh` builds the host test with AddressSanitizer and
UndefinedBehaviorSanitizer (it found the 1.1 row buffer overflow fixed in
1.2), then runs the whole app on a stub API and a simulated radio
(time, RSSI per tuned frequency, emitters with a level, period, duration and
count, and the PA4 audio of a burst). Burst audio comes from EPIRB 406's
`gen406.py` (biphase-L, phase modulation, noise, FM discriminator, the PA4
chain): long, short and self-test frames, a bit flipped in PDF-1 and in PDF-2,
inverted polarity, 3 kHz off, CNR 15 dB, a carrier lost at 300 ms and a plain
carrier, each detected 0 to 35 ms after its start. Without audio, the scenarios
cover: a 1G-length burst on every channel, 600 Hz off its centre;
a continuous carrier, at launch or appearing later, next to a beacon; 2G bursts;
WATCH; EPIRB 406 not installed; 50 ms and 200 ms blips; the 10 dB threshold;
an alarm acknowledged after 60 s; the flashlight restored whatever the blink
count; the help page (shown, no scanning meanwhile, EXIT there only leaves it);
the longest last-burst row after 20 minutes; TEST mode; and a wide receive
window, where the neighbouring channel answers first.

## Sources

- C/S T.012, Cospas-Sarsat 406 MHz Frequency Management Plan, Annex H,
  Table H.2 (channel assignment table) and section 1.4
- C/S T.001 (first-generation burst: 160 ms of carrier, bit and frame sync,
  0.44 / 0.52 s; BCH-1 (82,61) and BCH-2 (38,26) generators)
- `App/apps/epirb406/dec406.c` (the decoder), `test/gen406.py` and
  `test/frame406.py` (burst audio and frames for the host test)
- `App/apps/epirb406/` (RSSI trigger, `tune()`, hand-off to Sig Finder),
  `App/apps/app_api.h` (`handoff`, `vfo_stage`, `beam_get`)
- `App/app/spectrum.c` (`SetFScan()`: retuning without waiting),
  `App/board.c` (PC13, the flashlight, an output from boot)
