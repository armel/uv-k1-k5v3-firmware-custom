# CW Keyer overlay app

`CW Keyer` transmits manually keyed A1A-style carrier Morse on the current TX
VFO. It is entirely contained in the 4 KiB overlay and uses API level 3;
it has no resident firmware component. This variant uses only PTT and
SIDE1/SIDE2. External keys and paddles connected through USB-C are handled by
the separate `CW USB` app. Both apps compile the same shared source.

## Requirements and install

- F4HWN Fusion firmware with overlay ABI 1 and API level 3.
- Build: `./compile-app.sh cwkeyer` from the repository root, output
  `build/Apps/CWKeyer.app`; install it in a free slot with UV Studio.
- The VFO must be in FM: the over-the-air signal is an unmodulated carrier
  (A1A), so the other station needs a CW or SSB receiver to hear a tone; an FM
  receiver only hears its squelch open and close.

## Upgrading from CW Keyer 1.0

CW Keyer 1.1 no longer reads a key or paddle through USB-C; it uses only PTT
and SIDE1/SIDE2. When it replaces CW Keyer 1.0 in the same slot, the app name
is unchanged, so the existing CW Keyer settings are retained. If you used the
USB-C input, keep CW Keyer in its current slot and install the separate
`CW USB` app in a free slot. CW USB starts with defaults on first installation,
so copy the required settings manually, especially `XTAL`.

## Controls

- `PTT`: straight-key carrier in every mode.
- `SIDE1` / `SIDE2`: straight keys or dit/dah contacts according to the mode.
- `1`: toggle `STRAIGHT` / `PADDLE`.
  - `STRAIGHT`: SIDE1/SIDE2 are parallel straight keys.
  - `PADDLE`: automatic dits and dahs, no paddle memory. The keypad API shows
    only one SIDE key at a time, so this is a single-paddle keyer.
- `0`: enable or mute the CW receive audio path.
- `2`: raise the keyer speed (5 to 60 WPM).
- `3`: raise the receive CW tone by 50 Hz.
- `4`: raise the semi-break-in delay by 50 ms.
- `5`: swap the dit and dah contacts.
- `6`: raise the crystal correction by one BK4829 `REG_3B` step (0.19 ppm).
- `F`, then `2`, `3`, `4` or `6`: lower the corresponding setting instead.
- `UP` / `DOWN`: tune the app frequency by a fixed 100 Hz CW step, up to
  +/-50 kHz. Holding a direction repeats after 400 ms, then every 100 ms.
- `MENU`: show the help page; `MENU` or `EXIT` returns to the keyer.
- `EXIT`: leave the app.

| Setting | Range | Default |
|---|---|---|
| Speed (`WPM`) | 5 to 60 WPM | 18 |
| Receive tone and sidetone (`TONE`) | 300 to 900 Hz, 50 Hz steps | 700 Hz |
| Semi-break-in (`BK`) | 250 to 1500 ms, 50 ms steps | 300 ms |
| Crystal correction (`XTAL`) | -50 to +50 `REG_3B` steps | +14 (+2.69 ppm) |
| Mode / order | `STRAIGHT`, `PADDLE` / `DI-DA`, `DA-DI` | `STRAIGHT` / `DI-DA` |

The status bar shows the title, `RX` while the transmitter is off, `TX OFF`
when a transmission is refused, the speaker or `F` icon and the app version.
A transmission is refused (`TX OFF`) when the resident firmware forbids TX on
the frequency (TX lock outside the allowed bands), when the battery is too low
or the voltage too high, or when the VFO is not in FM. Release every keying
control to clear it.

The help page (`MENU`) lists the keys in two columns of capsules, as on
CW Decode, with the 100 Hz tuning step at the bottom right. Opening it ends a
semi-break-in hang and returns to RX; PTT and the paddles do not key the
transmitter and the S-meter pauses until `MENU` or `EXIT` closes it.

Keyer element timing is measured on the SysTick counter, so the keypad scan
done at every poll (about 0.2 ms) does not stretch the elements. A contact
change must read the same on five consecutive polls (about 5 ms) before a
straight-key edge or a keyer start is accepted.

The main screen shows all six controls as compact, equal-width 43-pixel
3x5-font capsules in the bottom three lines below the frequency. The left
column is anchored to the left edge, the right column to the right edge, and
every label is left-aligned inside its capsule: the mode (`STRAIGHT`,
`PADDLE`), `WPM`, `TONE`, `BK`, the contact order (`DI-DA`, `DA-DI`) and
`XTAL`. The status bar shows the app
version.
Between the frequency and the settings, the receive S-meter repeats the RSSI
line of the main radio screen: the level in dBm, `S0` to `S9` then `+10` to
`+40`, and 13 bars, the four above S9 hollow. It uses the same scale (S9 at
-93 dBm, 6 dB per S unit, capped at -53 dBm). The reading rises immediately
and falls by 3 dB every 100 ms (one S unit every 200 ms) so that short CW
elements remain readable. It pauses while the transmitter is prepared or on.
The Foxhunt speaker icon shows that receive audio is enabled; the `F` icon
temporarily replaces it while reverse adjustment is armed.

