# Quansheng UV-K1 / UV-K5 V3 — CAT Edition (F4HWN 6.0.0 Base)

> [!WARNING]
> **IMPORTANT WARNING / DISCLAIMER**  
> **Install and use this firmware entirely at your own risk!**  
> Firmware modifications carry the inherent risk of device malfunction or bricking. The authors and contributors assume no responsibility or liability for any hardware damage, data loss, or violation of local radio communications laws and regulations. Before flashing, it is strongly recommended to back up your radio's calibration data (e.g. using UV Studio).

> [!IMPORTANT]
> **Installation & Flashing:**  
> To install this firmware, download the **`f4hwn.cat.bin`** file (located in the main folder of this repository) and flash it to your radio using **[UV Studio](https://armel.github.io/uvstudio/#dump-calib)**. Remember to back up your calibration data first via the [Dump Calibration tool](https://armel.github.io/uvstudio/#dump-calib) before flashing!

---

## Project Overview

This project is based on the official **[F4HWN custom firmware](https://github.com/armel/uv-k1-k5v3-firmware-custom)** (v6.0.0 for the **PY32F071** microcontroller).

It is extended with a dedicated **CAT** variant (`f4hwn.cat` / preset `CAT`), which introduces:
- **Extended Kenwood CAT Protocol** for full remote transceiver control from a computer (UART / USB VCP).
- **Asynchronous hardware scanner** and background RSSI measurements (BK4819) without blocking the radio user interface.
- **Default settings optimized for PC/CAT operation (with non-destructive EEPROM preservation)**:
  - **Always boots in Frequency (VFO) Mode** instead of memory channel (MR) mode.
  - **Single VFO mode** (`DUAL_WATCH = DUAL_WATCH_OFF`) — prevents the radio from switching bands in the background during remote control.
  - **Battery Saver disabled** (`BATTERY_SAVE = 0`) — the receiver does not enter cyclic sleep mode, ensuring instantaneous response to RF signals and CAT commands.
  - **Non-Destructive EEPROM**: These settings are applied dynamically in RAM for CAT operation and **never overwrite** the user's permanent EEPROM configuration. When switching back to stock or another firmware (e.g. via multiboot), your original user settings (Battery Saver, Dual Watch, MR channel mode) remain completely intact.
  - Standard UART baud rate: **38400 baud** (8N1).
- **Instantaneous Squelch & Carrier Reporting (`BY1;` / `BY0;`)**:
  - Edge-triggered, real-time CAT notifications transmitted over UART and USB VCP immediately upon squelch opening (`BY1;` on carrier detect) and closing (`BY0;` on squelch tail).
  - Standard Kenwood `BY;` query command to poll current squelch / busy status at any time (`BY1;` = busy/open, `BY0;` = quiet/closed).
  - Essential for digital modes, soundmodems, APRS, SDR controllers, and remote gateways.
- **Built-in BK4819 FSK Modem & Digital Messaging (`FE`, `FC`, `FM`, `FTA`, `FTX`, `FPA`, `FPX`)**:
  - Full bidirectional packet data communications using the radio's native BK4819 FSK engine at 1200 or 2400 baud with configurable sync words, packet lengths from 8 to 100 bytes, and CCITT CRC16 validation.
  - Send text messages (`FTA`) or raw hexadecimal binary frames (`FTX`).
  - Asynchronous broadcast of incoming packets (`FPA` text / `FPX` binary) with signal RSSI (dBm) over both UART and USB VCP.
  - Auto-mute mode (`FE2`) to silence packet sound on the speaker during reception.
  - Busy channel lockout policy (`FM1`) to prevent packet transmission over active voice channels.
  - Real-time message display on LCD Line 4 without interfering with the S-meter bar.
- **Enhanced LCD UI for Data & Telemetry**:
  - **Smooth S-meter**: Real-time signal strength bar refreshed at ~80 ms during reception and instantly cleared upon squelch close.
  - **Simultaneous FTA & S-meter display**: Received FSK text packets (`FTA`) and DTMF tones are displayed on Line 4 directly above the S-meter bar on Line 5, allowing full visibility of both without visual collision.
- **Safe Transmit Heartbeat Watchdog (`TXS;` / `TS;`) — Essential Hardware & PA Protection**:
  - **Thermal & Hardware Protection**: Handheld radios lack active cooling fans or heavy heatsinks. Traditional latching `TX;` leaves the transmitter permanently keyed if the controlling software freezes, crashes, or the USB connection drops — risking severe thermal overload, PA burnout, battery depletion, and frequency jamming.
  - **Automatic Fail-Safe Timer**: `TXS;` (and shorthand alias `TS;`) turns on transmission **with an active hardware watchdog** (default 1000 ms, customizable from 50 ms to 30,000 ms).
  - **Periodic Keep-Alive**: Host software transmits periodic heartbeats (e.g. every 200–500 ms) to keep the transmitter keyed. If heartbeats stop for any reason, the radio automatically and cleanly shuts down the RF Power Amplifier within the timeout window.
  - **Multi-stage Hardware Cutoff**: Immediately pulls PA enable LOW, cuts PA bias/gain to zero, extinguishes the red TX LED, reconfigures the BK4819 front-end, and returns to RX.
  - Can still be cancelled prematurely at any instant using standard `RX;`.
- **Fast Telemetry Dump (`RA;` / `QS;`)**: Single-query dump of all radio parameters, frequency, modulation, power, CTCSS/DCS, battery status, and RSSI with minimal CPU overhead.
- **Interactive Self-Documentation (`HELP;` and `HELPJ;`)**: Built-in human-readable and JSON machine-readable commands schema listing all supported commands, syntax, and parameter ranges.
- **Bidirectional Parameter Queries**: All configuration commands (`FA`, `FB`, `FR`, `MD`, `PC`, `SQ`, `OS`, `OV`, `RD`, `FE`, `FC`, `FM`) support querying the current state by omitting arguments.
- **Hardware Safety & PA Protection**: TX frequency validation against band limits (`TX_freq_check` / `TX_LOCK`), clean PA and red TX LED shutdown on transmit release, and resilient serial command buffers.
- **Multiboot support**: 100% native support for the **multiboot** mechanism is preserved (UART commands `0x0720`–`0x0728` and flash partitioning). This enables safe and convenient firmware changes, rolling back to previous releases, or flashing multiple firmware versions.

---

## CAT Commands Quick Reference

All commands are transmitted as ASCII text strings over the serial port and must be terminated with a semicolon (`;`).

| Command | Example / Format | Response | Description |
|---|---|---|---|
| **FA** | `FA;`<br>`FA00145000000;` | `FA[11 digits Hz];`<br>*(none)* | **VFO A Frequency**: read (without argument) or set frequency (11 digits in Hz, e.g. `00145000000;` = 145.000 MHz). |
| **FB** | `FB;`<br>`FB00433000000;` | `FB[11 digits Hz];`<br>*(none)* | **VFO B Frequency**: read or set VFO B frequency (11 digits in Hz). |
| **FR** | `FR;`<br>`FR0;`<br>`FR1;` | `FR[0-1];`<br>*(none)* | **Active VFO Selection & Query**: `FR;` returns active VFO (`FR0;` for VFO A, `FR1;` for VFO B). `FR0;` switches to VFO A, `FR1;` switches to VFO B. |
| **TX** | `TX;` | *(none)* | **Force Transmit (PTT ON)**: switches the transceiver to transmit mode (latching, permanent until `RX;`). |
| **TXS** / **TS** | `TXS;`<br>`TS;`<br>`TXS[ms];` / `TS[ms];` | *(none)* | **Safe Transmit (Heartbeat Watchdog)**: turns on TX with an automatic 1-second timeout (default 1000 ms, or custom `50`–`30000` ms). Host must continuously send `TXS;` / `TS;` (e.g. every 300–500 ms) to keep transmitting. If host crashes or USB disconnects, radio automatically drops back to RX within timeout and shuts down PA and red LED. Can be stopped at any time with `RX;`. |
| **RX** | `RX;` | *(none)* | **Return to Receive (PTT OFF)**: stops transmitting and returns to receive mode (cancels both `TX;` and `TXS;`, shuts off PA and red LED). |
| **MO** | `MO;`<br>`MO1;`<br>`MO0;` | `MO[0-1];`<br>*(none)* | **Monitor (Open Squelch) & Query**: `MO;` returns current monitor status (`MO0;` or `MO1;`). `MO1;` opens squelch, `MO0;` restores normal squelch operation. |
| **MD** | `MD;`<br>`MD4;` / `MD5;` / `MD2;` | `MD[mode];`<br>*(none)* | **Modulation Mode**: `4` = FM, `5` = AM, `2` = USB. Without parameter, returns active VFO's current modulation. |
| **PC** | `PC;`<br>`PC[0-7];` e.g. `PC6;` | `PC[0-7];`<br>*(none)* | **Transmitter Output Power & Query**: `PC;` returns current power level (`PC0;`..`PC7;`). `PC[0-7];` sets power: `0`=20mW (Low1), `1`=100mW (Low2), `2`=500mW (Low3), `3`=1W (Low4), `4`=2W (Low5), `5`=3W (Mid), `6`=5W (High), `7`=User. |
| **OF** | `OF;` | *(none)* | **Disable Subtones**: turns off CTCSS/DCS on active VFO. |
| **CT** | `CT0885;`<br>`CT0670;` | *(none)* | **Set CTCSS Tone**: 4 digits in tenths of Hz (e.g. `0885` = 88.5 Hz, `0670` = 67.0 Hz). |
| **DT** | `DT023;`<br>`DT047;` | *(none)* | **Set DCS Code**: 3 digits in octal notation (e.g. `023`, `047`). |
| **SQ** | `SQ;`<br>`SQ[0-9];` e.g. `SQ5;` | `SQ[0-9];`<br>*(none)* | **Squelch Level & Query**: `SQ;` returns current squelch level (`SQ0;`..`SQ9;`). `SQ[0-9];` sets squelch from `0` (open) to `9`. |
| **BY** | `BY;` | `BY[0-1];`<br>Asynchronously:<br>`BY1;` (on carrier detect)<br>`BY0;` (on squelch tail) | **Receiver Busy / Squelch Status**: `BY;` returns current squelch state (`BY1;` if squelch open, `BY0;` if closed). Radio automatically transmits `BY1;` immediately when squelch opens, and `BY0;` immediately when squelch closes over both UART and USB VCP. |
| **OS** | `OS;`<br>`OS0;` / `OS1;` / `OS2;` | `OS[0-2];`<br>*(none)* | **Shift Direction (Offset) & Query**: `OS;` returns current shift direction (`OS0;`..`OS2;`). `0` = none (simplex), `1` = positive offset (+), `2` = negative offset (-). |
| **OV** | `OV;`<br>`OV00000600000;` | `OV[11 digits Hz];`<br>*(none)* | **Frequency Offset Value & Query**: `OV;` returns current offset frequency in Hz. `OV[f11];` sets offset (11 digits in Hz, e.g. `00000600000;` = 600 kHz). |
| **IF** | `IF;` | `IF[freq11]00000[mod][tx];` | **Transceiver Status**: Kenwood TS-2000 standard format returning active frequency, modulation, and TX/RX status. |
| **RA** / **QS** | `RA;`<br>`QS;` | `RA,[rx11],[tx11],[shift],[off11],[mod],[pwr],[bw],[sq],[busy],[tx_t],[tx_v],[rx_t],[rx_v],[tx],[sq_o],[bat_mv],[bat_%],[rssi],[fsk_m],[fsk_b],[fsk_l];` | **Dump All Radio Settings (Ultra-low CPU overhead)**: returns all parameters in a single CSV line: RX freq (11d Hz), TX freq (11d Hz), shift dir (0/1/2), offset (11d Hz), mod (4=FM, 5=AM, 2=USB), power (0..7), bandwidth (0=Wide, 1=Narrow), squelch (0..9), busy lock (0/1), TX tone type (0=None, 1=CTCSS, 2=DCS-N, 3=DCS-I) and value, RX tone type and value, TX state (0/1), squelch open (0/1), battery mV (e.g. 7800), battery % (0..100), RSSI dBm (e.g. -105), FSK mode (0..2), FSK baud (1200/2400), and FSK length. `QS;` is a lightweight alias. |
| **S1** | `S1;` | `S1,[vfo],[dbm],[sq];` | **Instant RSSI Measurement**: immediate signal strength (dBm) and squelch status (0/1) for active VFO. |
| **SM** | `SM;` / `SM0;`<br>`SM[freq11];` | `SM[freq11],[dbm],[sq];` | **Fast Spot Frequency Measurement**: measures signal level on specified frequency with proper RF bandpass filtering, without permanently altering VFO settings. Without argument, measures active VFO. |
| **RD** | `RD;`<br>`RD1;`<br>`RD0;` | `RD[0-1];`<br>Asynchronously:<br>`RR[vfo],[dbm];` | **Auto RSSI Reporting & Query**: `RD;` returns current reporting state (`RD0;` or `RD1;`). `RD1;` enables periodic `RR` packets every ~200 ms (or upon signal level change), `RD0;` disables. Broadcasts over both UART and USB VCP. |
| **SL** | `SL[idx2][freq11];`<br>e.g. `SL0000145000000;` | `SL_OK;` | **Scanner List Definition**: writes frequency into scanner list cell (indices `00` to `24`, up to 25 channels). |
| **SC** | `SC[count2];` e.g. `SC03;` | Asynchronously:<br>`SR,[dbm],[sq],...;` | **Start Hardware Scanner**: asynchronously measures defined channels and returns complete results vector over both UART and USB VCP. |
| **SCF** | `SCF[freq11][,ticks];` | Asynchronously:<br>`SQ[freq],[sq],[dbm],[noise],[glitch];` | **Single Hardware Measurement (SCF)**: detailed measurement of signal strength, noise, and glitches directly from BK4819. |
| **DTMF** | *(automatic)* | Asynchronously:<br>`RD[char],[dbm];` | **DTMF Tone Reporting**: when a DTMF character is received, the radio automatically sends an `RD` packet with the decoded character and RSSI level. |
| **FE** | `FE;`<br>`FE0;` / `FE1;` / `FE2;` | `FE[0-2];`<br>`FE_OK;` / `FE_ERR;` | **FSK Modem Enable / Mode**: `FE;` queries active mode. `FE0;` = disabled, `FE1;` = enabled (audible RX), `FE2;` = enabled with auto-mute (mutes audio while packet is received). Confirms with `FE_OK;`. |
| **FC** | `FC;`<br>`FC[baud],[sync4],[len];`<br>e.g. `FC2400,ABCD,32;` | `FC[baud],[sync4],[len];`<br>`FC_OK;` / `FC_ERR;` | **FSK Modem Configuration**: `FC;` queries configuration. Set baudrate (`1200` or `2400`), 4-hex sync word (e.g. `ABCD`), and packet length (`8` to `100`, default `32`, must be even). Confirms with `FC_OK;`. |
| **FM** | `FM;`<br>`FM0;` / `FM1;` | `FM[0-1];`<br>`FM_OK;` / `FM_ERR;` | **FSK Channel Busy Lockout**: `FM;` queries policy. `FM0;` = always transmit, `FM1;` = busy channel lockout (prevents transmission if squelch is open). Confirms with `FM_OK;`. |
| **FTA** | `FTA[text];`<br>e.g. `FTAHello World;` | `FT_OK;`<br>`FT_ERR;`<br>`FT_BUSY;` | **FSK Transmit ASCII Text**: transmits text message as an FSK data packet with preamble, sync word, length, and CCITT CRC16. Returns `FT_OK;` on success, `FT_BUSY;` if channel or transmitter is busy, or `FT_ERR;` on invalid length. |
| **FTX** | `FTX[hex_pairs];`<br>e.g. `FTX01020304;` | `FT_OK;`<br>`FT_ERR;`<br>`FT_BUSY;` | **FSK Transmit Binary / Hex**: transmits raw binary payload encoded as hexadecimal string. Returns `FT_OK;` on success, `FT_BUSY;` if busy, or `FT_ERR;` on invalid hex. |
| **FPA** / **FPX** | *(automatic)* | Asynchronously:<br>`FPA[text],[dbm];`<br>`FPX[hex],[dbm];` | **FSK Packet Received**: when an incoming FSK packet is decoded, the radio broadcasts `FPA` (ASCII text) or `FPX` (raw binary hex) with measured signal RSSI (dBm) over both UART and USB VCP. Text packets are also displayed directly on Line 4 of the LCD screen. |
| **ID** | `ID;` | `ID020;` | **Transceiver Identification**: Kenwood TS-2000 standard identification query. Required by Hamlib (`rigctl -m 2014`), WSJT-X, flrig, Chirp, etc. |
| **AI** | `AI;`<br>`AI0;` | `AI0;` | **Auto Information**: Kenwood standard auto-info query/set. |
| **VR** | `VR;` | `VR6.0.0;` | **Firmware Version**: returns current firmware version. |
| **HELP** | `HELP;` | Multi-line text ended with `;\r\n` | **Human-readable Help**: outputs a formatted, human-readable list of all CAT commands, aliases, syntax, and parameter ranges. |
| **HELPJ** | `HELPJ;` | `{"commands":[...]};\r\n` | **JSON Commands Schema**: dumps JSON list of all supported CAT commands and aliases (`commands` key). |

---

## Safe Transmit (TXS / TS) & Power Amplifier (PA) Protection

> [!CAUTION]
> **Why Safe Transmit (`TXS;` / `TS;`) is Vital for Handheld Transceivers:**  
> Unlike 100 W desktop base station rigs equipped with extruded aluminum heatsinks and cooling fans, the **Quansheng UV-K1 / UV-K5 V3** is an ultra-compact handheld radio. Its RF Power Amplifier (PA) transistor and internal PCB thermal dissipation are engineered for typical portable duty cycles (e.g. 5% TX, 5% RX, 90% standby).
>
> Using traditional unmonitored `TX;` (PTT ON) over serial relies completely on the host software remembering to send `RX;`. If the host application encounters an unhandled exception, crashes, freezes during an OS thread lock, hits a breakpoint, or if the USB cable is accidentally unplugged, **the radio remains keyed continuously**.
>
> Continuous unmitigated transmission leads to:
> 1. **Rapid thermal runaway** and permanent degradation or destruction of the RF PA MOSFET.
> 2. **Complete battery drain** within minutes.
> 3. **Unintended transmission / continuous jamming (QRM)** on local repeaters or simplex frequencies.
>
> **The `TXS;` / `TS;` command family solves this problem entirely at the firmware level.**

### Heartbeat Watchdog Architecture

Safe Transmit operates on a strict **heartbeat-watchdog principle**:
- **Default 1-Second Failsafe**: Sending `TXS;` or shorthand `TS;` activates transmission with an automatic **1000 ms (1.0 s)** countdown watchdog.
- **Customizable Timeout**: The timeout can be configured anywhere between **50 ms** and **30,000 ms (30 s)** by appending the duration in milliseconds:
  - `TXS500;` / `TS500;` — 500 ms watchdog (ideal for high-speed packet radio and soundmodems).
  - `TXS1500;` / `TS1500;` — 1.5 s watchdog.
  - `TXS5000;` / `TS5000;` — 5 s watchdog (suitable for long digital frames or voice streaming).
- **Periodic Heartbeats**: To keep the transmitter active, the host software simply resends `TXS;` or `TS;` at periodic intervals (e.g., every 250–500 ms for a 1000 ms timeout).
- **Instant Watchdog Reload**: Each received `TXS;` / `TS;` frame immediately resets the countdown timer back to full duration.

### Multi-Stage Hardware Failsafe Sequence

The internal watchdog countdown is driven directly by the radio's high-precision 10 ms firmware tick loop (`g_TxSafeTimeout_10ms`). If no heartbeat arrives before the timer reaches zero, the firmware executes a complete, non-blocking hardware protection shutdown:

```
[ Host Heartbeats Stop ] ──> [ Watchdog Ticks to 0 ]
                                      │
                                      ▼
                        ┌───────────────────────────┐
                        │ 1. Cut PA Bias & Drive = 0│
                        │ 2. Pull PA_ENABLE LOW     │
                        │ 3. Extinguish Red TX LED  │
                        │ 4. Reconfigure RX FrontEnd│
                        │ 5. Turn On BK4819 RX      │
                        │ 6. Update LCD Screen      │
                        └───────────────────────────┘
```

1. **PA Drive Shutdown**: Immediately writes `0, 0` to BK4819 power amplifier control registers (`BK4819_SetupPowerAmplifier(0, 0)`), dropping RF drive to zero.
2. **Physical Hardware Pin Disable**: Pulls the physical PA power enable line low (`BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false)`).
3. **Indicator LED Extinguish**: Turns off the bright red front-panel TX LED (`BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false)`).
4. **RF Front-End Reconfiguration**: Re-applies active VFO receive registers and AGC settings (`RADIO_SetupRegisters(true)`).
5. **Receiver Reactivation**: Turns the BK4819 receiver back on (`BK4819_RX_TurnOn()`).
6. **Display State Refresh**: Transitions the radio back into Foreground state and refreshes the LCD.

### Immediate Manual Release (`RX;`)

The host does **not** have to wait for the watchdog timer to expire when a transmission completes normally. Sending standard `RX;` at any time instantly cancels transmission, resets the watchdog timer (`g_TxSafeTimeout_10ms = 0`), and executes the clean hardware shutdown without delay.

### Frequency Lockout & Out-of-Band Safeguards

Before keying the transmitter via `TXS;`, `TS;`, or `TX;`, the firmware checks the target VFO frequency against the hardware band limits and TX lockout configuration (`TX_freq_check()` and `TX_LOCK`). If transmission is prohibited on that frequency, the command is safely rejected and transmission is blocked.

### Comparison: Raw `TX;` vs Safe `TXS;` / `TS;`

| Feature | Standard CAT `TX;` | Safe Watchdog `TXS;` / `TS;` |
|---|---|---|
| **Transmission Mode** | Latching (Permanent ON until `RX;`) | Heartbeat-driven Watchdog (Auto-Off) |
| **Default Failsafe Timeout** | **None** (Transmits forever) | **1000 ms (1.0 second)** |
| **Timeout Range** | N/A | **50 ms to 30,000 ms** (configurable) |
| **PC Software Crash / Freeze** | ❌ **Stuck in TX** until battery dies or radio burns out | ✅ **Auto-reverts to RX within $\le 1$ s** |
| **USB Cable Disconnected** | ❌ **Stuck in TX** permanently | ✅ **Auto-reverts to RX within $\le 1$ s** |
| **Serial Driver / Thread Lock** | ❌ **Stuck in TX** permanently | ✅ **Auto-reverts to RX within $\le 1$ s** |
| **RF Power Amplifier Safety** | ❌ Severe danger of thermal destruction | ✅ **100% PA thermal protection** |
| **Early / Normal Release** | Send `RX;` | Send `RX;` (instantly disarms timer) |
| **Host Application Obligation**| Must remember to send `RX;` | Sends periodic heartbeats (e.g. every 300 ms) |

### Developer Integration Guide

Integrating Safe Transmit into your Python, C++, or C# software is simple. Below is a production-grade Python implementation:

```python
import serial
import time

def transmit_with_watchdog(ser: serial.Serial, duration_seconds: float, watchdog_ms: int = 1000):
    """
    Safely transmits for `duration_seconds` using periodic heartbeats.
    Guarantees clean shutdown via try...finally block.
    """
    heartbeat_interval = (watchdog_ms / 1000.0) * 0.4  # Re-arm at 40% of watchdog window (e.g. every 400ms)
    start_time = time.time()
    
    try:
        while (time.time() - start_time) < duration_seconds:
            # Send heartbeat (TXS or shorthand TS):
            ser.write(f"TS{watchdog_ms};".encode("ascii"))
            time.sleep(heartbeat_interval)
    finally:
        # Immediate clean release upon completion, error, or KeyboardInterrupt:
        ser.write(b"RX;")
```

---

### Testing and Diagnostics
A comprehensive Python script is provided to test and demonstrate all CAT commands:
```powershell
# Automatically detect connected radio COM port and run all test suites:
python tools\cat_tester.py

# Specify COM port and baud rate (default 38400):
python tools\cat_tester.py COM16 38400

# Run with verbose frame-by-frame debug log:
python tools\cat_tester.py COM16 --verbose

# Listen for real-time squelch open/close notifications (BY1/BY0):
python tools\cat_tester.py COM16 --listen-squelch

# Send FSK ASCII text message (FTA):
python tools\cat_tester.py COM16 --send-fsk "Hello UV-K5!"

# Send raw FSK binary payload in hexadecimal (FTX):
python tools\cat_tester.py COM16 --send-fsk-hex 01020304

# Listen for incoming FSK packets (FPA/FPX):
python tools\cat_tester.py COM16 --listen-fsk

# Listen for incoming FSK packets with Auto-Mute enabled (FE2):
python tools\cat_tester.py COM16 --listen-fsk --auto-mute
```

#### Dual-Radio FSK Latency & Round-Trip (RTT) Tester
To test bidirectional communication between two UV-K1 / UV-K5 radios, measure one-way latency, round-trip time (ping-pong), and check RSSI at the lowest TX power (~20 mW) on 433.960 MHz:
```powershell
# Automatically detect two connected USB radios (tests both 1200 and 2400 baud, 32-byte frames):
python tools\fsk_rtt_tester.py

# Specify COM ports explicitly:
python tools\fsk_rtt_tester.py COM16 COM5

# Test ultra-fast short 8-byte frames:
python tools\fsk_rtt_tester.py COM16 COM5 --packet-len 8

# Test specifically 2400 baud with 16-byte frames:
python tools\fsk_rtt_tester.py COM16 COM5 --fsk-baud 2400 --packet-len 16

# Run 10 iterations with Auto-Mute enabled:
python tools\fsk_rtt_tester.py COM16 COM5 --count 10 --auto-mute
```

---

# Stats

![Alt](https://repobeats.axiom.co/api/embed/ecdd86aa536b716f088339a0c5ee734558f78c28.svg "Repobeats analytics image")

# F4HWN firmware port for the UV-K1 and UV-K5 V3 using the PY32F071 MCU

This repository is a fork of the [F4HWN custom firmware](https://github.com/armel/uv-k5-firmware-custom), who was a fork of [Egzumer custom firmware](https://github.com/egzumer/uv-k5-firmware-custom). It extends the work done for the UV-K5 V1, based on the DP32G030 MCU, and adapts it to the newer UV-K1 and UV-K5 V3 built around the PY32F071 MCU. It is the result of the joint work of [@muzkr](https://github.com/muzkr) and [@armel](https://github.com/armel).

A big thanks to DualTachyon, who paved the way by releasing the very first open-source [firmware](https://github.com/DualTachyon/uv-k5-firmware) for the UV-K5 V1. None of this would have been possible without that initial work !

# A note for developers who intend to fork this project

This firmware is distributed under the Apache 2.0 License, carrying forward the original copyright of DualTachyon, whose work laid the foundation for the UV-K5 open-source ecosystem.
If you create a fork or a derived version, **we strongly encourage you to keep your work open source**.

Keeping your fork open:

- aligns with the intent and spirit of the Apache 2.0 License
- supports the amateur-radio and embedded-development community
- avoids unnecessary fragmentation
- allows others to study, audit and improve the firmware

It is also very much in line with the **ham spirit**: sharing knowledge, experimenting together and helping each other, rather than closing things off or claiming them as your own.

Maintaining an open-source fork is the best way to help build a healthy and sustainable ecosystem for everyone.

> [!WARNING]
> THIS FIRMWARE HAS NO REAL BRAIN. PLEASE USE YOUR OWN. Use this firmware entirely at your own risk. There is absolutely no guarantee that it will work in any way, shape, or form on your radio(s); it may even brick your radio(s), in which case you would need to buy another radio. Anyway, have fun.

> [!NOTE]
> Regarding CHIRP: as with many custom firmwares, you need to use a dedicated driver. The matching CHIRP driver is bundled with each release of this repository, so you can download the firmware and its driver together from the [Releases page](https://github.com/armel/uv-k1-k5v3-firmware-custom/releases).

> [!CAUTION]
> Backing up your calibration data with [UV Studio](https://armel.github.io/uvstudio/#dump-calib) immediately after flashing this firmware is strongly recommended. It is a critical best practice before experimenting with any custom build.

# Donations

Special thanks to Jean-Cyrille F6IWW (3 times), Fabrice 14RC123, David F4BPP, Olivier 14RC206, Frédéric F4ESO, Stéphane F5LGW (2 times), Jorge Ornelas (4 times), Laurent F4AXK, Christophe Morel, Clayton W0LED, Pierre Antoine F6FWB, Jean-Claude 14FRS3306, Thierry F4GVO, Eric F1NOU, PricelessToolkit, Ady M6NYJ, Tom McGovern (4 times), Joseph Roth, Pierre-Yves Colin, Frank DJ7FG, Marcel Testaz, Brian Frobisher, Yannick F4JFO, Paolo Bussola, Dirk DL8DF, Levente Szőke (2 times), Bernard-Michel Herrera, Jérôme Saintespes, Paul Davies, RS (3 times), Johan F4WAT, Robert Wörle, Rafael Sundorf, Paul Harker, Peter Fintl, Pascal F4ICR (2 times), Mike DL2MF (3 times), Eric KI1C / F4WFS (3 times), Phil G0ELM, Jérôme Lambert, Eliot Vedel, Alfonso EA7KDF, Jean-François F1EVM, Robert DC1RDB (2 times), Ian KE2CHJ, Daryl VK3AWA, Roberto Brunelli, Robert Boardman, Stephen Oliver, Nicolas F4INE, William Bruno, Daniel OK2VLK, Tayler Chew, Peter DL7RFP, Philippe Kopp, Rune LA6YMA, Jeremy Luna, Steef Wagenaar (2 times), Zhuo BG7SGA, Jamie M0JLB, Antoine LIBERT, Vince K0DKR, Julia DF7JA, Ken 2E0UMK, Victor TI2SYS, Tobi DG9LAY, Deaglan K4DFQ, Catherine PALMER, Brian WA6JFK, Stéphane Hintzy, Roger F1HCN, Marcin Kusaj, Flavio Cottarelli, Bob N1MLZ, Carlos EA1IJ, Brian M7YLF, Giuseppe IT9LLH, 邓 月 and Jon M1JRH for their [donations](https://www.paypal.com/paypalme/F4HWN). That’s so kind of them. Thanks so much 🙏🏻

## Table of Contents

* [Project Overview](#project-overview)
* [CAT Commands Quick Reference](#cat-commands-quick-reference)
* [Safe Transmit (TXS / TS) & PA Protection](#safe-transmit-txs--ts--power-amplifier-pa-protection)
* [Testing and Diagnostics](#testing-and-diagnostics)
* [Main features and improvements from F4HWN](#main-features-and-improvements-from-f4hwn)
* [Main Features from Egzumer](#main-features-from-egzumer)
* [Manual](#manual)
* [Compiling and Building from Docker](#compiling-and-building-from-docker)
* [Flashing the Firmware with UV Studio](#flashing-the-firmware-with-uv-studio)
* [Credits](#credits)
* [Other sources of information](#other-sources-of-information)
* [License](#license)

## Main features and improvements from F4HWN

### Fusion edition

Fusion is the generic reference edition for the UV-K1 and UV-K5 V3. It is intended
for everyday use and is the base inherited by the specialized editions. It includes:

- Fagci's spectrum analyzer,
- broadcast FM radio and VOX,
- [UV Studio](https://armel.github.io/uvstudio/) with integrated K5Viewer screen mirroring, screenshots and remote keyboard control,
- advanced RX audio profiles and Audio Scope,
- automatic RX/TX activity logging with RF Log,
- multiboot support.

Specialized presets extend Fusion for specific uses:

- **Transfer** adds AirCopy and BEAM wireless channel transfer.
- **FieldOps** adds first-responder controls, Fox Hunt and Morse Beacon support.
- **Labs** is the experimental edition. It carries the broad feature selection of the
  other releases and adds the overlay-apps platform (apps loaded from external Flash and
  run in a 4 KiB RAM overlay) — the newest, least-settled work. Expect rough edges. It is
  not a strict superset of every other edition: features may be exchanged between releases
  to preserve stability and memory headroom.
- **Custom** remains a manually configured build based directly on the hidden technical default.

### Radio and signal handling

- Reworked output-power levels:
  - `Low 1`: below approximately 20 mW,
  - `Low 2`: approximately 125 mW,
  - `Low 3`: approximately 250 mW,
  - `Low 4`: approximately 500 mW,
  - `Low 5`: approximately 1 W,
  - `Mid`: approximately 2 W,
  - `High`: approximately 5 W,
  - `User`: configurable through `SetPwr`.
- S-meter calibrated according to the [IARU Region 1 recommendation for VHF/UHF](https://hamwaves.com/decibel/en/):
  - fixed S0 to S9+ values replace the former EEPROM S-meter thresholds,
  - Classic and Tiny display styles are available.
- Configurable 12.5 kHz or 6.25 kHz narrow-FM bandwidth.
- Per-channel TX lock.
- Adjustable RX audio volume.
- Advanced RX audio profiles for FM and AM reception.
- Regional frequency-lock profiles for amateur bands, PMR446, FRS, GMRS and MURS.
- Support for 1600, 2200 and 3500 mAh battery profiles.

### Spectrum analyzer

- Channel names displayed in the spectrum view.
- Persistent spectrum settings.
- Faster and smoother spectrum rendering.
- Improved behavior when freezing the spectrum or disconnecting USB-C.
- Spectrum analyzer state can be restored automatically at startup.

### Scanning

- Support for up to 24 named scan lists.
- Each memory channel can be assigned to:
  - `OFF`,
  - one scan list from `01` to `24`,
  - `ALL`.
- The `ALL` list scans every channel except those assigned to `OFF`.
- Automatic selection of the next valid list when the requested list is empty.
- Direct scan-list selection while scanning:
  - `00` selects `ALL`,
  - `01` to `24` select the corresponding list.
- Long press on `MENU` while scanning to exclude the current memory channel.
- Up to 64 frequency exclusions.
- Very fast scanning mode, reaching approximately 150 frequencies per second.
- Scan progress, RSSI and detected CTCSS/DCS information.
- Configurable scan resume behavior.
- Scan state can be restored automatically at startup.

### User interface

- Improved VFO screen with:
  - Classic and Tiny S-meter styles,
  - Classic and Tiny frequency-information layouts,
  - `MAIN ONLY`, `DUAL` and `CROSS` display modes,
  - RX activity indication on the active VFO,
  - optional RX LED blinking,
  - squelch, monitor, step and CTCSS/DCS information,
  - last-RX indication,
  - RX and TX timers.
- Improved status bar with updated fonts and icons.
- Menu index remains visible while editing an entry.
- Improved frequency and memory-channel input.
- Improved audio-level display.
- AirCopy progress percentage and gauge.
- Smooth backlight fading.
- Manual backlight controls for quickly switching between minimum and maximum brightness.
- Configurable contrast and inverted-display mode.
- Configurable navigation layout for the different radio models.
- Improved power-on message and optional startup logo.
- System-information pages for:
  - firmware version and build information,
  - battery information,
  - Flash and SRAM usage,
  - project and documentation QR codes.

### Audio and transmission controls

- Classic and OnePush PTT modes.
- Configurable timeout-timer alerts:
  - disabled,
  - sound,
  - visual,
  - sound and visual.
- Configurable end-of-transmission alerts using the same modes.
- Audio Scope during RX and TX.
- Improved Audio Scope behavior with OnePush PTT, DTMF and the 1750 Hz tone.
- Configurable automatic deep-sleep timeout.
- Quick actions for:
  - RX mode,
  - main-VFO-only display,
  - PTT,
  - wide/narrow bandwidth,
  - 1750 Hz tone,
  - mute,
  - RX audio profile,
  - maximum power,
  - offset removal.

### Fox Hunt and Beacon

- Dedicated Fox Hunt receiver with:
  - calibrated S-meter display,
  - scrolling signal-history graph,
  - peak, minimum and trend indicators,
  - selectable RF attenuation,
  - silent, Geiger-style and received-audio modes,
  - long-press `F` keypad lock (attenuation stays adjustable with the arrow keys).
- Independent Morse Beacon transmitter with:
  - `MOE`, `MOI`, `MOS`, `MOH`, `MO5` and `MO` identifiers,
  - optional callsign identification,
  - configurable TX and idle periods,
  - live TX and idle countdowns,
  - persisted Beacon settings,
  - interactive control during transmission,
  - shared long-press `F` keypad lock,
  - TX-lock, modulation and battery-safety checks.
- Fox Hunt and Beacon screens are mirrored to UV Studio's integrated K5Viewer.

### RF Log

- Automatic logging of RX and TX activity when RF Log is enabled.
- Logs stored in the radio's external Flash memory.
- Recorded information includes:
  - RX or TX direction,
  - frequency and channel,
  - channel name,
  - activity duration,
  - S-meter level,
  - battery voltage.
- On-radio history with RX/TX filtering and detailed views.
- Live RF Log dashboard and history access through UV Studio.

### Connectivity and data transfer

- Live screen streaming to [UV Studio](https://armel.github.io/uvstudio/) over a USB serial connection.
- Screenshot capture and download.
- Remote keyboard control through the integrated K5Viewer.
- Automatic reconnection after a USB disconnect.
- RF Log monitoring, analytics and CSV export.
- Firmware flashing, calibration backup and restore, and boot-logo management from the same interface.
- BEAM transfer of complete channel settings between compatible radios.
- Improved AirCopy interface and progress reporting.

### Settings and controls

- New or extended menu entries:
  - `SetPwr`: configurable User output power,
  - `SetPTT`: Classic or OnePush PTT,
  - `SetTOT`: timeout-timer alert,
  - `SetEOT`: end-of-transmission alert,
  - `SetCtr`: display contrast,
  - `SetInv`: inverted display,
  - `SetLck`: keypad or keypad-and-PTT lock,
  - `SetMet`: S-meter style,
  - `SetGUI`: VFO information style,
  - `SetRxA`: RX audio profile,
  - `SetTmr`: RX and TX timers,
  - `SetOff`: automatic deep-sleep delay,
  - `SetNFM`: narrow-FM bandwidth,
  - `SetVol`: RX audio volume,
  - `SetScn`: scan mode,
  - `SetNav`: radio-specific navigation layout.
- Improved `PonMsg`, `BackLt`, `TxTOut`, `ScnRev` and `KeyLck` menus.
- Full VFO state restoration with a long press on `EXIT`.
- Squelch changes made with `F + UP` or `F + DOWN` are persisted.

### Keyboard shortcuts and assignable actions

- `F + UP` or `F + DOWN`: adjust the squelch level.
- `F + F1` or `F + F2`: adjust the frequency step.
- `F + 8`: temporarily switch the backlight between minimum and maximum brightness.
- `F + 9`: return to the configured backlight strategy.
- Configurable short- and long-press actions include:
  - RX mode,
  - main-VFO-only display,
  - virtual PTT,
  - wide/narrow bandwidth,
  - 1750 Hz tone,
  - mute,
  - RX audio profile,
  - maximum power,
  - offset removal,
  - BEAM,
  - RF Log,
  - Fox Hunt,
  - Beacon.

### Reliability and optimization

- Improved squelch and S-meter behavior.
- Fixed DTMF overlay issues.
- Fixed scan-range limits.
- Cleaner startup display.
- Removed PWM-related audio noise.
- Improved serial and K5Viewer key handling.
- Improved VFO persistence and restoration.
- Extensive code refactoring and memory optimization.
- DTMF calling and the scrambler remain disabled in Fusion.
- Legacy AM Fix support has been removed.

## Main features from Egzumer:
* many of OneOfEleven mods:
   * long press buttons functions replicating F+ action
   * fast scanning
   * channel name editing in the menu
   * channel name + frequency display option
   * shortcut for scan-list assignment (long press `5 NOAA`)
   * scan-list toggle (long press `* Scan` while scanning)
   * configurable button function selectable from menu
   * battery percentage/voltage on status bar, selectable from menu
   * longer backlight times
   * mic bar
   * RSSI s-meter
   * more frequency steps
   * squelch more sensitive
* fagci spectrum analyzer (**F+5** to turn on)
* some other mods introduced by me:
   * SSB demodulation (adopted from fagci)
   * backlight dimming
   * battery voltage calibration from menu
   * better battery percentage calculation, selectable for 1600mAh or 2200mAh
   * more configurable button functions
   * long press MENU as another configurable button
   * better DCS/CTCSS scanning in the menu (`* SCAN` while in RX DCS/CTCSS menu item)
   * Piotr022 style s-meter
   * restore initial freq/channel when scanning stopped with EXIT, remember last found transmission with MENU button
   * reordered and renamed menu entries
   * LCD interference crash fix
   * many others...

 ## Manual

Up to date manual is available in the [Wiki section](https://github.com/armel/uv-k1-k5v3-firmware-custom/wiki)

## Radio performance

Please note that the Quansheng UV-Kx radios are not professional quality transceivers, their
performance is strictly limited. The RX front end has no track-tuned band pass filtering
at all, and so are wide band/wide open to any and all signals over a large frequency range.

Using the radio in high intensity RF environments will most likely make reception difficult,
especially in AM mode. The receiver simply does not have a great dynamic range, so stronger
signals can easily cause distortion, desensitization and poor AM audio.
This is fundamentally a hardware limitation: firmware can improve behavior at the margins, but
it cannot overcome the front-end design of the radio.
In practice, AM reception will degrade first and most severely, while FM reception is generally
more tolerant and should remain more usable.

But, they are nice toys for the price, fun to play with.

## Compiling and Building from Docker

This project provides a Docker-based build system for the UV-K1 and UV-K5 V3.
Everything is handled through the `compile-firmware.sh` helper script. Fusion is
the default generic preset, while specialized builds are generated in their own
`build/<Preset>` directories.

### Prerequisites

- Docker installed on your system
- Bash environment (Linux, macOS, WSL, Git Bash on Windows)

### Build Script Overview

The script `compile-firmware.sh`:

1. Builds the Docker image (`uvk1-uvk5v3`) if it does not already exist.
2. Configures the selected preset with `cmake --fresh`.
3. Builds the firmware and outputs matching `.elf`, `.bin` and `.hex` files.
4. Displays Flash and RAM usage; `All` keeps the individual build logs quiet.

### Usage

```bash
./compile-firmware.sh [Preset] [extra CMake options]
```

The default preset is **Fusion**. Available presets are:

- **Custom**
- **Fusion**
- **Transfer**
- **FieldOps**
- **Labs**
- **Max**
- **CAT** (Kenwood CAT remote control edition)
- **All** (Fusion, Transfer, FieldOps, Labs, Max, and CAT)

Examples:

```bash
./compile-firmware.sh
./compile-firmware.sh CAT
./compile-firmware.sh Fusion
./compile-firmware.sh Transfer
./compile-firmware.sh FieldOps
./compile-firmware.sh Labs
./compile-firmware.sh All
```

### Passing Additional CMake Options

You can pass extra configuration options after the preset name.  
These are forwarded directly to `cmake --preset` inside the container.

Examples:

```bash
./compile-firmware.sh FieldOps -DENABLE_VOX=OFF
./compile-firmware.sh Fusion -DENABLE_FEAT_F4HWN_GAME=ON
./compile-firmware.sh Fusion -DSQL_TONE=600
```

To prepare the rolling development firmware:

```bash
./compile-firmware.sh Fusion -DDEV=ON
```

This keeps the regular build output in `build/Fusion` and also updates
`archive/f4hwn.fusion.development.bin`. The development build is identified as
`DEV` in the firmware information screen. Publishing the updated archive file
remains an explicit Git operation.

### Notes

- The first run may take a few minutes while Docker builds the base image.
- Each build runs inside Docker, so your host environment remains clean.

## Flashing the Firmware with UV Studio

You can flash the UV-K5 V3 and UV-K1 directly from your web browser using the Web Serial-based [UV Studio](https://armel.github.io/uvstudio/#dump-calib).

UV Studio combines firmware flashing, calibration maintenance, boot-logo management, K5Viewer and RF Log in a single interface. It requires no application installation, server or account. Use a desktop browser with Web Serial support, such as Chrome, Brave, Edge, Opera or Firefox 151+.

> [!IMPORTANT]
> **Flashing the CAT Firmware:**
> 1. Download the **`f4hwn.cat.bin`** file (available in the root of this repository or in `build/CAT/`).
> 2. Open [UV Studio](https://armel.github.io/uvstudio/#dump-calib).
> 3. **Back up your calibration first!** Go to [Dump Calibration](https://armel.github.io/uvstudio/#dump-calib) with the radio in normal mode and download your `calibration.dat` file.
> 4. Put your radio into **DFU mode (flash mode)** (hold PTT while turning the power knob on).
> 5. Load and flash the downloaded **`f4hwn.cat.bin`** file using UV Studio.

## Steps to flash the firmware

- Download the **`f4hwn.cat.bin`** file.
- Open [UV Studio](https://armel.github.io/uvstudio/#dump-calib) in your browser.
- Connect your radio to your computer using a compatible USB programming cable (USB-C or Baofeng/Kenwood style double-jack USB cable).
- Make sure your radio is in **DFU mode (flash mode)** (power on while holding PTT).
- Load your local **`f4hwn.cat.bin`** firmware file.
- Click on `Flash Firmware`, then select the serial port associated with your radio.
- The progress bar will guide you through the flashing steps.

Once finished, your radio restarts with the new firmware.

## Steps to dump or restore calibration data

[UV Studio](https://armel.github.io/uvstudio/) can also dump and restore calibration data, which is highly recommended. It is best to create a dump immediately after installing the F4HWN firmware, and to restore it before installing another firmware or returning to the stock firmware.

### Dump

- Open the [Dump Calibration](https://armel.github.io/uvstudio/#dump-calib) view in UV Studio.
- Power on your radio in **normal mode**.
- Click `Dump Calibration Data`.

When the process is complete, click `Download calibration.dat` to save the file to your computer.

> [!NOTE]
> A good practice is to rename your calibration file using the serial number of your radio, which you can find on the label on the back of the device once you remove the battery. This helps avoid mixing up calibration files when you own multiple units.

### Restore

- Open the [Restore Calibration](https://armel.github.io/uvstudio/#restore-calib) view in UV Studio.
- Power on your radio in **normal mode**.
- Select your `calibration.dat` file on your computer.

Click `Restore Calibration Data` and wait until the process fully completes.

## Other sources of information

- [k1-teardown](https://github.com/armel/k1-teardown) 

## Credits

Many thanks to various people:

* [Muzkr](https://github.com/muzkr)
* [Mrkusypl](https://github.com/mrkusypl)
* [Andrej](https://github.com/Tunas1337)
* [Egzumer](https://github.com/egzumer)
* [OneOfEleven](https://github.com/OneOfEleven)
* [DualTachyon](https://github.com/DualTachyon)
* [Mikhail](https://github.com/fagci)
* [Manuel](https://github.com/manujedi)
* @wagner
* @Lohtse Shar
* [@Matoz](https://github.com/spm81)
* @Davide
* @Ismo OH2FTG
* [OneOfEleven](https://github.com/OneOfEleven)
* @d1ced95
* and others I forget

## License

Copyright 2023 Dual Tachyon
https://github.com/DualTachyon

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
