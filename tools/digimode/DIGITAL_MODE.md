# Digital Mode: Sub-Hz FSK Transmit Engine for UV-K1 / UV-K5 V3

## Overview

This feature adds a **digital mode transmit engine** to the UV-K1/K5V3 firmware, enabling the radio to act as a precision FSK transmitter controlled entirely over UART. A host PC generates the symbol sequence and the firmware handles real-time frequency synthesis with sub-hertz accuracy — no audio path required.

The primary use case is **FT8 and other WSJT-X modes on VHF/UHF**, but the architecture is generic enough for any constant-envelope FSK protocol (JT65, JT9, WSPR, FST4W, etc.).

### Key Capabilities

- **Sub-Hz frequency resolution** via a novel Vernier tuning technique that combines the BK4819 PLL integer step (10 Hz) with crystal oscillator trim (REG_3B)
- **Two transmission modes**: real-time streaming (PC-timed) and autonomous scheduled playback (firmware-timed)
- **NTP-style clock synchronization** between PC and radio for microsecond-accurate symbol timing
- **EMF-resilient protocol** with 5x frequency redundancy and majority voting
- **Heartbeat watchdog** — automatic TX shutdown on link loss (1 second timeout)
- **Build-time feature flag** — `ENABLE_DIGMODE` (opt-in, zero impact when disabled)

### Architecture

```
┌──────────────────────┐         UART / USB-CDC          ┌──────────────────────┐
│                      │  ◄──────────────────────────►   │                      │
│   Host PC            │     digmode UART protocol       │   UV-K1/K5V3 Radio   │
│                      │                                 │                      │
│  - ft8_send_batch.py  │   SYNC_REQ / SYNC_RESP          │  - Clock sync        │
│  - ft8_send_symbols.py│   SCHED_TX / SCHED_APPEND       │  - Vernier tuning    │
│  - Custom app        │   START_TX / STOP_TX / SET_FREQ  │  - TX engine (CW)    │
│                      │   STATUS / NOOP / ACK            │  - LCD display       │
└──────────────────────┘                                 └──────────────────────┘
```

## Prerequisites

### CW Modulation Mode

Digital mode depends on the CW modulation mode (`MODULATION_CW`) which keys an unmodulated carrier via PTT. CW mode is included in the same feature branch and adds:

- A `CW` entry in the modulation selector (FM → AM → USB → **CW**)
- TX: FM hardware with AF muted + 650 Hz sidetone through the speaker
- RX: USB demodulator with a -650 Hz frequency offset

The digital mode uses the CW TX path internally but immediately silences the sidetone and takes over frequency control.

### Hardware

Any UV-K1 or UV-K5 V3 with:
- **UART** (stock programming cable) or **USB-CDC** (AIOC or similar USB audio/serial adapter)
- Default baud rate: **38400** (UART) or device-native (USB-CDC)

## UART Protocol Specification

### Frame Format

All communication uses a simple binary framing shared between PC and radio:

```
┌───────┬───────┬───────┬─────────────────┬───────┐
│ SYNC  │  CMD  │  LEN  │    PAYLOAD      │  CRC  │
│ 0xAB  │  1B   │  1B   │   0..N bytes    │  1B   │
└───────┴───────┴───────┴─────────────────┴───────┘
```

| Field   | Size   | Description |
|---------|--------|-------------|
| SYNC    | 1 byte | Always `0xAB` |
| CMD     | 1 byte | Command ID (`0x01`–`0x0A`) |
| LEN     | 1 byte | Payload length (0–255) |
| PAYLOAD | LEN bytes | Command-specific data (big-endian) |
| CRC     | 1 byte | XOR of all preceding bytes (SYNC through last PAYLOAD byte) |

**Coexistence with stock protocol:** The stock Quansheng UART protocol uses `0xAB 0xCD` as its header. Since valid digital mode CMD bytes are `0x01`–`0x0A` (never `0xCD`), the two protocols coexist on the same UART without collision. The firmware's UART parser checks the second byte to dispatch accordingly.

### CRC Calculation

```python
def xor_crc(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
    return crc

def build_frame(cmd: int, payload: bytes = b'') -> bytes:
    header = bytes([0xAB, cmd, len(payload)])
    body = header + payload
    return body + bytes([xor_crc(body)])
```

