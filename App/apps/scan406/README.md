# Scan 406: find a 406 MHz beacon, then hand it to EPIRB 406

Scan 406 watches every Cospas-Sarsat 406 MHz channel a beacon can transmit
on. When it hears a burst, it raises an alarm. For a first-generation burst in
AUTO, it then tunes the VFO to that channel and opens EPIRB 406, which decodes
the next burst. The alarm can be acknowledged immediately; it also ends after
30 s in that case so EPIRB 406 is ready before the beacon repeats, about every
50 s.

EPIRB 406 on its own listens on one frequency: 406.031 MHz with the wide filter
covers the 406.025 to 406.040 MHz channels. Beacons type approved since 2025
transmit on 406.076 MHz, outside that window. Scan 406 covers both.

Key 1 adds a watch of 121.500 MHz and 121.375 MHz (SAR exercise beacons) for
the homing siren of an ELT or a PLB (off by default, see below).

Needs a Fusion firmware with app API level 3 (`handoff`, `vfo_stage`) and the
`APP_CAP_BEAM2` receive-profile bridge. EPIRB 406 must be installed for the
hand-off.

## Version 1.8: FM broadcast overload and off-frequency beacons

Field report (03/11/2026, ADRASEC 73, Nagoya UT-109A airband antenna on a
car): an exercise beacon on 121.375 MHz was found up to about 500 m only,
while 406 MHz bursts were found 5 km away. FM broadcast (88-108 MHz) overloads
the UV-K1 front end on the airband; the operator heard it on 121.5 MHz AM,
covering the beacon beyond 500 m. A 406 MHz beacon transmits about 20 dB more
than a 121.375 MHz exercise beacon, which alone is a factor 10 in range. The
first remedy is a FM band-stop filter (88-108 MHz) between antenna and radio:
it keeps both 121 and 406 MHz, which an airband band-pass would not.

- **Low fixed gain on one VHF visit in two.** REG_7E bit 15 fixes the AGC,
  bits 14-12 pick an entry of the REG_10..REG_14 gain table; 1.8 uses entry 0
  (REG_10 `0x0318`, lowest LNA and PGA) on every other visit and the AGC on
  the others, each with its own floors. Back to UHF, REG_7E is written back:
  the 406 MHz scan keeps the AGC. Bench: two FM carriers (SSTV test pictures
  `F4WAT TEST`) on 144.040 and 144.070 MHz, whose third-order product falls
  on the siren at 144.100 MHz, no attenuator. With the AGC, a siren found
  without them was lost under them; with entry 0 it was found with the
  carriers 20 dB stronger. The AGC visits still hear a siren 6 kHz off, which
  the low gain loses.
- **The siren's own frequency.** After two positive listens, the RSSI is read
  from 10 kHz below to 10 kHz above the point in 1 kHz steps (AGC, the watch's
  RF filter: on the bench these found +3 and +6 kHz with 8 to 9 dB above the
  ends, where the stock AM filter or the low gain gave 2 to 5 dB). The
  strongest step is kept when it stands 6 dB above the two ends, else the
  nominal frequency. The alarm shows it (`ELT 121.504 0S AGO`) and AUTO hands
  it to Sig Finder, whose AM narrow filter (3.5 kHz) would lose a beacon 6 kHz
  off. Bench: +3 kHz read 144.102, -3 kHz 144.096, +6 kHz 144.104, -6 kHz
  144.095 MHz (1 to 2 kHz).
- **406 MHz sensitivity.** Same build, TEST mode, T.001 bursts set digitally
  around the threshold, 8 bursts a step: 7 to 8 of 8 at -10 and -12 dB with
  the watch off or on. At -14 dB, at the threshold, 15/24 with the watch off
  and 10/24 with it on: not significant on that many bursts, but a small loss
  right at the limit cannot be excluded (the watch takes about 35 ms a
  second). Turn the watch off where 406 MHz alone matters.

4088 B of code (8 B left), 408 B of assets.

## Version 1.7: 121.375 MHz as well

