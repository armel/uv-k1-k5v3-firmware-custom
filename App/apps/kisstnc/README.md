# KISS TNC: the radio as the TNC of APRSdroid over USB

The phone runs the APRS application ([APRSdroid](https://github.com/ge0rg/aprsdroid):
map, messages, beacons), the radio is its TNC: Bell 202 AFSK 1200 bauds on the
VFO, KISS over the USB-C cable (USB OTG on the phone side). Any KISS client on a
computer works the same way (`test/kiss_host.py` is one).

Needs the overlay API level 4 and its serial ports (`APP_CAP_SERIAL`, see
`App/apps/app_api.h`); older firmware refuses the app.

| Check | State |
|---|---|
| Host test (`test/test_tnc.py`) | 50 checks, all pass |
| Reception on the radio | Frames from an FT3D (Mic-E and plain position) reach the computer byte for byte, FCS stripped, KISS escapes right; APRSdroid shows them (2026-10-19) |
| Transmission on the radio | APRSdroid beacon heard directly by an iGate 32 km away and shown on aprs.fi (2026-10-19, UV-K1) |
| USB link at key-up | Holds with a ferrite at the radio end and the antenna away from the cable; drops otherwise, even at low power (see Known limits) |
| AX.25 connected mode on the radio | 1024 B each way with MAXFRAME 1, 2 and 4 against another radio's TNC, intact on narrow and wide (UV-K1, 2026-11-28) |

## What you need

- **A USB OTG adapter between the radio and the phone**: the phone is the
  USB host, the radio a USB device. A computer needs no adapter.
- **An RF filter on the cable, at the radio end**, for any power above Low: a
  clip-on ferrite in VHF material (type 31 or 43), the cable looped 2 to 4
  turns through it. Without it, the RF the cable picks up at key-up resets
  the radio's USB port and the phone drops the link (see Known limits). Keep
  the antenna away from the cable and the phone too.

## Use

1. VFO on the APRS frequency (144.800 MHz in Region 1), FM. Launch **KISS TNC**.
2. APRSdroid: Preferences > Connection: protocol **TNC (KISS)**, type **USB
   serial** (the baud rate is ignored), empty KISS init string. Connect the
   radio to the phone through the OTG adapter, start tracking, accept the USB
   permission. APRSdroid's device filter does not list the radio, so the
   connection is started by hand.
3. Stations heard appear on the map; beacons and messages go out on the VFO.

Frequency, power and bandwidth come from the VFO, as in the other apps.

## Screen and keys

| Line | Shows |
|---|---|
| Status bar | Capsule `USB` (host has the port open, DTR) / `NO USB` / `RX ONLY`; the lock while the keys are locked |
| 0 | `RX`, `TX`: frames received from the air and sent to the air |
| 1 | `BAD`: packets whose frame began (DCD with a carrier over two 50 ms slots or more, the first call of the address in) and did not decode, a weak or distorted signal or a collision; `LOS`: frames lost, from the host and not transmitted (RX only, TX not allowed on this VFO, a narrower VFO, the queue full, longer than AX.25 allows), or from the air with no room left before the next slot |
| 2 | The last station heard: `dBm` its RSSI, `DEC` how many of the three slicers decoded its frame (`3/3` a clean signal, `1/3` one near the edge); empty before the first |
| 3 | `FRM`: its call (6 characters and an SSID, 9 at most) |
| 6 | The keys |

Every count has its fixed place and shows from launch, 0 until it counts. The
counts go up to 9999, then start at 0 again.

The green LED lights while the channel is in use: a carrier (the squelch
open), or a frame coming in (from its first call on: noise passes for a
preamble or a single address byte every few seconds).

| Key | Action |
|---|---|
| 1 | Speaker on / off (saved) |
| 3 | RX only: frames from the host are counted as lost, never transmitted (not saved) |
| # (hold 400 ms) | Key lock on / off: every other key is ignored while it is on, the lock shows in the status bar |
| EXIT | Quit |

## How it works

- **RX**: the APRS RX demodulator (PA4 sampled at 9.6 kHz, band-pass,
  1200 / 2200 Hz correlators, AGC, three slicers with their DPLL, NRZI, HDLC,
  FCS), with the slicers' weights 5:6, 1:1 and 6:5 (about -1.6, 0 and +1.6 dB;
  APRS RX has 2:3, 1:1 and 3:2, outside the range a UV-K1's RX audio decodes
  best in). Every AX.25 frame with a good FCS goes to the host as a KISS data
  frame, FCS stripped; the copy a second slicer gives is dropped. Unlike APRS
  RX there is no APRS-only check: a TNC passes every AX.25 frame. Frames are
  sent in the next housekeeping slot (every 50 ms), never inside the sampling
  loop. While the app runs, the demodulated audio is 6 dB up (REG_43 bit 2)
  and the RX 300 Hz high-pass is off (REG_2B bit 10), whose phase shift moves
  the 1200 Hz tone's edges against the 2200 Hz tone's: on narrow a UV-K1 then
  decoded every long frame from another radio, about three in four without.
  The firmware sets both registers again when the app ends.
