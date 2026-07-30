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
| `0x0877` | set modulation | `0x0878`, **always** | 1 byte, below |
| `0x0951` | register info *(reply)* | — | `[reg:u16][value:u16]` |
| `0x0874` | set-VFO result *(reply)* | — | 12 bytes, below |
| `0x0878` | set-modulation result *(reply)* | — | 4 bytes, below |

An unknown opcode is dropped in silence. That is the only way to detect firmware level from the
outside: send `0x0873` or `0x0877` and see whether anything comes back.

> **`0x0875`/`0x0876` are deliberately skipped.** radio-server's ADR 0111 records the classic
> Quansheng Dock's full-control extras as *"0x0872 modulation, 0x0873/4 backlight, 0x0875/6 AM
> emulation"*. That census cannot be re-verified from this tree, so it is treated as claimed rather
> than assumed free — which is the check `0x0873`'s own allocation skipped: it was chosen after
> ruling out `0x0872` alone, from a range the same census had already spoken for. That one is
> shipped and cannot be walked back. If you are extending this protocol, **check the census before
> you allocate**; a wire opcode is the one thing here you cannot rename later.

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

## `0x0877` set modulation — the other extension

Puts the radio on a demodulator. Like `0x0873`, it writes the radio's **own** VFO and lets the
firmware's setup path apply it, rather than poking the chip: a chip-only change does not survive, and
it applies only half of what a modulation change involves (the AM filter bandwidth is set on the way
into AM and never restored on the way out; the compander and the CTCSS interrupt mask are gated on FM
elsewhere).

It writes **both** VFOs, for the same dual-watch reason `0x0873` does.

### Request — 1 byte

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | `modulation` | `0` FM, `1` AM. `2` is **reserved for USB and refused** — see below |

**The wire's values are not the firmware's enum**, on purpose. The radio's `ModulationMode_t` is
`{ FM, AM, USB, [BYP, RAW,] UKNOWN }`, and the bracketed pair only exists on builds with
`ENABLE_BYP_RAW_DEMODULATORS` — so the enum's numeric end **moves with a build flag**. A wire value
derived from it would mean different things in two builds of the same protocol. Send the wire's
numbers; the firmware maps them, both ways.

`2` (USB) has its **number** fixed so it can never come to mean anything else, but the **value** is
refused at F7 with `ERR_FIELD`. Nobody has put this radio on USB at a bench, it cannot transmit in it
on this build anyway, and a refusal never moves the radio — so accepting it later is purely additive.
Anything above `2` is refused as well. **Refused, never clamped.**

### The modulation is sticky, and `0x0873` carries it

After one successful `0x0877`, **every later `0x0873` applies that modulation too**. This is not a
convenience. `0x0873` has to write *some* modulation into the VFO it applies; it used to write FM
literally, so "set AM, then tune" put the radio silently back on FM. And sending the two as separate
frames is not merely awkward — **this link drops frames**. The firmware is single-threaded and
anything arriving while it is busy is discarded, not queued. Tune lands, set-modulation is dropped,
and the radio sits on the right channel in the wrong demodulator with nothing on the wire saying so.

Two rules follow, and both are requirements on **you**:

- The sticky value is **session state, seeded FM**. It is never read back out of the radio — adopting
  whatever the front panel was last left on is how a repeater channel ends up in the wrong
  demodulator. A host that never sends `0x0877` gets FM on every tune, exactly as before F7.
- It is scoped to the **radio's power cycle, not your connection**. It outlives your process. **A
  reconnecting host must assert the modulation it wants**, not assume FM.

A refused `0x0877` — busy, short, out of range — **never** moves the sticky value. Only a modulation
the radio actually took becomes what later tunes apply.

### Reply `0x0878` — 4 bytes, sent for every outcome

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | `status` |
| 1 | u8 | `modulation` — the wire value the radio is **now** on, or `0xFF` |
| 2 | u8 | `raw` — the radio's own `ModulationMode_t`. **Diagnostic only** |
| 3 | u8 | `flags` — bit 0: the radio will key its own transmit path in this modulation |

| `status` | Meaning |
|---|---|
| `0` | applied, exactly as requested |
| `1` | payload was empty |
| `2` | busy — the host holds full control (`0x0870`); retry after `0x0871` |
| `4` | not a modulation this firmware accepts |
| `5` | firmware built without the radio-side binding |

The numbers are `0x0874`'s, **holes and all** — `3`, `6` and `7` cannot arise here and are left unused
rather than renumbered, so one status table decodes both commands and "status 4 means a field was off
its scale" stays true whichever produced it.

`modulation` is read back out of the radio's VFO **after** it applied it, not echoed from the request.
`raw` is there for the same reason `0x0874` reports the radio's own `OUTPUT_POWER_*`: when the two
scales disagree, only the raw value makes it visible. Its numbering shifts with
`ENABLE_BYP_RAW_DEMODULATORS`, so read it, log it, and **never branch on it**.

