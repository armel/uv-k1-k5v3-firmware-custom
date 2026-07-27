# The dock control mode — wire protocol

This firmware adds a **dock control mode**: a serial protocol that lets a host computer read and write
the radio's BK4819 chip registers, take over the radio entirely, and hand it a whole channel to tune
itself to. It is what [radio-server](https://github.com/kbennett2000/radio-server) drives, and this
document exists so you can drive it from **your own** software without reading that project.

The framing and the register commands are byte-compatible with nicsure's
[Quansheng Dock](https://github.com/nicsure/quansheng-dock-fw) firmware for the classic (DP32G030)
UV-K5, from which they were ported. One command, `0x0873`, is an extension that exists only here.

**Two independent implementations agree on every vector below**: this firmware's
[`App/app/dock.c`](App/app/dock.c) and radio-server's `radio_server/backends/uvk5/frames.py`. If you
write a third, the golden frames at the end are your oracle.

---

## The link

**38400 baud, 8N1**, over the radio's K1 jack — in practice via an
[AIOC cable](https://na6d.com/products/aioc-ham-radio-all-in-one-cable), which presents the radio as a
USB serial port *and* a USB sound card on one connector.

The radio **never speaks first** at top level. There is no streaming, no heartbeat, no sequence
numbers, no flow control. Every reply is caused by a command you sent. So to find out whether a radio
is there, you must **elicit** — send something that must be answered and see if it is. Silence means
"no answer", never "idle".

## Framing

```
 AB CD | Size:u16 LE | obf( payload[Size] + CRC:u16 LE ) | DC BA
```

- `payload = [opcode:u16 LE][param_len:u16 LE][params…]`, so `Size = 4 + param_len`.
- Total wire length is `Size + 8`.
- `param_len` may be 0. The payload cap is **254** bytes; a longer frame is dropped, never truncated.

**Obfuscation** is a 16-byte repeating XOR, applied to the payload **and** the two CRC bytes
(`table[i % 16]`, `i` counting from the first payload byte). It is self-inverse — the same operation
decodes.

```
16 6C 14 E6 2E 91 0D 40 21 35 D5 40 13 03 E9 80
```

It is **always on** in this firmware. The classic dock had a plaintext `0x0514` HELLO that toggled
encryption off; this tree hard-defines the link as encrypted and does not implement that toggle, so a
plaintext HELLO gets no answer here. (That is a useful tell: a radio that answers a plaintext HELLO is
on classic or stock firmware, not this one.)

**CRC-16/XMODEM** — poly `0x1021`, init `0`, no reflection, no final XOR — computed over the
**plaintext** payload.

> **The asymmetry that will cost you an afternoon if you miss it.** Commands you send carry a **real
> CRC**, and the firmware validates it and silently drops a frame that fails. Replies the radio sends
> carry a **dummy `obf(0xFF 0xFF)`** in the CRC slot — *not* a real CRC. So **validate outgoing,
> never validate incoming.** A decoder that checks reply CRCs rejects every reply the radio sends.

### Receiving

Sync on `0xAB`; require `0xCD` next; read `Size`; bounds-check `Size + 8`; wait for the whole frame;
require the tail `0xDC 0xBA`. On any mismatch, drop one byte and resync — never truncate a frame to
make it fit.

---

## Commands

| Opcode | Name | Reply | Params |
|---|---|---|---|
| `0x0850` | write registers | **none** | `[count:u16][reg:u16, value:u16] × count` |
| `0x0851` | read registers | `0x0951` **× count** | `[count:u16][reg:u16] × count` |
| `0x0870` | enter full control | **none** | — |
| `0x0871` | exit full control | **none** | — |
| `0x0873` | set VFO | `0x0874`, **always** | 13 bytes, below |
| `0x0951` | register info *(reply)* | — | `[reg:u16][value:u16]` |
| `0x0874` | set-VFO result *(reply)* | — | 12 bytes, below |

An unknown opcode is dropped in silence. That is the only way to detect firmware level from the
outside: send `0x0873` and see whether anything comes back.

### `0x0851` read registers — and how to probe for a radio

`ReadRegisters([0x30])` is the standard liveness elicit: one register, one `0x0951` back. Retransmit
it — opening the serial port can reset the radio, so the first attempt may be eaten by a reboot.

### `0x0870` / `0x0871` full control

`0x0870` takes the radio over: it clears the display, backs up the registers, forces the receive audio
path alive, and then **sits in a loop servicing only serial commands** until `0x0871` arrives. While
it is held:

- **The radio's own logic is suspended** — that is the point, so your register writes are not fought.
- **The PTT button and the keypad are dead.** The loop is not sampling them.
- **The radio's own speaker hisses**, because the squelch is open at the chip. Expected; it stops on
  exit.

`0x0871` calls the firmware's `RestoreRadio()`, which ends in `RADIO_SetupRegisters(true)` — and that
**retunes the synthesiser from the radio's own VFO**.

> **Every register you wrote is thrown away on exit.** Frequency (`0x38`/`0x39`), CTCSS (`0x51`/`0x07`)
> and bandwidth (`0x43`) are all transient. A host that tunes by register can never hand the radio a
> channel and let go. That is precisely why `0x0873` exists.

`0x0873` is deliberately dispatched **outside** the `0x0870` loop. Inside it, it answers `ERR_BUSY`.

---

## `0x0873` set VFO — the extension

Hands the radio a whole channel and lets **its own** code set it up: `RADIO_ApplyOffset` computes the
transmit leg, and `RADIO_ConfigureSquelchAndOutputPower` does the **per-band power-amplifier
calibration that lives in the radio's flash and is not readable from the host**. That last point is
why this command exists rather than a pile of register writes: the calibration is the radio's, so let
the radio apply it.

It writes **both** VFOs. Dual watch alternates which one is current, and a channel applied to only one
of them transmits from the wrong place roughly half the time.

### Request — 13 bytes

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | u32 LE | `rx_hz` | **Hz** |
| 4 | u32 LE | `offset_hz` | **Hz** |
| 8 | u16 LE | `ctcss_tenths` | tenths of a Hz: `1000` = 100.0 Hz. `0` = no tone |
| 10 | u8 | `direction` | `0` simplex, `1` offset up, `2` offset down |
| 11 | u8 | `narrow` | `0` wide FM, `1` narrow |
| 12 | u8 | `power` | `0` low, `1` mid, `2` high |

> **Frequencies on the wire are Hz. The radio's VFO stores 10 Hz units.** The firmware converts. Send
> Hz. (This was wrong in the first cut and was silent, because the radio's band lookup *clamps* an
> out-of-range value instead of rejecting it — so a frequency 10× off landed on a band edge and tuned
> "successfully". The firmware now re-checks against the band table rather than trusting the clamp.)

**Every field is validated and a bad one is refused, never clamped**, on the grounds that a channel
silently moved is worse than a channel refused — a wrong transmit leg is somebody else's repeater.
An offset-down that would underflow is refused rather than wrapped. A CTCSS value must match the
radio's tone table **exactly**; a near-miss is refused rather than transmitted tone-less, because a
tone-less transmission into a tone-guarded repeater simply does not open it and looks like a radio
fault.

### Reply `0x0874` — 12 bytes, sent for every outcome

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | `status` |
| 1 | u8 | `power` — the **radio's** `OUTPUT_POWER_*`, not the 0/1/2 you sent |
| 2 | u32 LE | `rx_hz` |
| 6 | u32 LE | `tx_hz` — **the leg that actually radiates** |
| 10 | u16 LE | `ctcss_tenths` as applied |

| `status` | Meaning |
|---|---|
| `0` | applied, exactly as requested |
| `1` | payload shorter than 13 bytes |
| `2` | busy — the host holds full control (`0x0870`); retry after `0x0871` |
| `3` | offset direction was not 0/1/2 |
| `4` | bandwidth or power off its scale |
| `5` | firmware built without the radio-side binding |
| `6` | the receive or transmit leg falls outside every band this radio has |
| `7` | tone absent from the radio's CTCSS table |

**On any non-zero status the frequency fields are zeroed**, unconditionally, in the protocol core
rather than in each binding. So a reply can never describe a channel the radio is not on, which makes
`status` the only field you have to check first.

The frequencies are read back out of the radio's VFO **after** it applied them — they are where it
landed, not what you asked for. Compare them.

### The power scale is not the scale you sent

The wire's `0`/`1`/`2` maps through the firmware to `OUTPUT_POWER_LOW1`, `MID`, `HIGH`, whose numeric
values in the radio's own enum (`USER, LOW1..LOW5, MID, HIGH`) are **1, 6 and 7**.

> Assigning the wire value raw makes "high" mean `LOW2`. That tunes perfectly, reads back perfectly,
> and **never opens a repeater** — invisible from the host until somebody notices the radio is quiet.
> That bug shipped here and was caught only once `0x0874` started reporting the applied value. If you
> implement this, check `power` in the reply against `{low:1, mid:6, high:7}`.

---

## Golden vectors

Byte-exact frames, verified against both implementations. Use these before you trust your codec.

**`0x0851` read register `0x30`, which holds `0xC1FE`** → the `0x0951` reply:

```
AB CD 08 00 47 65 10 E6 1E 91 F3 81 DE CA DC BA
```
deobfuscated payload: `51 09 04 00 30 00 FE C1`

**`0x0873` request** — receive 448.525 MHz, transmit 5 MHz down, 100.0 Hz tone, wide, high power
(params only, before framing):

```
C8 F2 BB 1A 40 4B 4C 00 E8 03 02 00 02
```

**its `0x0874` reply** — applied, `power = 7` (`HIGH`), transmit leg 443.525 MHz:

```
AB CD 10 00 62 64 18 E6 2E 96 C5 B2 9A 2F 5D E7 7C 19 01 83 E9 93 DC BA
```
deobfuscated payload: `74 08 0C 00 00 07 C8 F2 BB 1A 88 A7 6F 1A E8 03`

---

## Things that are not in the protocol, and will still bite you

**A six-second transmit lockout.** The radio's *stock* EEPROM commands — the HELLO `0x0514`, the
EEPROM read `0x051B`, the EEPROM write `0x051D`, and `0x052F` — each arm a **6-second timer during
which the radio refuses to transmit, and cuts an over already in progress**. Reading arms it as surely
as writing. **No dock command arms it**, which is the main practical reason to tune with `0x0873`
rather than by writing the radio's memory. If you mix the two, expect the transmitter to be dead for
six seconds after any memory conversation.

**Receive audio needs more than the chip.** The audio path to the K1 jack passes the BK4819's audio
selector *and* an MCU GPIO that enables the external amplifier — and that GPIO is **not reachable over
this protocol**. Full-control mode starves the firmware timeslice that would normally raise it, so a
host that only writes registers gets a radio that receives **silence** while every register reads back
correct. This firmware forces the path alive on `0x0870` entry (that is what "F3" means below). On
older builds, no amount of register writing fixes it.

**Transmit needs more than `REG_30`.** Keying by writing the transmit-DSP bit is not enough: the stock
transmit path also raises the power-amplifier enable and sets the PA bias. Without those the radio
reports a successful key-up, the register read-back confirms it, and **nothing usable radiates** — a
near-field sniff sees a carrier and an antenna run sees nothing. This firmware performs the stock PA
sequence on the key-up edge (that is "F5"). Diagnosing this cost a full cycle.

**A dock transmission leaves the receiver on the wrong band.** After a dock-mode transmit, the
firmware's filter-path selection leaves the *receive* front-end configured for the VFO's band, which
may not be the band you transmitted on. That is **not fixed in firmware** — radio-server corrects it
host-side. If you tune far from where the radio's own VFO sits, expect to re-assert your receive
registers after every over.

---

## Firmware levels

The dock mode arrived in stages, and each unlocked something the one before it lacked. Releases are
tagged `radio-server-fN-v5.7.0`.

| Level | Adds | Without it |
|---|---|---|
| **F1** | nothing — a build gate proving fork → build → flash → boot on an unmodified image | — |
| **F2** | the dock mode itself: `0x0850`/`0x0851`/`0x0870`/`0x0871` | no dock at all; the commands are silently ignored |
| **F3** | forces the receive audio path alive on `0x0870` | connects, and receives silence |
| **F5** | engages the power amplifier on the key-up edge | keys cleanly, radiates nothing usable |
| **F6** | `0x0873`/`0x0874` set-VFO | tuning does not survive `0x0871`; no power control |

**F6 is cumulative** — it contains F2, F3 and F5. Flash that one.

### Detecting the level from your own software

There is no version command (the plaintext HELLO is not answered here). So ask the *command*:

1. Prove the link with a `0x0851` read of register `0x30`. A `0x0951` back means **F2 or later**.
2. Send a `0x0873` with an **empty** payload. A `0x0874` back — status `1`, `ERR_SHORT` — means
   **F6 or later**. Silence means older.

Step 2 is safe by construction: the length check is the first branch of that command, so the firmware
refuses before it decodes a field, before it calls its VFO binding, and with every frequency in the
reply blanked. It is a question, not a tune. Any `0x0874` answers it — `ERR_BUSY` proves the command
exists just as well as `ERR_SHORT`.

---

## Implementing against this

- **[`App/app/dock.c`](App/app/dock.c) / [`dock.h`](App/app/dock.h)** are pure C with **no firmware or
  hardware includes** — all hardware sits behind a caller-supplied `dock_hal_t`. You can compile them
  on a host and use them as a reference decoder directly.
- **[`tests/host/test_dock.c`](tests/host/test_dock.c)** is 66 checks including the golden frames
  above. `make -C tests/host run`. It needs nothing but a C compiler.
- **`radio_server/backends/uvk5/frames.py`** in
  [radio-server](https://github.com/kbennett2000/radio-server) is a complete, independently-written
  Python implementation of this protocol, with the register-level cookbook (frequency, bandwidth,
  CTCSS, key-up/key-down) in its `radio.py`.
- The BK4819 register sequences for tuning and keying are **not** part of this protocol — they are the
  chip's, reachable through `0x0850`/`0x0851`. See radio-server's ADR 0112 for the derived sequences.