- **TX**: KISS bytes are read from USB at every sample into a 1 KiB queue: up
  to 7 APRS-sized frames, or 3 of AX.25's longest (a connected-mode window).
  The queue waits for a clear channel: no slicer inside a preamble or a frame,
  and no carrier (the squelch open, as the radio's SQL setting has it; at
  SQL 0 the squelch is always open, so there the DCD alone holds the queue).
  Then, once the host has been quiet for 20 ms (a window written in more than
  one go stays one transmission), p-persistence: KISS P (default 63, one
  chance in four) once per KISS SLOTTIME (default 100 ms). Every queued frame
  goes out in the same key-up with the APRS TX modulator. The key-up adds no
  time of its own: `tx_prepare` (no firmware waits, the PA off), the tone and
  TX link set, then the PA on and straight into TXDELAY flags (40 = 267 ms by
  default, or KISS TXDELAY x 1.5), so the host's TXDELAY is all the lead-in.
  Each frame is bit-stuffed with its FCS, one flag between two, TXTAIL flags
  after the last (3 at least), then back to RX. Tone gains by the VFO's
  bandwidth (REG_43 bits 5:4, read before each key-up), 2200 Hz 6 dB louder
  than 1200 Hz, as a voice path's pre-emphasis leaves it: narrow 47 / 80 for
  1.0 / 2.0 kHz of deviation, 2.3 kHz at the peak; wide 35 / 68 for 1.5 /
  3.0 kHz, 3.2 kHz at the peak (a UV-K1 on an SDR; the firmware doubles the
  deviation on wide, and the gain is not linear above 66). A narrower VFO
  (6.25 kHz, the NFM option) does not send packets: its frames count as lost.
  Interrupts are masked while the bits play, so nothing delays a tone switch;
  the frames are in RAM before key-up, so a USB problem during TX cannot cut
  them.
- **KISS**: port 0 only (frames for other ports are skipped). Data (0), TXDELAY
  (1), P (2), SLOTTIME (3), TXTAIL (4), FULLDUPLEX (5: send whatever is on the
  channel), ACKMODE (0x0C: the frame's 2 id bytes come back as `C0 0C id id C0`
  once it is on the air), RETURN (0xFF: quits the app). SetHardware (6) answers
  Dire Wolf's two queries: `TNC:` gets `TNC:KISS TNC 2.0`, `TXBUF:` the bytes of
  the frames still queued (`TXBUF:0` when all is sent), so the USB link can be
  checked without RF.
- **USB**: `serial_read` / `serial_write` / `serial_ctl` on `APP_SERIAL_USB`.
  A KISS frame goes out in writes of up to 64 bytes. A write the host does
  not take within 50 ms ends that frame: the next one starts with FEND, so
  the host drops the cut frame and resyncs.

## Known limits

- **RF on the USB cable**: at key-up, RF picked up by the cable can reset the
  radio's USB port. APRSdroid then reports "KissReader out of
  data" (its USB read fails) and stops its service at the first error; it has
  to be restarted, the cable replugged. Seen even at low power with the antenna
  near the cable and a cable of about one wavelength (2 m at 144 MHz). Cures:
  a ferrite at the radio end (VHF material, the cable looped 2-4 turns), the
  antenna away from the cable and the phone, a short cable.
- Tone gains and twist are fixed per bandwidth (measured on a UV-K1).
- Half duplex: no reception while transmitting.

## Tests

`test/test_tnc.py` builds the app for the computer against a mock API and mock
MCU registers (`test/host_tnc.c`, `test/host_hw.h`) and checks it with APRS RX's
channel model:

- RX: six frames (position, digipeated, message, 200 B, status, bad FCS)
  through the radio audio path (RAW and STD) at 0, 1500 and 3000 Hz of noise:
  exactly the frames the Python reference decoder finds, never the bad one;
- USB timeout: the host refuses the 2nd write of a 200-byte frame; that frame
  arrives cut, the next one whole (fails if the app keeps writing after a
  refused chunk);
- TX: the bit stream rebuilt from the tone writes and their timing: 40 flags,
  the frame plus FCS exactly, 1200 / 2200 Hz only, KISS TXDELAY 30 -> 45 flags,
  the tone gains of a narrow and of a wide VFO, nothing sent on a narrower one,
  RX 6 dB up and without the 300 Hz high-pass; the key-up through `tx_prepare`
  without `tx_set_params` or `tx_tone`, AF muted for it and set to FM after
  `tx_end`;
- loop: the modulator's tones, through the channel model, decoded by the
  demodulator, give back the host's frame (STD 0 and 1500 Hz noise, RAW), and
  a burst of 4 frames on shared flags gives back all 4;
- KISS: 4 queued frames go out in one key-up, in order; an ACKMODE frame's id
  comes back after it is sent; a frame for port 1 is not sent; `TNC:` and
  `TXBUF:` are answered, a lone `T` is not; a window of 4 frames (850 B) through
  the port's 256-byte ring at USB speed, at once or as the host's answer to a
  received frame 55 ms later, goes out whole, one in two writes 10 ms apart in
  one key-up; a carrier (the squelch open) holds the queue, one that never ends
  too, but not at SQL 0; of 4 long frames 3 wait and the 4th is dropped
  (`TXBUF:` shows the 3);
- screen: the last station's RSSI, slicer count and call on the counts'
  grid, also a 9-character call, none of it before a station is heard; a
  packet with a carrier whose middle is lost counts as `BAD 1`, a preamble
  alone and a carrier alone do not; no line is longer than the 18 characters
  it has; every count in its place from launch; `#` held 400 ms shows the
  lock and takes it away again, a tap does not;
- green LED: off for a preamble alone and in 20 s of noise without a
  carrier, on for a frame coming in and off again after it.

`test/kiss_host.py` stands in for APRSdroid on a Mac or Linux computer: it
prints every frame the radio sends and can send frames
(`--call F4XXX-7 --count 1 --info '>test'`, which **transmits**: a free APRS
frequency or a dummy load).

Size: 4,088 B of the 4,096 B overlay + 327 B of assets. The queue (1 KiB) and
the received frames (664 B) live on `app_main`'s stack, as in APRS RX.
