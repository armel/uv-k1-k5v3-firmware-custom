# CW Keyer overlay app

`CW Keyer` transmits manually keyed A1A-style carrier Morse on the current TX
VFO. It is entirely contained in the 4 KiB overlay and uses only API level 2;
it has no resident firmware component.

## Controls

- `PTT`: straight-key carrier in either mode.
- `SIDE1` / `SIDE2` in `STRAIGHT`: additional straight keys.
- `SIDE1` / `SIDE2` in `PADDLE`: automatic dits and dahs with element memory.
- `0`: enable or mute the CW receive audio path.
- `1`: switch between `STRAIGHT` and `PADDLE`.
- `2`: raise the keyer speed.
- `3`: raise the receive CW tone by 50 Hz.
- `4`: raise the semi-break-in delay by 50 ms.
- `5`: swap the SIDE1/SIDE2 dit and dah assignments.
- `6`: raise the crystal correction by 0.01 ppm.
- `F`, then `1` to `6`: apply the corresponding setting in the reverse
  direction. Two-state settings (`MODE` and paddle order) simply toggle.
- `UP` / `DOWN`: tune the app frequency by a fixed 100 Hz CW step. Holding a
  direction repeats after 400 ms, then every 100 ms.
- `MENU`: disarm TX and open the help page.
- `EXIT`: leave the app from the main page, or return to the main page from
  help. `MENU` also returns from help.

The main screen shows all six controls as compact, equal-width 43-pixel
3x5-font capsules in the bottom three lines below the frequency. The left
column is anchored to the left edge, the right column to the right edge, and
every label is left-aligned inside its capsule: `STRAIGHT` / `PADDLE`, `WPM`,
`TONE`, `BK`, `DIT-DAH` / `DAH-DIT`, and `XTAL`. Keyboard shortcuts are shown
on the help page instead of inside the capsules. While help is open, keying and
setting changes are disabled; `MENU` or `EXIT` returns to the main page.
The Foxhunt speaker icon shows that receive audio is enabled; the `F` icon
temporarily replaces it while reverse adjustment is armed.

The speed, mode, paddle assignment, calibration, receive pitch, break-in delay
and speaker state are saved when the app exits. The default correction is +2.77 ppm,
measured as +400 Hz at 144.225 MHz and +1.20 kHz at 433.225 MHz. A positive
displayed correction means the uncorrected carrier was high; the app therefore
lowers the programmed TX frequency proportionally. The PTT always remains a
straight key, so it is usable even while `PADDLE` is selected.
The frequency offset selected with `UP` / `DOWN` is local to the overlay. It is
applied equally to CW RX, TX and the displayed frequency, and resets to zero
when the app exits; the resident VFO is never modified.
Interface keys are accepted only after a stable 20 ms reading. PTT and the two
side keys remain immediate, preventing keypad-matrix transients from opening
help or changing a setting without adding latency to CW keying.
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
`MENU`, `EXIT`, and every abort path call `tx_end` to disable the PA and restore
normal RX. After the final mark, the configurable semi-break-in hang (300 ms
by default) keeps the synthesizer ready across Morse element gaps, then
automatically calls `tx_end` and restores reception without requiring `MENU`.
The overlay then selects the BK4829 USB baseband output and tunes below the
calibrated RX frequency by the configured receive pitch (700 Hz by default),
so an incoming A1A carrier is heard at that pitch. When enabled, the speaker
remains open throughout RX: gating it with the FM squelch clips the start of
dits and chops the tone at every carrier transition.

The atomic `tx_set_params` service necessarily enables RF briefly before the
overlay can remove PA drive. The app then waits for the corrected synthesizer
setting to lock and applies the normal ramp to the first deliberate mark. A
spectrum/SDR check is required before assuming the four-step envelope is
spectrally clean on every power band.

True iambic A/B squeeze keying is not possible through the current single-key
`get_key` API. The paddle mode supports one paddle at a time, automatic repeat,
and memory of the opposite paddle when the scanner reports it during an element.
No sidetone is generated: the existing tone services also feed the TX
modulation path, which would turn the signal into MCW instead of A1A.
