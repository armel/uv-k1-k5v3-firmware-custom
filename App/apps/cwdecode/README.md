# CW Decode overlay app

`CW Decode` decodes keyed RF carriers (A1A) from the BK4829 RSSI envelope. It
also uses the BK4829 USB baseband output as a product detector, with the
receiver internally tuned 700 Hz below the displayed carrier frequency. This
makes incoming CW audible without changing the RSSI-based decoding path.

The adaptive detection threshold starts 4 dB above the calibrated quiet-channel
level. This margin remains adjustable and is saved with the app configuration.

## IC-705 bench test

1. Select the same quiet 2 m or 70 cm frequency on both radios.
2. Put the IC-705 in CW mode and use the lowest practical RF power, or use a
   dummy load / attenuated coupling for a conducted test.
3. Tune the UV-K1/K5 V3 to that frequency and launch `CW Decode`.
4. Keep the IC-705 key up while `KEEP CHANNEL QUIET` is displayed.
5. Send `VVV` first so the timing estimator settles, then send clean Morse.
   The automatic start-up capture range is roughly 8–22 WPM. For a faster
   operator, press `2` as needed before sending; use `F` then `2` as needed for
   a slower operator.

## Display

- The up/down marks show whether more decoded text is available.
- The boxed `F` icon means that the next adjustable key runs in reverse.
- The speaker icon, shared with Foxhunt, means that receive audio is enabled.
  The `F` icon temporarily replaces it while reverse adjustment is armed.
- The `AGC` icon means that automatic receiver gain is enabled.
- The upper capsule row shows detection `THR` on the left, the adaptive `WPM`
  speed estimate in the centre, and `RSSI` on the right. Their frames reserve
  the maximum value widths and therefore remain fixed. The dBm suffix is
  omitted from the main screen and documented in the lower-right corner of
  the help page. The bottom row shows the current `MORSE` symbol on the left
  and the RX frequency on the right, using Beacon's normal-font format.

## Controls

- `UP` / `DOWN`: smooth pixel-by-pixel history scrolling.
- `0`: enable or mute the continuously open CW receive audio path.
- `*`: switch between normal and compact decoded-text fonts.
- `1`: raise the detection threshold; `F` then `1` lowers it.
- `2`: raise the current WPM bias; `F` then `2` lowers it.
- `3`: enable or disable AGC, then repeat quiet-channel calibration.
- `4`: repeat quiet-channel calibration.
- `5`: clear the decoded history.
- `MENU`: open the help page.
- `EXIT`: leave the app from the decoder, or return to the decoder from help.
  `MENU` also returns from help.

Threshold and RSSI values are expressed in dBm.

The help page uses the same two-column capsule layout as CW Keyer. Decoding
and audio continue in the background while help is displayed, but its setting
and scrolling controls are ignored until `MENU` or `EXIT` returns to the
decoder.

A normal Morse word gap schedules a space. A much longer silence of 15 dot
units ends the sequence (about 1.2 seconds at 15 WPM), but does not create an
empty line. The pending space or line break is inserted only when the next
keyed carrier starts. Automatic following advances by one complete text row;
manual history scrolling remains pixel-smooth, and a new keyed carrier resumes
automatic following on the active line. The font, threshold margin, AGC mode
and speaker state are saved. To protect mark timing, decoded text is redrawn during the longer
inter-word silence; the `MORSE` capsule continues to show each symbol between
full redraws. Timing is relearned on every launch. Letters A-Z, digits 0-9 and
the `+`, `=` and `/` symbols are supported; malformed or unsupported patterns
are shown as `?`.
