# CW Keyer overlay app

`CW Keyer` transmits manually keyed A1A-style carrier Morse on the current TX
VFO. It is entirely contained in the 4 KiB overlay and uses only API level 2;
it has no resident firmware component.

## Proposed 0.6 changes

This is a contribution proposal; the version number is subject to maintainer
approval. It retains the API2 CW Keyer 0.5 implementation and adds Iambic A/B
keying with persistent selection. The overlay is 4092/4096 bytes with Arm GNU
Toolchain 13.3.Rel1; assets are external to that budget. No resident changes.

Current `main` still has 0.2 sources. This proposal also brings the self-contained
0.5 CW implementation from `d0f30a67` into that branch; it does not merge other
firmware development commits. The separate `feature_update_api_v3` branch uses
API3 formatting services and is not the target of this API2 proposal.

- **AUTO**: the 0.5 automatic paddle behavior, without paddle memory.
- **IAMB A**: holding both paddles alternates dots and dashes. Releasing both
  finishes the current element, then stops unless an isolated opposite-paddle
  tap was already remembered.
- **IAMB B**: also remembers a squeeze during the mark, producing one opposite
  element after release. A single paddle alone does not queue this extra tail.

USB exposes both contacts simultaneously. SIDE remains a single-paddle input
because the resident keypad API reports one key at a time.

## Requirements and install

- F4HWN Labs firmware v6.2.0 or later (overlay ABI 1, API level 2).
- Build: `./compile-app.sh cwkeyer` from the repository root, output
  `build/Apps/CWKeyer.app`. See build, tests and update notes below.
- The VFO must be in FM: the over-the-air signal is an unmodulated carrier
  (A1A), so the other station needs a CW or SSB receiver to hear a tone; an FM
  receiver only hears its squelch open and close.

## Controls

- `PTT`: straight-key carrier in every mode.
- Paddle port (`7`): `SIDE` uses SIDE1/SIDE2; `USB` uses the contact adapter
  described below (D+ = tip = dit, D- = ring = dah, sleeve = ground).
- `1`: toggle `STRAIGHT` / `PADDLE`.
  - `STRAIGHT`: SIDE1/SIDE2 are parallel straight keys; on `USB` only the tip
    keys, so a mono plug works.
  - `PADDLE`: the algorithm selected by `8`, displayed as AUTO, IAMB A or IAMB B.
- `0`: enable or mute the CW receive audio path.
- `2`: raise the keyer speed (5 to 60 WPM).
- `3`: raise the receive CW tone by 50 Hz.
- `4`: raise the semi-break-in delay by 50 ms.
- `5`: swap the dit and dah contacts.
- `6`: raise the crystal correction by one BK4829 `REG_3B` step (0.19 ppm).
- `7`: toggle the paddle port `SIDE` / `USB`.
- `8`: cycle AUTO / IAMB A / IAMB B, independent of STRAIGHT/PADDLE.
- `F`, then `2`, `3`, `4` or `6`: lower the corresponding setting instead.
- `UP` / `DOWN`: tune the app frequency by a fixed 100 Hz CW step, up to
  +/-50 kHz. Holding a direction repeats after 400 ms, then every 100 ms.
- `EXIT`: leave the app.

| Setting | Range | Default |
|---|---|---|
| Speed (`WPM`) | 5 to 60 WPM | 18 |
| Receive tone and sidetone (`TONE`) | 300 to 900 Hz, 50 Hz steps | 700 Hz |
| Semi-break-in (`BK`) | 250 to 1500 ms, 50 ms steps | 300 ms |
| Crystal correction (`XTAL`) | -50 to +50 `REG_3B` steps | +14 (+2.69 ppm) |
| Mode / port / order | `STRAIGHT`, `PADDLE` / `SIDE`, `USB` / `DI-DA`, `DA-DI` | `STRAIGHT` / `SIDE` / `DI-DA` |
| Paddle algorithm (`8`) | AUTO, IAMB A, IAMB B | AUTO; last selection restored |

The status bar shows the title, `RX` while the transmitter is off, `TX OFF`
when a transmission is refused, the speaker or `F` icon and the app version.
A transmission is refused (`TX OFF`) when the resident firmware forbids TX on
the frequency (TX lock outside the allowed bands), when the battery is too low
or the voltage too high, or when the VFO is not in FM. Release every keying
control to clear it.