### Command Reference

#### `0x01` START_TX (PC → Radio)

Begin transmitting an unmodulated carrier at the specified frequency.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | base_freq | TX frequency in 10 Hz units, big-endian (e.g., `14417400` = 144.174 MHz) |
| 4 | 1 | power | TX power level: `0`–`7` (LOW1–HIGH) or `0xFF` (use current VFO setting) |

- If `LEN < 4`, uses the current VFO TX frequency.
- If `LEN < 5`, power defaults to `0xFF` (current VFO).
- Response: `ACK` with `result = 0x00` (OK).
- Side effects: enters digital mode (switches display, disables squelch, opens USB RX), then activates PA.

**Power levels:**

| Value | Name | Description |
|-------|------|-------------|
| 0 | USER | Current VFO setting |
| 1 | LOW1 | Lowest power |
| 2 | LOW2 | |
| 3 | LOW3 | |
| 4 | LOW4 | |
| 5 | LOW5 | |
| 6 | MID | Medium power |
| 7 | HIGH | Maximum power |
| 0xFF | (default) | Use current VFO setting |

#### `0x02` STOP_TX (PC → Radio)

Immediately stop transmitting. PA is turned off, radio returns to USB RX.

| Payload | None (LEN = 0) |
|---------|-----------------|

- Response: `ACK` with `result = 0x00`.

#### `0x03` SET_FREQ (PC → Radio)

Set the audio-frequency offset for the current transmission. Used in **streaming mode** where the PC controls timing.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 2×5 | freq_dhz[5] | Audio offset in 0.1 Hz units, big-endian, repeated 5 times |
| 10 | 4 | apply_at | Radio-local timestamp (microseconds) when this frequency should take effect |

The frequency value represents the audio offset from the base TX frequency. For example, FT8 at 1500 Hz base with tone 0 would be `15000` (= 1500.0 Hz in 0.1 Hz units).

**5x redundancy and EMF recovery:** The frequency field is transmitted 5 times. On CRC success, the first copy is used. On CRC failure, the firmware performs majority voting across all 5 copies and falls back to the previous frequency if no majority is found. After 10 consecutive CRC failures, TX is automatically stopped.

**Timing:** The `apply_at` field is a radio-local microsecond timestamp obtained via clock synchronization (see SYNC_REQ). The firmware buffers entries in a FIFO (depth: 8) and applies each frequency change at the specified time using microsecond-resolution delays.

**No ACK is sent** for SET_FREQ to minimize UART traffic during transmission.

#### `0x04` STATUS (PC → Radio)

Request the current radio status.

| Payload (request) | None (LEN = 0) |
|--------------------|-----------------|

Response frame (radio → PC):

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | tx_active | `0x01` if transmitting, `0x00` if idle |
| 1 | 2 | cur_freq_dhz | Current audio offset in 0.1 Hz units, big-endian |

#### `0x05` ACK (Radio → PC)

Acknowledgment sent by the radio in response to commands.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | orig_cmd | The command being acknowledged |
| 1 | 5 | result×5 | Result byte repeated 5 times (`0x00` = OK, `0x01` = Error) |

The result is repeated 5 times for the same EMF-resilience reason; the host should use majority voting.

#### `0x06` SYNC_REQ (PC → Radio)

Initiate an NTP-style clock synchronization round.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | pc_time_us | PC-local timestamp in microseconds, big-endian |

- The first SYNC_REQ also enters digital mode (USB RX, squelch disabled, display switch).
- Response: `SYNC_RESP` (see below).

#### `0x07` SYNC_RESP (Radio → PC)

Clock synchronization response.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | pc_time_echo | Echo of the PC timestamp from SYNC_REQ |
| 4 | 4 | radio_time_us | Radio-local timestamp at the moment of response |

**Computing the clock offset (Python):**

```python
t1 = pc_timestamp_at_send
# ... send SYNC_REQ(t1), receive SYNC_RESP ...
t3 = pc_timestamp_at_receive
t2 = radio_time_us_from_response

rtt = t3 - t1
offset = t2 - t1 - rtt // 2    # radio_time ≈ pc_time + offset
```

Run 5–10 rounds and take the median offset for best accuracy. Typical RTT over UART at 38400 baud is 2–5 ms; the resulting clock alignment is well within the timing requirements of FT8 (160 ms symbols).