> **On any non-zero status, `modulation` and `raw` are `0xFF` — not `0`.** `0x0874` blanks its
> frequencies to zero because 0 Hz is obviously not a channel. Copying that here would blank
> modulation to `0`, which **is FM** — a refusal shipping a plausible claim about where the radio is.
> `0xFF` also means "the radio is on something this wire cannot name", which is what you get from a
> build with the BYP/RAW demodulators compiled in.

> **A successful set-AM can stop the transmitter, and that is what `flags` is for.** Built without
> `ENABLE_TX_WHEN_AM` — which this firmware is not — the radio refuses to transmit in any modulation
> that is not FM. That refusal lives in the path the radio's **PTT pin** drives, so if you key by
> asserting a serial control line into that pin (an AIOC cable, say), **your transmitter stops
> working the moment you select AM**. Keying by writing `REG_30` over `0x0850` does *not* go through
> that path and is unaffected — so the same radio state means "cannot transmit" for one host and
> "transmits normally" for another. You cannot see a build flag from out here; check bit 0.
>
> The flag **reports** the condition. It does not remove it. **AM is receive-only on this firmware**,
> and enabling `ENABLE_TX_WHEN_AM` is not something this fork does.

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

**`0x0877` request** — set AM (full frame):

```
AB CD 05 00 61 64 15 E6 2F 11 D5 DC BA
```
deobfuscated payload: `77 08 01 00 01`

**its `0x0878` reply** — applied, AM, `raw = 1`, `flags = 0` (**the radio will not key its own PTT
path in AM**):

```
AB CD 08 00 6E 64 10 E6 2E 90 0C 40 DE CA DC BA
```
deobfuscated payload: `78 08 04 00 00 01 01 00`

**`0x0877` with an empty payload** — the F7 probe:

```
AB CD 04 00 61 64 14 E6 D7 2B DC BA
```
deobfuscated payload: `77 08 00 00`

**its `0x0878` reply** — `ERR_SHORT`, naming no modulation:

```
AB CD 08 00 6E 64 10 E6 2F 6E F2 40 DE CA DC BA
```
deobfuscated payload: `78 08 04 00 01 FF FF 00`

---

## Things that are not in the protocol, and will still bite you

**A six-second transmit lockout.** The radio's *stock* EEPROM commands — the HELLO `0x0514`, the
EEPROM read `0x051B`, the EEPROM write `0x051D`, and `0x052F` — each arm a **6-second timer during
which the radio refuses to transmit, and cuts an over already in progress**. Reading arms it as surely
as writing. **No dock command arms it** — not `0x0873` and not `0x0877` either — which is the main
practical reason to tune with `0x0873` rather than by writing the radio's memory. If you mix the two,
expect the transmitter to be dead for six seconds after any memory conversation.

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

**A non-FM modulation stops the radio's own transmit path.** Covered in full under `0x0877` above,
repeated here because it is the kind of thing you find at a bench rather than in a spec: built without
`ENABLE_TX_WHEN_AM`, the radio refuses to transmit in anything but FM, and that refusal is in the path
the **PTT pin** drives. Keying by serial control line into that pin stops working the moment you
select AM; keying by `REG_30` over `0x0850` does not. `0x0878`'s `flags` bit 0 is how you tell.

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
| **F7** | `0x0877`/`0x0878` set-modulation, and `0x0873` stops forcing FM | the radio is FM-only; there is no way to receive AM |

**F7 is cumulative** — it contains F2, F3, F5 and F6. Flash that one.

### Detecting the level from your own software

There is no version command (the plaintext HELLO is not answered here). So ask the *command*:

1. Prove the link with a `0x0851` read of register `0x30`. A `0x0951` back means **F2 or later**.
2. Send a `0x0873` with an **empty** payload. A `0x0874` back — status `1`, `ERR_SHORT` — means
   **F6 or later**. Silence means older.
3. Send a `0x0877` with an **empty** payload. A `0x0878` back — status `1`, `ERR_SHORT` — means
   **F7 or later**. Silence means F6 or older.

Steps 2 and 3 are safe by construction: the length check is the first branch of each command, so the
firmware refuses before it decodes a field, before it calls its binding, and with every frequency (or
modulation) in the reply blanked. They are questions, not commands. Any reply answers — `ERR_BUSY`
proves the command exists just as well as `ERR_SHORT` does.

---

## Implementing against this

- **[`App/app/dock.c`](App/app/dock.c) / [`dock.h`](App/app/dock.h)** are pure C with **no firmware or
  hardware includes** — all hardware sits behind a caller-supplied `dock_hal_t`. You can compile them
  on a host and use them as a reference decoder directly.
- **[`tests/host/test_dock.c`](tests/host/test_dock.c)** is 98 checks including the golden frames
  above. `make -C tests/host run`. It needs nothing but a C compiler.
- **`radio_server/backends/uvk5/frames.py`** in
  [radio-server](https://github.com/kbennett2000/radio-server) is a complete, independently-written
  Python implementation of this protocol, with the register-level cookbook (frequency, bandwidth,
  CTCSS, key-up/key-down) in its `radio.py`.
- The BK4819 register sequences for tuning and keying are **not** part of this protocol — they are the
  chip's, reachable through `0x0850`/`0x0851`. See radio-server's ADR 0112 for the derived sequences.