There is no help page in this build: the paddle keyer and the USB port use the
space it took.

On the `USB` port the app sets PA11/PA12 to inputs with pull-up and stops the
USB clock while it runs (a powered USB device holds D+ for about two seconds
after a contact closes); only those bits are changed and they are restored on
exit, so the PC link works again once the app is closed. There is no PC link
while the app is open.

## USB-C paddle adapter

The implementation and F4LPF's UV-K5 V3 tests confirm these signal functions:

| USB-C line | MCU pin | Jack | Function |
|---|---|---|---|
| D+ | PA12 | tip | dit (straight key in `STRAIGHT`) |
| D- | PA11 | ring | dah |
| GND | - | sleeve | common |

Contacts close to ground (active low); the app supplies input pull-ups. Key `5`
swaps the logical DIT/DAH roles after reading both pins. No external voltage
is needed on the contacts. Keep power conductors insulated and use the paddle
adapter only on the radio, not on a charger or computer. A mono plug is fine
in STRAIGHT (ring ignored); in PADDLE it holds DAH closed before reversal.

These are signal-level connections, not a complete USB-C connector pin-number
or cable-color specification. Check the actual plug/breakout by continuity;
no complete CC/shield arrangement is asserted here. The upstream adapter
documentation is in [CW Keyer commit edc5c91a](https://github.com/armel/uv-k1-k5v3-firmware-custom/commit/edc5c91a)
(credited to KI6CR via F4WAT).

Keyer element timing is measured on the SysTick counter, so the keypad scan
done at every poll (about 0.2 ms) does not stretch the elements. Contacts must
read the same on three consecutive polls (about 3.5 ms) before a straight-key
edge or a keyer start is accepted.

The main screen shows all six controls as compact, equal-width 43-pixel
3x5-font capsules in the bottom three lines below the frequency. The left
column is anchored to the left edge, the right column to the right edge, and
every label is left-aligned inside its capsule: the mode (`STRAIGHT`,
`PADDLE`), `WPM`, `TONE`, `BK`, the port and contact order
(`SIDE DI-DA`, `USB DA-DI`, ...) and `XTAL`. The status bar shows the app
version.
The Foxhunt speaker icon shows that receive audio is enabled; the `F` icon
temporarily replaces it while reverse adjustment is armed.

The speed, mode, paddle assignment, calibration, receive pitch, break-in delay,
port and speaker state are saved when the app exits. A 0.2 configuration is
kept on upgrade; its crystal correction (hundredths of a ppm) is converted to
the nearest `REG_3B` step. The crystal correction is
written to BK4829 `REG_3B` (reference crystal word) on entry, on top of the
value found there, and that value is restored on exit. The default is 14 steps
(+2.69 ppm), for a carrier measured +400 Hz high at 144.225 MHz and +1.20 kHz
high at 433.225 MHz; the step sign is still to be confirmed on the bench. The
PTT always remains a straight key, so it is usable in the paddle mode.
The frequency offset selected with `UP` / `DOWN` is local to the overlay. It is
applied equally to CW RX, TX and the displayed frequency, and resets to zero
when the app exits; the resident VFO is never modified.
Interface keys are accepted only after a stable 20 ms reading, so
keypad-matrix transients cannot change a setting. PTT is immediate; the paddle
contacts (SIDE or USB) only go through the 3.5 ms contact debounce.
Full-screen refreshes are kept out of the keying path; TX/RX transitions update
only the status line. Battery sampling is deferred until the transmitter has
fully returned to RX so it cannot stretch a mark or an inter-element space.
A continuous mark is limited to ten seconds as stuck-key protection; the key
must be released before the app will accept another transmission.

## RF keying

On the first mark, the app calls the existing `tx_set_params` API service. That
service selects the TX VFO, starts the BK4829 transmitter and enables the board
PA before returning. The app immediately removes PA drive, mutes every
audio/sub-audio source and caches BK4829 register `0x36`, whose upper byte
drives the `VRAMP` PA-bias output. It then applies the proportional crystal
correction to synthesizer registers `0x38`/`0x39`, in their native 10 Hz units,
and restarts register `0x30` through zero so the VCO recalibrates and latches
the corrected frequency.
Register `0x50` remains in TX modulation-mute for the entire session. During a
mark, register `0x30` is `0xC3FA`, matching the driver's TX-link configuration
with the microphone ADC disabled. During a space it becomes `0xC3F2`, which
also gates the BK4829 internal PA gain while leaving the PLL locked. The
unmodulated carrier is therefore keyed with three coordinated controls: the
internal PA gain, `VRAMP`, and the board PA enable.

Every mark uses a four-step, 4 ms rise and fall of the saved PA bias. Internal
PA gain and board PA enable are asserted before the rise, then removed after
the fall. Between marks the synthesizer remains locked, but internal PA gain,
PA bias and the board PA are all off.
`EXIT`, retuning, and every abort path call `tx_end` to disable the PA and restore
normal RX. After the final mark, the configurable semi-break-in hang (300 ms
by default) keeps the synthesizer ready across Morse element gaps, then
automatically calls `tx_end` and restores reception without requiring `MENU`.
The overlay then selects the BK4829 USB baseband output and tunes below the
calibrated RX frequency by the configured receive pitch (700 Hz by default),
so an incoming A1A carrier is heard at that pitch. The same pitch is used for
the local TX sidetone. When enabled, the speaker remains open throughout RX:
gating it with the FM squelch clips the start of dits and chops the tone at
every carrier transition.

The atomic `tx_set_params` service necessarily enables RF briefly before the
overlay can remove PA drive. The app then waits for the corrected synthesizer
setting to lock and applies the normal ramp to the first deliberate mark. A
spectrum/SDR check is required before assuming the four-step envelope is
spectrally clean on every power band.

Squeeze keying needs USB: the single-key `get_key` API shows one SIDE key at
a time. AUTO retains automatic repeat without memory; A/B add the memory
described below.
The TX sidetone follows the approach used by NR7Y's CW implementation: Tone1
is routed to the BK4829 `AF_ALAM` output while register `0x50` keeps TX
modulation muted. Morse edges change only the Tone1 frequency control word in
register `0x71`; writing zero silences the speaker between marks without the
pop caused by repeatedly changing either the tone-enable or AF-selector
register. At the end of the semi-break-in delay, the AF DAC gain fades to its
minimum before the selector moves to USB and the speaker amplifier is disabled.
After the RX profile is restored, the USB output settles at minimum DAC gain
before the amplifier is enabled. The gain then rises one DAC code at a time
instead of jumping directly to full level. The over-the-air signal therefore
remains A1A rather than MCW. Key `0` controls both receive audio and the TX
sidetone.

## Status and known limits

- Upstream 0.5 documentation at `a59526f6` reports UV-K1 tests including
  USB-C contacts, PC link restoration and SDR timing/decoding at 20 to 60 WPM.
  These predate the present A/B addition and are not new tests of this candidate.
- F4LPF tested the development build on a UV-K5 V3: USB-C simultaneous input,
  squeeze alternation, distinct A/B release behavior, selection, normal CW in
  the tried configurations, reopening and power-cycle persistence. This is
  not an exhaustive hardware validation; the renamed candidate needs its
  own installation/update check. The automated tests below are host tests.
- At 60 WPM each fixed 4 ms ramp is 20% of a dot. Real RF timing, first-mark
  behavior and electrical bounce still need checks on each intended setup.
- The K-plug line PA10 is not used as a paddle input: it follows the PTT
  button on the bench.

## Credits

- CW Keyer: Armel F4HWN.
- USB-C paddle port, paddle keyer and contact debounce: Johan Denoyer F4WAT,
  USB-C wiring after KI6CR's CW adapter for the UV-K5 V3.
- TX sidetone approach: NR7Y's CW implementation.

This contribution was initiated and hardware-tested by F4LPF, with development
and documentation assistance from OpenAI Codex and ChatGPT. F4LPF defined the
requirements, built the cable, performed physical tests and selected the
behavior. Codex assisted with repository analysis, implementation, builds and
automated tests; ChatGPT assisted with architecture discussions, development
steps, interpretation and documentation. Original credits and license remain.

## Element memory and timing

With both contacts initially held, DIT has priority. Sustained squeeze then
alternates. A/B remember an isolated opposite contact during a mark or space.
B additionally remembers both contacts during the mark, including the 4 ms
rise and fall ramps. A squeeze appearing only during the space does not queue
a B tail; if still held at the decision it causes the normal alternation.
One pending opposite element is consumed once and cleared on the next mark.

Active memory requires two identical consecutive samples. A staggered release
leaving the opposite contact alone for that long is treated as a tap, including
in A. USB is observed every wait iteration and after each 1 ms ramp step.
SIDE is observed during the waits, with the existing 4 ms blind interval in
each ramp. Very short contacts can be missed. Opaque resident calls during
RX-to-TX preparation cannot be polled by the app. No global timers are changed.
EXIT aborts a wait and ramps RF down; the pending memory is not played after it.

## Saved settings and updates

The last algorithm is restored after normal exit and after a power cycle.
Absent/invalid configuration selects AUTO. Select the mode, exit normally,
and wait for the app menu before powering off. No settings change means no
save callback; a changed setting stages data on exit, and the resident skips
programming identical bytes. There are no continuous writes during CW.

Updating official CW Keyer 0.5 in its existing slot keeps its `CW Keyer` name
and the first 12 configuration bytes unchanged. Bytes 12/13, previously unused
within the 16-byte local area, add algorithm 0/1/2 and extension tag 0xD3. Both
must validate before the new algorithm is applied. Old 0.5 records therefore
start at AUTO without losing other settings. The original 0.2 migration is
retained. Downgrading to 0.5 ignores the extension and can still save its
12-byte prefix; a later upgrade may find the old algorithm extension again.

The loader stores config per slot and checks a 16-byte name owner tag. It
preserves both during the official slot update procedure. Keep the same slot
for an update; moving slots does not migrate settings. An experimental slot
owned by `CW Keyer DEV` does not match the final name and starts at defaults.
No automatic migration from that development identity is performed. A
different slot, even with the same application name, is never targeted by
this ordinary configuration path.

The existing external NOR save is not atomic. Power loss before exit loses
the unsaved change; loss during erase/program may damage this slot's header,
config or assets and require reinstallation. API2 exposes no atomic commit
guarantee. Range/tag checks do not detect every possible valid-looking corruption.

## Build and automated checks

The repository Dockerfile pins Arm GNU Toolchain 13.3.Rel1 and Python 3.10.
From the repository root, build only this application:

```sh
./compile-app.sh cwkeyer
```

For an installed matching native toolchain and Python 3:

```sh
(cd App/apps/cwkeyer && bash build.sh)
```

The native build creates `App/apps/cwkeyer/CWKeyer.app`. The Docker wrapper
copies it to `build/Apps/CWKeyer.app`. Both use the unchanged linker assertion
at 4096 bytes. This proposal measured 4092 bytes including 76 bytes of globals,
leaving 4 bytes; the 107-byte assets and 64-byte header make a 4263-byte APP.
Other compiler versions must be checked independently against the limit.

Host tests require Python 3 and a GCC-compatible native C compiler, plus the
ARM `nm` utility for artifact verification. After a native build, run from root:

```sh
python3 App/apps/cwkeyer/tests/test_keyer.py --output-dir /tmp/cwkeyer-checks
python3 App/apps/cwkeyer/tests/test_persistence.py --output-dir /tmp/cwkeyer-checks
```

Use `HOST_CC` and `ARM_NM` to select compiler paths when they are not on PATH.
Use the same output directory for both commands, outside tracked sources. For
a Docker-only build, run these checks inside the build image after building
the app there so the generated assets, ELF and APP are available.

The first suite runs 153 timeline cases at 5/18/60 WPM in AUTO/A/B and compares
AUTO with a small attributed 0.5 reference fixture. The second executes actual
app config code and resident config/NOR functions with simulated Flash. It
checks migration, invalid values, unchanged writes, other-slot isolation and
five interrupted-write points. See [tests/README.md](tests/README.md) for
expected sequences and coverage limits. Tests add nothing to the overlay.