#### `0x08` NOOP (PC → Radio)

Heartbeat / keep-alive. No payload, no response. Resets the 1-second heartbeat watchdog.

| Payload | None (LEN = 0) |
|---------|-----------------|

The host **must** send NOOP frames at least once per second during streaming mode to prevent automatic TX shutdown. Recommended interval: 80 ms.

#### `0x09` SCHED_TX (PC → Radio)

Upload a complete symbol schedule and (optionally) defer TX start to a precise timestamp. The firmware plays back all symbols autonomously with microsecond timing — no further UART traffic required during transmission.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | base_freq | Base TX/RX frequency in 10 Hz units, big-endian |
| 4 | 4 | interval_us | Microseconds between symbol changes (e.g., `160000` for FT8) |
| 8 | 1 | power | TX power level (same as START_TX) |
| 9 | 4 | start_at | Radio-local timestamp to begin TX (`0` = immediate) |
| 13 | 2×N | freq_dhz[] | Array of audio offsets in 0.1 Hz units, big-endian (**N may be 0**) |

- **RX frequency switch:** Every SCHED_TX sets the VFO RX frequency to `base_freq` and reconfigures the receiver hardware. This takes effect immediately, before any TX begins.
- **Empty list (N = 0):** When no frequency entries are provided (LEN = 13), the command only switches the RX frequency and clears any running TX/schedule — no transmission occurs. This is the recommended way to change the monitoring frequency from the host.
- Maximum symbols per SCHED_TX: limited by LEN field (max 255 bytes → 121 symbols). Use SCHED_APPEND for longer sequences.
- Maximum total schedule buffer: **256 entries**.
- Response: `ACK` with `result = 0x00`.
- If `start_at > 0`, the radio displays a countdown and begins TX at the exact radio-local timestamp.
- If `start_at = 0`, TX begins immediately.
- When all symbols have been played, the firmware automatically stops TX and returns to USB RX at `base_freq`.

#### `0x0A` SCHED_APPEND (PC → Radio)

Append additional symbols to an existing schedule (uploaded via SCHED_TX).

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 2×N | freq_dhz[] | Additional audio offsets in 0.1 Hz units, big-endian |

- Must be sent after SCHED_TX and before playback reaches the end.
- Response: `ACK` with `result = 0x00`.

## Vernier Tuning

The BK4819's PLL has a minimum step of 10 Hz, which is far too coarse for FT8 (6.25 Hz tone spacing). The Vernier technique achieves sub-Hz resolution by combining two tuning mechanisms:

1. **PLL frequency word** (REG_38/39): integer steps of 10 Hz
2. **Crystal oscillator trim** (REG_3B): shifts the reference oscillator, producing a frequency change proportional to the carrier frequency

The relationship is:

```
actual_offset = xtal_trim × alpha - pll_comp × 10000   (in millihertz)

where alpha = 5000 × f_carrier_hz / 26000000    (mHz per REG_3B LSB)
```

- `xtal_trim` — crystal oscillator trim offset written to REG_3B
- `pll_comp` — PLL compensation steps subtracted from REG_38/39

The solver searches `xtal_trim` in [0..600] and picks the best integer `pll_comp` for each, minimizing the residual error. At 144 MHz, `alpha ≈ 27.7 mHz/LSB`, giving a typical residual error under 5 mHz.

## Usage Guide

### Method 1: Scheduled Playback (Recommended for FT8)

This is the simplest and most reliable method. The PC uploads the entire symbol sequence in advance, and the firmware handles all timing internally.

**Step 1: Connect and synchronize clocks**

```python
import serial, struct, time, statistics

SYNC = 0xAB

def xor_crc(data):
    c = 0
    for b in data:
        c ^= b
    return c

def build_frame(cmd, payload=b''):
    hdr = bytes([SYNC, cmd, len(payload)])
    body = hdr + payload
    return body + bytes([xor_crc(body)])

ser = serial.Serial('/dev/ttyUSB0', 38400, timeout=0.1)
time.sleep(0.3)
ser.reset_input_buffer()

# Determine clock offset between PC and radio
offsets = []
t0 = time.monotonic()
for _ in range(7):
    t1 = int((time.monotonic() - t0) * 1_000_000) & 0xFFFFFFFF
    ser.write(build_frame(0x06, struct.pack('>I', t1)))  # SYNC_REQ
    time.sleep(0.1)
    resp = ser.read(64)
    t3 = int((time.monotonic() - t0) * 1_000_000) & 0xFFFFFFFF
    # Parse SYNC_RESP to extract radio timestamp t2
    # offset = t2 - t1 - (t3 - t1) // 2
    # offsets.append(offset)

clock_offset = int(statistics.median(offsets))
```

