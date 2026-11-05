# APRS DIGI: WIDE1-1 / WIDE2-1 digipeater

APRS DIGI is a self-contained Fusion overlay application. It receives Bell 202
AFSK at 1200 baud with the APRS RX software demodulator and repeats eligible
AX.25 UI frames with the APRS TX tone generator.

Version 0.1 starts as a `WIDE1-1` fill-in. Key `1` switches between:

- `W1`: only `WIDE1-1` is accepted;
- `W12`: both `WIDE1-1` and `WIDE2-1` are accepted.

The selected mode is shown in a 3x5-font capsule in the status bar and is saved
when the app closes. In either mode:

- the first unused path address must be `WIDE1-1`, or `WIDE2-1` in `W12`;
- it is replaced in place by the boot-message callsign with SSID 3 and the H bit;
- source, destination, remaining path and information bytes are preserved;
- the FCS is recalculated;
- the eight most recent logical packets are suppressed for up to 30 seconds,
  ignoring path and FCS;
- a frame already containing this digipeater callsign with its H bit is dropped;
- the complete BK4829 receive profile is restored after every retransmission,
  followed by a 50 ms discriminator settling interval;
- only one frame may wait for transmission.

`WIDE2-2` and all higher hop counts remain unsupported.

The app remains an overlay and does not add resident APRS support or change the
overlay API.

## Requirements

- Fusion firmware and the matching application ABI level 2;
- a plain 1-6 character callsign in boot-message line 1;
- FM on the selected VFO;
- a non-zero squelch setting. The channel must remain squelch-closed for a
  randomized 100-227 ms before transmission. A pending frame is dropped after
  3 seconds. Hearing the same logical packet from another digipeater during
  that wait cancels the queued copy.

The digipeater identifies as the boot callsign with SSID `-3`. EXIT stops it.
The screen shows accepted AX.25 UI frames (`RX`) and transmitted frames (`DIGI`)
on its second row, then logical duplicates (`DUP`) and all other drops (`DROP`)
on its third row. A dotted separator and the large receive frequency match the
APRS RX and APRS TX layout. Under the frequency, the RSSI of the most recently
decoded frame remains displayed until another valid frame is received.

## Bench test

Use three devices when possible: a source, the radio running APRS DIGI, and an
independent APRS receiver. Start at minimum power on a lawful test frequency,
not on the live APRS channel.

The existing Flipper files in `../aprsrx/test/flipper` provide useful cases:

| File | Expected result |
|---|---|
| `aprs_pos.sub` | repeated once as `MYCALL-3*` |
| `aprs_msg.sub` | repeated once |
| `aprs_long.sub` | repeated as `MYCALL-3*,WIDE2-2` |
| `aprs_status.sub` | dropped: no path |
| `aprs_digi.sub` | dropped: first unused hop is `WIDE2-1` |
| `aprs_badfcs.sub` | rejected before the digipeater logic |

Sending the same eligible frame repeatedly inside 30 seconds must produce only
one transmission. After 30 seconds it may be repeated again.

In `W12` mode, `aprs_digi.sub` is instead repeated once as
`F1ZZZ-10*,MYCALL-3*`. Press `1` to return to the default `W1` mode.

`test/model.py` checks path replacement, FCS, payload preservation, loop
prevention and the negative cases without compiling the firmware.
