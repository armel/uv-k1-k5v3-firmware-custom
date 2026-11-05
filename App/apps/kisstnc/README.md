# KISS TNC: the radio as the TNC of APRSdroid over USB

The phone runs the APRS application ([APRSdroid](https://github.com/ge0rg/aprsdroid):
map, messages, beacons), the radio is its TNC: Bell 202 AFSK 1200 bauds on the
VFO, KISS over the USB-C cable (USB OTG on the phone side). Any KISS client on a
computer works the same way (`test/kiss_host.py` is one).

Needs the overlay API level 3 serial ports (`APP_CAP_SERIAL`, see
`App/apps/app_api.h`); older firmware refuses the app.

| Check | State |
|---|---|
| Host test (`test/test_tnc.py`) | 19 checks, all pass |
| Reception on the radio | Frames from an FT3D (Mic-E and plain position) reach the computer byte for byte, FCS stripped, KISS escapes right; APRSdroid shows them (2026-10-19) |
| Transmission on the radio | APRSdroid beacon heard directly by an iGate 32 km away and shown on aprs.fi (2026-10-19, UV-K1) |
| USB link at key-up | Holds with a ferrite at the radio end and the antenna away from the cable; drops otherwise, even at low power (see Known limits) |

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
| Status bar | Capsule `USB` (host has the port open, DTR) / `NO USB` / `TX` / `RX ONLY` |
| 0 | Frames received from the air and sent to the air |
| 2 | Last station heard, with its RSSI |
| 3 | `drop N`: frames from the host not transmitted (RX only, or TX not allowed on this VFO) |
| 4 | VFO frequency |
| 5 | `to N ov N re N`: USB link events since launch (shown once one happened) |

| Key | Action |
|---|---|
| 1 | Speaker on / off (saved) |
| 2 | Send a fixed test frame to the host, without RF: checks the USB link |
| 3 | RX only: frames from the host are counted as dropped, never transmitted (not saved) |
| EXIT | Quit |

## How it works

- **RX**: the APRS RX demodulator, unchanged (PA4 sampled at 9.6 kHz, band-pass,
  1200 / 2200 Hz correlators, AGC, three slicers with their DPLL, NRZI, HDLC,
  FCS). Every AX.25 frame with a good FCS goes to the host as a KISS data frame,
  FCS stripped; the copy a second slicer gives is dropped. Unlike APRS RX there
  is no APRS-only check: a TNC passes every AX.25 frame. Frames are sent in the
  next housekeeping slot (every 50 ms), never inside the sampling loop.
- **TX**: KISS bytes are read from USB every 10 ms. A data frame waits for a
  clear channel (no slicer inside a preamble or a frame), then p-persistence
  (KISS P, default 63: one chance in four per 50 ms slot). It is sent with the
  APRS TX modulator: TXDELAY flags (40 = 267 ms by default, or KISS TXDELAY
  x 1.5), the frame bit-stuffed with its FCS, 3 tail flags, then back to RX.
  Tone level 66 (APRS TX's default), no twist. Interrupts are masked while the
  bits play, so nothing delays a tone switch; the whole frame is in RAM before
  key-up, so a USB problem during TX cannot cut it.
- **KISS**: data frames (command 0), TXDELAY (1) and P (2) are honoured; the
  other commands are accepted and ignored.
- **USB**: `serial_read` / `serial_write` / `serial_ctl` on `APP_SERIAL_USB`.
  A KISS frame goes out in writes of up to 64 bytes. A write the host does
  not take within 50 ms ends that frame: the next one starts with FEND, so
  the host drops the cut frame and resyncs.

## USB link events (line 5)

- `to`: a write the host did not take within 50 ms (data dropped, link kept).
  A host that reads late shows here, and the link recovers by itself.
- `ov`: the 256-byte receive ring was lapped (host data lost).
- `re`: the host enumerated the radio again, i.e. a USB reset: RF on the cable
  at key-up, or the cable replugged.

A link that stops with `re` going up is RF on the cable.

## Known limits

- **RF on the USB cable**: at key-up, RF picked up by the cable can reset the
  radio's USB port (`re` goes up). APRSdroid then reports "KissReader out of
  data" (its USB read fails) and stops its service at the first error; it has
  to be restarted, the cable replugged. Seen even at low power with the antenna
  near the cable and a cable of about one wavelength (2 m at 144 MHz). Cures:
  a ferrite at the radio end (VHF material, the cable looped 2-4 turns), the
  antenna away from the cable and the phone, a short cable.
- One frame from the host at a time: a frame arriving while another waits for
  the channel is dropped (APRSdroid sends one frame per beacon).
- The channel check is the demodulator's DCD only: a voice carrier is not seen.
- Tone level and twist are fixed (APRS TX defaults).
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
  the frame plus FCS exactly, 1200 / 2200 Hz only, KISS TXDELAY 30 -> 45 flags;
- loop: the modulator's tones, through the channel model, decoded by the
  demodulator, give back the host's frame (STD 0 and 1500 Hz noise, RAW);
- key 2: the test frame for a 5-letter, an empty (NOCALL) and a 7-letter
  callsign (cut to 6).

`test/kiss_host.py` stands in for APRSdroid on a Mac or Linux computer: it
prints every frame the radio sends and can send frames
(`--call F4XXX-7 --count 1 --info '>test'`, which **transmits**: a free APRS
frequency or a dummy load).

Size: 3,620 B of the 4,096 B overlay + 339 B of assets.