**Step 2: Build the symbol schedule**

```python
# FT8 example: 79 symbols, 160ms each, 6.25 Hz spacing, 1500 Hz base audio
FT8_SYMBOLS = [3, 1, 4, 0, 6, 5, 2, ...]  # 79 symbols (0-7)
BASE_AUDIO_HZ = 1500.0
TONE_STEP_HZ = 6.25
SYMBOL_US = 160_000
BASE_FREQ_10HZ = 14417400  # 144.174 MHz

# Convert symbols to audio offsets in 0.1 Hz units
freq_list = [round((BASE_AUDIO_HZ + sym * TONE_STEP_HZ) * 10) for sym in FT8_SYMBOLS]

# Compute TX start in radio-local time (next 15-second boundary + 0.5s)
now_wall = time.time()
slot = (now_wall // 15 + 1) * 15 + 0.5
wait_us = int((slot - now_wall) * 1_000_000)
pc_start = int((time.monotonic() - t0) * 1_000_000) + wait_us
radio_start = (pc_start + clock_offset) & 0xFFFFFFFF
```

**Step 3: Send SCHED_TX**

```python
payload  = struct.pack('>I', BASE_FREQ_10HZ)   # base frequency
payload += struct.pack('>I', SYMBOL_US)          # interval (160000 us)
payload += struct.pack('B', 0xFF)                # power (use VFO setting)
payload += struct.pack('>I', radio_start)        # deferred start timestamp
for f in freq_list:
    payload += struct.pack('>H', f)

ser.write(build_frame(0x09, payload))            # SCHED_TX
```

The radio will display a countdown and begin transmitting at the precise time. When all 79 symbols are played, it automatically returns to RX.

### Method 2: Real-Time Streaming

For protocols where the symbol sequence is not known in advance, or for continuous transmission, use START_TX + SET_FREQ streaming.

```python
# Start TX
ser.write(build_frame(0x01, struct.pack('>I', BASE_FREQ_10HZ) + bytes([0xFF])))
time.sleep(0.1)

# Stream frequency changes with timestamps
for i, symbol in enumerate(symbols):
    freq_dhz = round((BASE_AUDIO_HZ + symbol * TONE_STEP_HZ) * 10)
    apply_at = radio_start + i * SYMBOL_US
    
    # 5x redundancy
    payload = struct.pack('>H', freq_dhz) * 5 + struct.pack('>I', apply_at)
    ser.write(build_frame(0x03, payload))
    
    # Send heartbeat NOOPs between symbols
    ser.write(build_frame(0x08))
    time.sleep(0.08)

# Stop TX
ser.write(build_frame(0x02))
```

**Important:** In streaming mode, you must send NOOP heartbeats at least once per second (recommended every 80 ms) to prevent the watchdog from shutting down TX.

### Method 3: Large Schedules with SCHED_APPEND

For sequences longer than ~121 symbols (limited by the 255-byte LEN field), split the upload:

```python
# First batch: send SCHED_TX with the first N symbols
ser.write(build_frame(0x09, sched_payload_first_batch))

# Append remaining symbols
for chunk in remaining_chunks:
    payload = b''
    for f in chunk:
        payload += struct.pack('>H', f)
    ser.write(build_frame(0x0A, payload))  # SCHED_APPEND
```

The firmware's schedule buffer holds up to **256 entries** total.

## Reference Implementations

Two complete working examples are provided in `tools/digimode/`:

### `ft8_send_batch.py` — Scheduled Playback (Recommended)

Uploads the entire 79-symbol FT8 sequence to the radio in a single `SCHED_TX` command. The firmware autonomously plays back all symbols with microsecond-accurate timing — no further UART traffic is needed during transmission.

