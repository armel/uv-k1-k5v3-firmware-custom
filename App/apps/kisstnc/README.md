# KISS TNC

KISS TNC turns the radio into a 1200 baud AX.25 modem on the Kenwood
programming jack. A host program that already speaks to a serial KISS TNC
can send and receive real AX.25 frames. APRS is the usual payload; the modem
does not require UI frames, so connected-mode AX.25 passes as well.

The receiver is the APRS RX Bell 202 demodulator. The transmitter is the
APRS TX tone generator. The host link is the firmware USART at 38400 8N1,
the same port as the programming cable. No firmware rebuild is required.

## Host setup

1. Put the VFO in FM on the frequency you are licensed to use. For simplex
   APRS, set the receive and transmit frequencies to the same channel.
2. Set the squelch above zero. SQL mode will not transmit while the squelch
   is open.
3. Start KISS TNC, then open the programming-cable serial port from the host
   at **38400 8N1**, KISS, one port. Hardware flow control should be off.
4. EXIT on the radio leaves the modem and restores the programming port.

YAAC, Xastir and APRSISCE/32 use this the same way. Close the host before
using a radio programmer on the same cable.

### PinPoint APRS

Tools, Options, TNC:

| Setting | Value |
|---|---|
| TNC type | Serial KISS mode |
| Serial speed | 38400 |
| Data bits / parity / stop bits | 8 / None / 1 |
| Flow control | None |
| KISS TX port | 0 |
| RF speed | 1200 |
| Send startup script | off |
| Send shutdown script | off |

Kenwood TH-D7 / TM-D710 in the TNC list is the text TNC2 protocol, not this
modem. Start KISS TNC on the radio before connecting in PinPoint.

KISS data is the AX.25 frame without flags and without the FCS. The TNC
checks the FCS on receive and strips it, and appends a new FCS, flags and
bit stuffing on transmit. TXDELAY, persistence, slot time, TX tail and
full duplex are the standard KISS commands. The port nibble is ignored.

| KISS command | Effect | Power-on value |
|---|---|---|
| Data (0) | Queue one AX.25 frame | |
| TXDELAY (1) | Preamble, 10 ms units, at least four flags | 30 (300 ms) |
| Persistence (2) | Send when a random byte is at most this value | 255 (always) |
| SlotTime (3) | Wait after the channel is clear, 10 ms units | 10 (100 ms) |
| TXTAIL (4) | Closing flags, 10 ms units, at least two flags | 3 |
| FullDuplex (5) | 0 = SQL again, nonzero = transmit without carrier sense | 0 |

The receiver pauses while a frame is on the air. One host frame can wait.
A second one that arrives before that transmission is counted as a drop.
The modem listens again 50 ms after the transmitter returns to receive.

## Keys

| Key | Action |
|---|---|
| 1 / F then 1 | Tone level up / down (10-127, step 4, default 66). Saved |
| 2 / F then 2 | Twist up / down, -4..+8. Saved. This is the 2200 Hz level relative to 1200 Hz |
| 3 | Carrier sense: `SQL`, `DCD`, `FDX`. Saved |
| F | The next 1 or 2 adjusts downward |
| EXIT | Leave the modem and restore the programming port |

`SQL` waits until the hardware squelch is closed and the modem is not inside
a frame, then waits one slot and applies persistence. `DCD` ignores the
hardware squelch and uses the modem only; use it when the squelch is forced
open. `FDX` transmits on the next housekeeping pass.

The status line is the modem. `ON` is listening, `WAIT` is a host frame
queued, `TX` is on the air, and `NO TX` means the radio refused to
transmit. `SQL`, `DCD` and `FDX` are the carrier-sense mode. `F` means the
next 1 or 2 adjusts downward.

Before a frame passes, the large line says `Listening`. After that it
shows the latest station: `RX` or `TX` and the source callsign. The next
line is the tone level (`L`) and the twist. The frequency under that is
the receive VFO. Transmission uses the transmit VFO and the current power
level.

Level, twist and carrier-sense mode are saved. The KISS timing values are
not: the host sets them after it connects.

## Bench test

`test/kiss.py` checks FCS handling, escaping, parameter commands and the
rule that a second queued frame is dropped. It does not compile the radio
app. On the air, use a second APRS receiver and a legal test frequency
before a live channel.
