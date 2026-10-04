# APRSTX: experimental stock v6.0.0 overlay

Position-only, manual APRS TX for **F4HWN Labs v6.0.0 on UV-K1 / UV-K5 V3,
48 MHz PY32F071, active BK4829 driver**. This app does not change resident
firmware or API level 1. It uses the existing tone generator, not a PCM modem.
It is an experimental hardware backend: host/ARM checks pass, but hardware,
RF waveform, decoder acceptance, and stack watermark tests are pending.

Build from the repository root:

```sh
bash tests/aprstx/run.sh
./compile-app.sh aprstx
```

Install `build/Apps/APRSTX.app` with UVStudio Apps into a visible slot 1–8.
This replaces that slot's app/config. Open Apps (normally F+7). No screensaver
or shortcut is registered; no automatic or background transmissions occur.
The source callsign starts empty: the app never substitutes a test callsign.

## Configure and send

MENU opens setup; the navigation arrows select one of six fields. MENU opens
a draft and confirms it. EXIT cancels the draft or returns to the main screen;
EXIT from the main screen closes the app. Text/numeric drafts start empty.
Invalid values stay in the editor until corrected or cancelled.

* Call: 1–6 A–Z / 0–9 characters. Keys 2–9 cycle letters within 700 ms; wait or
  press another key to advance. F toggles letters/digits, STAR deletes.
* SSID: 0–15, entered separately from the call.
* Latitude: exactly `DDMMhh`, longitude: `DDDMMhh`, with hundredths of minutes.
  For example `554500` and `0373700` mean 55°45.00′ N, 037°37.00′ E. F toggles
  N/S or E/W. Confirm both fields, including intentional zero coordinates.
  Minutes must be below 60; ±90°/±180° allow only 00.00 minutes.
* Path: 0 = direct (default), 1 = WIDE1-1, 2 = WIDE1-1,WIDE2-1, 3 = ARISS.
  Select a path appropriate to the network; the app does not choose one.
* Symbol: arrows select printable `!`…`~`; F toggles `/` and `\` tables.

The main screen shows source/SSID, **selected TX frequency in MHz**, position
and path. Set frequency/power in the normal firmware before opening the app.
After all keys and PTT have been released for 60 ms, a new held physical PTT
press starts one attempt. Release PTT to cancel CCA or TX. Holding it after
SENT, BUSY or an error sends nothing further; release and press again to retry.
SENT means the waveform completed, not that a receiver acknowledged it.

Changed, fully valid settings are staged once on normal app exit using
`cfg_save(...,16)`; the loader writes flash after the app returns. Incomplete
setup is not saved. Config is slot-local, versioned `0xA1`, with CRC8/poly07,
padded callsign, SSID/path/table/valid flags, signed24 coordinates and symbol.
No external flash writes occur while the app runs.

## Stock hardware limitations

Use **FM, simplex RX=TX, dual watch and crossband disabled, selected VFO active,
and enter from quiet RX**. API 1 cannot inspect all those settings or prove
that hidden `gCurrentVfo` equals selected `gTxVfo`; these are user/setup
preconditions requiring device tests. `tx_state` checks frequency/TX lock, FM and battery. It does not enforce the
voice path's busy-channel lock or serial-session restrictions; avoid serial
configuration while this app runs. CCA rejects RX/TX mismatch and uses a provisional
RSSI threshold −110 dBm, 200 ms continuous quiet, 100–400 ms backoff and a
nominal five-second poll budget. It is a noise-level heuristic, not AX.25 DCD.

The app reads only these fixed MMIO addresses, and never writes them:

| Read | Address | Assumption |
| --- | --- | --- |
| SysTick CTRL / LOAD / VAL | E000E010 / E000E014 / E000E018 | CPU clock, enabled IRQ, LOAD=479999 |
| GPIOB IDR | 50000410, bit10 | Active-low physical PTT |

The bit clock accumulates a continuously sampled SysTick down-counter, waits
absolute 40000-cycle deadlines, and writes tone1 register71 only on NRZI zero.
Mark/space words are 3065/58BA. Flags are unstuffed, frame/FCS are stuffed
LSB-first, and the last symbol receives a full period. IRQs remain enabled.
A provisional 960-cycle (20 µs) lateness abort avoids catch-up bursts. Sampling
must never be interrupted for a full 10 ms SysTick wrap; timing and SPI WCET
must be measured on hardware. No display, key-matrix scan, delays, battery
sampling, flash access or sound effects occur in the timed loop.

The existing `tx_set_params` callback briefly enables PA **before** the app
can finish setup. The app immediately gates PA off, checks programmed frequency
registers38/39 against the selected frequency, and aborts mismatches. That
check cannot prevent the initial brief carrier on a stale VFO. Tone preparation
runs once with PA gated off, disables scrambling/VOX/compander/DTMF/subtones,
and verifies the microphone ADC is disabled before keying the data waveform.
Only the resident-assisted variant can provide quiet preparation and RF
ownership before PA-on.

All controlled outcomes share cleanup: PA off first, mute, tone1 off, resident
RX restoration. A software limit of 1900 ms after setup reserves a nominal
100 ms for blocking callbacks. **This is not an independent two-second PA
watchdog**: callbacks, missed SysTick wraps, IRQ starvation or HardFault can
defeat that bound. Stock cannot guarantee PA-off after a fault or bound
cancellation latency inside blocking setup. The resident-assisted PR supplies
the stronger timer/cancellation/timeout contract.

## Build evidence and required RF validation

ARM GNU 13.3.rel1, `-Os -flto -fno-inline-functions-called-once`:
payload / total workspace **4080 bytes**, `.bss` **172 bytes**, `.app` **4144
bytes** including the 64-byte header. `__app_end=0x20001270` leaves **16 bytes**
in the 4096-byte workspace. The 384-byte design reserve is not achieved;
position is the sole packet mode and comments are empty. The linker asserts
the whole workspace, not only file size. ELF has no undefined symbols; libc,
float, heap and division runtime are absent. Metadata: ABI1/API1, caps0,
VMA20000280, experimental APZOV1 destination.

`-fstack-usage` output is retained beside the map/ELF. The largest individual
app frame is72 bytes; the deepest apparent app-only chain is184 bytes
(entry64 + draw56 + coordinate40 + number24), excluding resident callbacks,
loader/menu and interrupts. This is not a measured total device stack bound.
The workflow uploads the .app/map/ELF/stack reports for review.

Host tests cover the LLD's exact FDD6/339-bit vector, independent unstuffing,
terminal stuffing before a tail flag, X.25 residue, all path EA/C/H bits,
config corruption, coordinate endpoints/minute60, editor cancel/multi-tap,
and held-at-entry / one-frame-per-new-PTT behavior. Hardware acceptance remains:

1. Dummy load and attenuated measurement: verify RF frequency/power, tone
   frequencies, deviation/twist and absence of microphone/subtone leakage.
2. Measure SPI completion jitter, symbol intervals, final symbol, cancellation
   latency, software timeout, and worst-case IRQ load.
3. Decode 100/100 fixed frames using Dire Wolf and a second independent decoder.
4. Test stale-current VFO/dual-watch/crossband/reverse/offset scenarios, failure
   cleanup, repeat launches and flash-save behavior; obtain stack watermark.

The protocol/editor are reusable, hardware-independent sources for the
resident-assisted app. New APRS code is written from the wire format described
in the LLD; no donor APRS implementation is copied. License: Apache-2.0, as
marked in each source; `app.ld` follows the repository's overlay build model.