Features:
- AIOC serial port auto-detection
- NTP-style clock synchronization (7 rounds, median filter)
- SCHED_TX with deferred start aligned to the next FT8 15-second slot
- ACK parsing with retry logic (5× redundancy)
- Real-time countdown and progress display

```bash
pip install pyserial

# Auto-detect AIOC, use current VFO power
python ft8_send_batch.py

# Specify port and power level
python ft8_send_batch.py --port /dev/ttyUSB0 --power LOW5

# Adjust TX start offset within the 15s slot
python ft8_send_batch.py --offset 0.3
```

### `ft8_send_symbols.py` — Real-Time Streaming

Streams individual `SET_FREQ` commands for each symbol with PC-side timing. The PC controls when each frequency change occurs via timestamped frames. Requires continuous NOOP heartbeats to keep the watchdog alive.

This mode is useful when:
- The symbol sequence is not known in advance (e.g., adaptive protocols)
- You need to modify the sequence mid-transmission
- You are building a WSJT-X bridge that forwards symbols in real time

```bash
pip install pyserial

# Auto-detect AIOC, use current VFO power
python ft8_send_symbols.py

# Specify port and power level
python ft8_send_symbols.py --port /dev/ttyUSB0 --power MID
```

**Important:** In streaming mode, the host must send NOOP heartbeats at least once per second (recommended every 80 ms). The script handles this automatically via `heartbeat_sleep()`.

## Build Configuration

Digital mode is controlled by the `ENABLE_DIGMODE` CMake option:

```cmake
# In CMakePresets.json
"ENABLE_DIGMODE": true

# Or via command line
cmake --preset Custom -DENABLE_DIGMODE=ON
```

When disabled, all digital mode code is compiled out with zero overhead.

### Files Added

| File | Description |
|------|-------------|
| `App/app/digmode.c` | UART protocol handler, TX engine, FIFO scheduler |
| `App/app/digmode.h` | Protocol constants, public API |
| `App/dsp/vernier.c` | Vernier frequency solver |
| `App/dsp/vernier.h` | Vernier API and types |
| `App/ui/digmode.c` | Dedicated LCD display page |
| `App/ui/digmode.h` | Display function prototype |
| `tools/digimode/ft8_send_batch.py` | Reference: scheduled playback via SCHED_TX |
| `tools/digimode/ft8_send_symbols.py` | Reference: real-time streaming via SET_FREQ |
| `tools/digimode/DIGITAL_MODE.md` | This documentation |

### Files Modified

| File | Change |
|------|--------|
| `App/radio.h` | Add `MODULATION_CW` to enum |
| `App/radio.c` | CW modulation support (TX, RX, end-of-transmission) |
| `App/functions.c` | CW TX path in `FUNCTION_Transmit()` |
| `App/scheduler.c` | Add `SCHEDULER_GetMicros()` for microsecond timestamps |
| `App/scheduler.h` | Declare `SCHEDULER_GetMicros()` |
| `App/CMakeLists.txt` | Add `ENABLE_DIGMODE` feature flag and source files |
| `App/ui/ui.h` | Add `DISPLAY_DIGMODE` to display enum |
| `App/ui/ui.c` | Register `UI_DisplayDigmode` function |
| `App/app/app.c` | Integrate digmode (key handler, poll, TX guards) |
| `App/app/uart.c` | Dispatch digmode frames from UART parser |
| `CMakePresets.json` | Enable `ENABLE_DIGMODE` in default preset |

## Safety Features

- **Heartbeat watchdog:** TX automatically stops after 1 second without any valid UART frame (streaming mode only; disabled during scheduled playback which has a defined endpoint).
- **CRC protection:** All commands (except SET_FREQ which uses redundancy) require valid CRC. Invalid frames receive `ACK(ERR)`.
- **TX timeout bypass:** Digital mode disables the firmware's built-in TX timer since transmission length is controlled by the host or schedule.
- **PTT lockout:** Physical PTT is ignored while digital mode is active to prevent accidental interference.
- **Consecutive CRC failure limit:** 10 consecutive CRC failures on SET_FREQ triggers automatic TX stop.
- **Automatic RX on completion:** Scheduled playback returns to USB RX when all symbols are played.

## License

All new code is licensed under the Apache License 2.0, consistent with the upstream project.