The speed, mode, paddle assignment, calibration, receive pitch, break-in delay
and speaker state are saved when the app exits. A configuration saved
by 0.4 or later is kept on upgrade; one from 0.2 (crystal correction in
hundredths of a ppm) is no longer converted and starts from the defaults.
The crystal correction is
written to BK4829 `REG_3B` (reference crystal word) on entry, on top of the
value found there, and that value is restored on exit. The default is 14 steps
(+2.69 ppm), for a carrier measured +400 Hz high at 144.225 MHz and +1.20 kHz
high at 433.225 MHz; a positive step lowers the carrier (checked on an SDR,
0 vs +14 steps at 433.650 MHz). The
PTT always remains a straight key, so it is usable in the paddle mode.
The frequency offset selected with `UP` / `DOWN` is local to the overlay. It is
applied equally to CW RX, TX and the displayed frequency, and resets to zero
when the app exits; the resident VFO is never modified.
Interface keys are accepted only after a stable 20 ms reading, so
keypad-matrix transients cannot change a setting. This includes `EXIT` while
the paddle keyer runs: a bouncing SIDE contact can make the keypad scan report
another key of its row (`EXIT` for SIDE1) for a single reading. PTT is
immediate; the SIDE paddle contacts only go through the contact debounce of
about 5 ms.
Full-screen refreshes are kept out of the keying path; TX/RX transitions update
only the status line. Battery sampling is deferred until the transmitter has
fully returned to RX so it cannot stretch a mark or an inter-element space.
A continuous mark is limited to ten seconds as stuck-key protection; the key
must be released before the app will accept another transmission.

## RF keying

On the first mark, the app calls the `tx_prepare` API service when the resident
API table provides it. That service selects the TX VFO and configures the
BK4829 transmitter without the FM settling waits, while leaving the board PA
disabled. On an older level 3 table, the app falls back to `tx_set_params` and
immediately removes PA drive after the call. It then mutes every audio/sub-audio
source and caches BK4829 register `0x36`, whose upper byte drives the `VRAMP`
PA-bias output. The proportional crystal correction is applied to synthesizer
registers `0x38`/`0x39`, in their native 10 Hz units, and register `0x30` is
restarted through zero so the VCO recalibrates and latches the corrected
frequency.
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
The overlay then selects BK4829 AF output 5 (`BK4819_AF_BASEBAND2` in the
driver, the upper-sideband demodulator, `AF_USB` in this app; unrelated to the
radio's USB-C connector) and tunes below the
calibrated RX frequency by the configured receive pitch (700 Hz by default),
so an incoming A1A carrier is heard at that pitch. The same pitch is used for
the local TX sidetone. When enabled, the speaker remains open throughout RX:
gating it with the FM squelch clips the start of dits and chops the tone at
every carrier transition.

With `tx_prepare`, PA enable remains off throughout setup, so there is no
unshaped carrier before the first deliberate mark. The compatibility fallback
to atomic `tx_set_params` can still enable RF briefly before the overlay can
remove PA drive. After either path, the app waits for the corrected synthesizer
setting to lock and applies the normal ramp. A spectrum/SDR check is required
before assuming the four-step envelope is spectrally clean on every power
band.

Squeeze keying is available only in `CW USB`: the single-key `get_key`
API shows one SIDE key at a time. The paddle mode has automatic repeat and no
paddle memory.
The TX sidetone follows the approach used by NR7Y's CW implementation: Tone1
is routed to the BK4829 `AF_ALAM` output while register `0x50` keeps TX
modulation muted. Morse edges change only the Tone1 frequency control word in
register `0x71`; writing zero silences the speaker between marks without the
pop caused by repeatedly changing either the tone-enable or AF-selector
register. At the end of the semi-break-in delay, the speaker amplifier is
disabled and the AF DAC gain is set to its minimum before the selector moves
to AF output 5. After the RX profile is restored, that output settles for
50 ms at minimum DAC gain before the amplifier is enabled. The gain then rises
one DAC code at a time instead of jumping directly to full level. Receive
retunes (pitch, crystal correction, frequency offset) use the same sequence.
The over-the-air signal therefore remains A1A rather than MCW. Key `0`
controls both receive audio and the TX sidetone.

When speaker audio is enabled, the speaker amplifier remains off for 10 ms
after the AF selector moves to `AF_ALAM` on the first CW Keyer mark of a TX
session. Tone1 and the RF carrier remain off during this settling interval;
the amplifier is enabled before the sidetone and the shaped RF mark start
together. This avoids both the selector click and a clipped first sidetone.
This CW Keyer-only sequencing does not change CW USB arming.

## Status and known limits

- The original combined app was tested on a UV-K1 (Labs v6.2.0), including
  SIDE and PTT keying, every setting, exit and power-off, timing and decoding
  from 20 to 60 WPM on an SDR. The split binary still requires validation.
- `PADDLE` is a plain automatic keyer without paddle memory; the 0.2 SIDE
  keyer remembered an opposite-paddle tap during an element. Iambic A/B
  (squeeze memory) is left for a later change, after on-air timing checks.
- Above about 40 WPM the fixed 4 ms ramps become a large part of a dot (20 %
  at 60 WPM): the envelope gets rounder, the bandwidth does not change.

## Credits

- CW Keyer application: Armel F4HWN ([@armel](https://github.com/armel)).
- Paddle keyer and contact debounce: Johan Denoyer F4WAT
  ([@jdenoy](https://github.com/jdenoy)).
- TX sidetone approach: NR7Y's CW implementation.