The siren watch also looks at 121.375 MHz, the frequency of SAR exercise
beacons, right after 121.500 MHz: 125 kHz away on the same VHF LNA, 10 ms for
the RSSI, so a look takes about 35 ms a second. Each frequency keeps its own
floor; the alarm says which one (`ELT 121.375 nS AGO`), and AUTO opens Sig
Finder on that frequency, AM narrow. A siren on each frequency gives one alarm
each, 121.500 MHz first. With the watch on, the top line reads
`ELT 121.500 121.375` (TEST: `ELT 433.695`; TEST keeps a single point). Bench
build with the two points moved to 144.225 and 144.100 MHz, AM siren on
144.100 MHz: alarm `ELT 144.100` 2.9 to 3.7 s after the siren starts from
LimeSDR gain 30 down to 15 dB, none at 12 dB, the same limit as the first
point. 3876 B of code, 392 B of assets.

## Version 1.6: the 121.5 MHz siren watch

With key 1 (saved), Scan 406 also looks at 121.500 MHz once a second for the
homing signal of a beacon: an AM carrier with a siren, a tone swept down from
about 1600 to 300 Hz two to four times a second. This catches 121.5 MHz-only
ELTs and PLBs, a beacon whose 406 MHz transmitter has failed, and exercise
beacons. A modern 406 MHz beacon is still heard further on 406 MHz (5 W)
than on 121.5 MHz (about 100 mW).

- Each look: the VHF LNA, the AM demodulator and 121.500 MHz (REG_33,
  REG_31 bit 0, REG_42, REG_2A, the values `RADIO_SetModulation` and
  `BK4819_PickRXFilterPathBasedOnFrequency` use on the BK4829), 20 ms for the
  RSSI to settle, then back to UHF FM. About 25 ms a second, 2.5 % of the
  406 MHz watch.
- A reading 5 dB above the 121.5 MHz floor (kept as a channel's) starts 1.2 s
  of audio capture: band-pass 300 to 1700 Hz, zero crossings per 20 ms, and a
  correlation of that series with a falling sawtooth of 240 to 500 ms. A fit
  of r² 0.40 or more over a sweep of 300 Hz or more, twice in a row (2.4 s),
  raises the alarm `ELT 121.500 nS AGO`. Anything else there (a plain carrier,
  voice) becomes the floor, so it is not listened to again until it drops.
- A 406 MHz burst that starts during a 2.4 s listen can be missed; the beacon
  repeats about 50 s later.
- After the alarm: WATCH goes on scanning; AUTO sets the VFO to 121.500 MHz AM
  narrow and opens Sig Finder with the launch VFO as its message, as EPIRB 406
  key 8 does, so EXIT in Sig Finder brings that VFO back. A siren still on
  after the acknowledgement does not raise the alarm again.
- The RF filter stays the `beam_prepare` one (FM narrow, REG_43 `0x4048`): on
  the bench the stock AM filter (`0x345C`) lost a siren 6 kHz off, and an ELT
  may be that far off.
- TEST mode: the siren is looked for in FM on channel S, 433.695 MHz. The top
  line shows the watched frequencies while the watch is on.

Bench of 03/11/2026 (LimeSDR siren behind a 30 dB attenuator, 2 m away; never
transmitted on 121.5 MHz):

| Test | Result |
|---|---|
| TEST mode, FM siren on 433.695 MHz, LimeSDR gain 34 down to 5 dB | alarm 3.5 to 4.6 s after the siren starts, every time; none again while it goes on |
| Bench build with the point on 144.100 MHz (2 m band, VHF LNA, AM): AM siren, gain 30 down to 15 dB (about -67 to -79 dBm) | alarm 2.9 to 4.2 s after the siren starts |
| Same, gain 12 dB and below | no alarm: the limit is about 3 to 4 dB above the RSSI floor |
| No transmission, 90 s (TEST) and 120 s (144.100 AM) | no false alarm |

The same bench, with a measurement app, found the RSSI settled 16 to 18.5 ms
after a jump from UHF to VHF and 7 ms after a jump back; a siren 3 kHz off
with either filter; and the correlation score at 51 to 92 % for a siren 3 dB or
more above the floor, 7 to 12 % on noise alone. It also showed that a
longer `STEP_MS` (5 or 7 ms instead of 3) does not detect weaker 406 MHz
bursts: the trigger stays about 11 dB above the floor.

## Version 1.5: strong beacons close by

Reported from the field: a beacon close to the radio was detected, but Scan
406 did not switch to it, or handed EPIRB 406 a frequency too far away to
decode. Reproduced on the bench (03/11/2026) with a 70 cm test transmitter
(a UV-K1 sending T.001 frames with the BK4829 FSK modem, LOW, 433.695 MHz,
1.5 m away):

- A strong burst leaks into every channel: up to 11 dB above the floor 48 kHz
  away, so the first channel swept could trigger. Its own channel still reads
  more than 20 dB above the leak. After a trigger, 1.5 reads all 11 channels
  once more (about 35 ms) and keeps the one furthest above its own floor.
  Above the floor, not the highest RSSI, so an interferer on another channel
  does not take the burst.
- That transmitter sent its frame sync 365 to 385 ms after its carrier start,
  against 220 ms for T.001, and its frame ended 665 to 685 ms after it
  (BK4829 modem preamble and set-up). The 300 ms search gave up first, then
  the CW ID on the same carrier made the burst a continuous carrier: no alarm.
  The frame sync search now lasts 400 ms and the capture 750 ms, so such a
  late frame sync is still found. The transmitter has since been fixed
  (frame sync at 224 or 244 ms, CW ID on its own key-up).
- A carrier still there after the alarm (the rest of that CW ID) gives the
  channel's floor its level, as a continuous carrier, instead of being timed
  as a new burst.

## Version 1.3 reliability fixes

- Scan 406 now calls `beam_prepare` at launch. This selects a temporary UHF FM
  receive profile before any direct BK4829 tuning, regardless of the selected
  VFO's band, modulation or bandwidth. The loader restores the original radio
  state when the overlay exits; only a successful AUTO hand-off intentionally
  stages the detected channel in FM wide.
- A first-generation alarm in AUTO ends automatically after 30 s and hands
  off to EPIRB 406. WATCH and GEN2 alarms still require a key, and any key can
  acknowledge every alarm earlier.

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

1. Launch Scan 406 from the Apps menu. Whatever band, modulation or bandwidth
   the selected VFO uses, the overlay temporarily selects UHF FM and scans the
   channels above.
2. Each channel has a bar: its highest level above its own noise floor since
   the last frame, 1 pixel per dB. The dotted line is the trigger (10 dB). The
   channel's kHz above 406.000 MHz is written under its bar.
3. A burst raises the alarm: a beep every second, the flashlight and the green
   LED blinking, the screen inverted. Any key acknowledges it, and that key
   does nothing else. Nothing is received while the alarm is on. For a GEN1
   burst in AUTO, the alarm also ends by itself after 30 s.
4. After the alarm:
   - **AUTO** (the default), first-generation burst: the VFO goes to the
     channel, FM wide, and EPIRB 406 opens and waits for the next burst. EXIT
     in EPIRB 406 goes back to the main screen, with the VFO on that channel.
   - **WATCH**: after a key acknowledges the alarm, the scan goes on. The burst
     stays on the screen with its age.
   - If EPIRB 406 is not installed, AUTO cannot hand off and scanning resumes
     after the alarm ends.
   - Second-generation burst: counted and shown; after a key acknowledges the
     alarm, the scan goes on (EPIRB 406 does not decode them).

| Key | Action |
|---|---|
| * | AUTO / WATCH (saved) |
| 0 | 406 MHz / TEST 433 MHz (saved) |
| 1 | ELT siren watch on 121.500 and 121.375 MHz, on / off (saved) |
| MENU | the help page: these keys (no scanning while it is shown) |
| any | acknowledges an alarm; leaves the help page |
| EXIT | quit |

The status bar shows the mode (`AUTO` or `WATCH`) and `TEST` in TEST mode.
With the siren watch on, the top line reads `ELT 121.500 121.375` (TEST:
`ELT 433.695`).
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

The channels are sometimes only 3 kHz apart, closer than the inherited receive
filter. A trigger starts a localisation pass over the 11 channels, which keeps
the one furthest above its floor; between two channels 3 kHz apart, the
displayed and staged frequency may still be the neighbour of the transmitter.
Both decode (bench, LimeSDR at -53 dBm on 433.695 MHz). The RSSI read 3 ms
after a retune reads about 30 dB under its final value on a UV-K1 (it starts
from about -130 dBm after the RX restart and settles in about 7 ms; first seen
as a saturation near -85 dBm while the main screen showed -53 dBm), which
does not matter for the localisation: the differences between channels are
kept, and the beacon's channel stays more than 20 dB above the leak.

A first-generation burst starts with 160 ms of plain carrier, then 15 bits of
bit sync and 9 bits of frame sync. A burst is detected within about 35 ms of
its start and localised within about 75 ms, so the app has time to switch to
listening. It captures the RX audio
on PA4 at 9.6 kHz (RAW receive, as EPIRB 406) and feeds EPIRB 406's own decoder
(`../epirb406/dec406.c`, included: one decoder for both apps). Then it checks
the BCH codes of C/S T.001:

| Shown | Meaning |
|---|---|
| `GEN1 OK` | frame sync found, BCH-1 (bits 25 to 106) correct and, on a long message, BCH-2 (bits 107 to 144) too: a genuine first-generation frame |
| `GEN1 ERR` | frame sync found, a BCH check fails: a weak signal, a carrier lost in the middle of the frame, or a badly coded beacon |
| `GEN1 CUT` | frame sync found, the frame outlasts the 750 ms capture |
| `TST` after it | the frame sync is the self-test one (011010000 instead of 000101111) |
| `GEN1 ?` | no frame sync, but a first-generation length (0.3 to 0.75 s): not verified |
| `GEN2` | no frame sync, 0.75 to 1.3 s: second generation (1 s), not decodable here |

Without a frame sync, the burst is timed until RSSI falls back 5 dB above the
floor. While it searches for the frame sync, the app reads RSSI every 10 ms
between two samples, as EPIRB 406 reads the keys, so a short blip ends the
search at once. It gives up 400 ms after the localisation.

| Duration, no frame sync | Meaning | Action |
|---|---|---|
| under 0.3 s | a blip | ignored |
| 0.3 to 0.75 s | `GEN1 ?` | alarm, then hand-off in AUTO |
| 0.75 to 1.3 s | `GEN2` | alarm, counted |
| 1.3 s and more | a continuous carrier (interferer, stuck transmitter) | the channel's floor takes its level, no alarm |

Every GEN1 (`OK`, `ERR`, `CUT`, `?`) raises the alarm and, in AUTO, is handed to
EPIRB 406 after a key or the 30 s timeout. A self-test frame is a real beacon
being tested by its owner: it raises the alarm too, marked `TST`.

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

The original 1.2 implementation was tested on a UV-K1 in TEST mode with a
LimeSDR transmitting the T.001 frames on the 433 MHz channel frequencies
(`txall.py --freq ...`). A real beacon has not been heard yet.

The 1.3 changes still need these bench checks:

1. Start from 145 MHz FM, then from a 433 MHz AM VFO and a 433 MHz USB VFO;
   verify that a TEST burst is detected and decoded in every case.
2. Leave AUTO unattended after a GEN1 burst and verify that EPIRB 406 opens at
   about 30 s; verify separately that WATCH and GEN2 continue alarming until a
   key is pressed.
3. Characterise the unresolved adjacent-channel behaviour at 433.695 MHz,
   with strong and weak signals. Record the RSSI on 433.692, 433.695 and
   433.698 MHz before choosing a filter or selection algorithm.

## Size

The code is limited to the 4 KiB overlay; the build script reports the exact
1.8 footprint (4088 B of code, 8 B left, 408 B of assets; 1.5 was 2528 B
and 324 B, the siren watch adds about 1.5 KB: front-end switch, filters, the sawtooth
correlation, all integer, divisions through the resident `uidivmod`; about
the 4 KiB overlay is full). The decoder and BCH checks were about 870 B in 1.2, and the
help page about 80 B, plus the read-only assets. The channels are an 11-byte
table (kHz above 406.000 MHz). The noise floors start at 0 (the loader zeroes
the overlay) and fall to the first readings, so there is no first-pass flag.
The flashlight is driven directly on PC13 (GPIOC ODR, an output from boot):
there is no API service for it. Its state is restored after each alarm.

## Tests

`test/run_tests.sh` builds the host test with AddressSanitizer and
UndefinedBehaviorSanitizer (it found the 1.1 row buffer overflow fixed in
1.2), then runs the whole app on a stub API and a simulated radio
(time, RSSI per tuned frequency, emitters with a level, period, duration and
count, and the PA4 audio of a burst). The stub records the `beam_prepare` call
and verifies that Scan 406 starts scanning only after the simulated receiver
has entered UHF FM. It does not model the BK4829 filter response or claim to
predict adjacent-channel RSSI. Burst audio comes from EPIRB 406's
`gen406.py` (biphase-L, phase modulation, noise, FM discriminator, the PA4
chain): long, short and self-test frames, a bit flipped in PDF-1 and in PDF-2,
inverted polarity, 3 kHz off, CNR 15 dB, a carrier lost at 300 ms and a plain
carrier, each detected 0 to 35 ms after its start. Without audio, the scenarios
cover: a 1G-length burst on every channel, 600 Hz off its centre;
a continuous carrier, at launch or appearing later, next to a beacon; 2G bursts;
WATCH; EPIRB 406 not installed; 50 ms and 200 ms blips; the 10 dB threshold;
AUTO hand-off after 30 s without a key; WATCH still acknowledged after 60 s;
the flashlight restored whatever the blink count; the help page (shown, no
scanning meanwhile, EXIT there only leaves it); the longest last-burst row
after 20 minutes; TEST mode; inherited VHF, AM and USB profiles; and the
documented case where either the exact channel or its 3 kHz lower neighbour
can be kept. Since 1.5: a strong beacon heard 25 dB lower on every channel up
to 60 kHz away (without its audio there), which must be localised on its own
channel in WATCH and AUTO; and the late test transmitter timing (184 or 204 ms of
carrier, 48 more bit sync ones, then 3.5 s of CW ID on the same carrier),
which must decode with a single alarm. With the 1.4 logic, these 18 checks
fail. Since 1.6, the 121.5 MHz watch: the stub models the LNA switches
(REG_33) and the AM demodulator (REG_31 bit 0), a VHF emitter is heard on the
VHF LNA only and its audio with AM only; looped PA4 audio of a siren (2.7 and
2 sweeps a second, a weak one), a plain carrier, voice-like tones and a rising
sweep. Checked: the watch off never tunes 121.5; WATCH alarm within 4 s,
shown, UHF FM back, about one look a second; AUTO to Sig Finder on 121.500 AM
narrow with the launch VFO; no alarm for the carrier, voice, rising sweep or
noise; 406 MHz bursts still caught at any phase of the looks; TEST on 433.695
FM; key 1 saved. Since 1.7: 121.375 MHz visited as often, its alarm, shown,
UHF FM back, AUTO to Sig Finder on 121.375; sirens on both frequencies give
two alarms; voice on 121.375 does not hide a siren on 121.500; both points on
the top line. Since 1.8: the stub models REG_43 and REG_7E, an emitter off
its frequency (2 dB per kHz on the watch's filter, out beyond 7 kHz) and
intermodulation heard with the AGC only: low-gain and AGC visits both seen,
REG_7E and REG_43 back afterwards; +4.5, -3 and +6 kHz sirens shown and
handed over on their own frequency, a flat search keeps the nominal one; a
siren under intermodulation is found by the low-gain visits. These checks
fail with the detector off, with a detector that
accepts anything, or without the switch back to UHF.

## Sources

- C/S T.012, Cospas-Sarsat 406 MHz Frequency Management Plan, Annex H,
  Table H.2 (channel assignment table) and section 1.4
- C/S T.001 (first-generation burst: 160 ms of carrier, bit and frame sync,
  0.44 / 0.52 s; BCH-1 (82,61) and BCH-2 (38,26) generators)
- Bench of 03/11/2026 (UV-K1, 70 cm test transmitter at 1.5 m, LimeSDR) and
  RTL-SDR captures of that transmitter (15/10/2026): the leak, the RSSI
  saturation and the timing above
- `App/apps/epirb406/dec406.c` (the decoder), `test/gen406.py` and
  `test/frame406.py` (burst audio and frames for the host test)
- `App/apps/epirb406/` (RSSI trigger, `tune()`, hand-off to Sig Finder),
  `App/apps/app_api.h` (`handoff`, `vfo_stage`, `beam_prepare`, `beam_get`)
- `App/driver/bk4829.c` (`BK4819_PickRXFilterPathBasedOnFrequency`,
  `BK4819_SetFilterBandwidth`, `BK4819_InitAGC`), `App/radio.c`
  (`RADIO_SetModulation`: the BK4829 AM registers), `App/apps/app_overlay.c`
  (`app_beam_prepare`: the FM narrow profile; `app_rssi_dbm`)
- Bench of 03/11/2026, evening: LimeSDR homing siren (AM and FM) on 433.695 and
  144.100 MHz behind a 30 dB attenuator, a measurement app for the RSSI after
  a band jump and the PA4 audio in AM, and the Scan 406 1.6 runs above
- `App/app/spectrum.c` (`SetFScan()`: retuning without waiting),
  `App/board.c` (PC13, the flashlight, an output from boot)
