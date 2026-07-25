# 0001 — Dock mode: engage the PA on key-up (`Dock_ForceTx`, F5)

Status: Accepted

> First ADR in this fork. Prior cycles (F1 build gate, F2 dock port, F3a RX force-open) are
> recorded in radio-server's `docs/adr/` (0118 / 0119 / 0120) and this repo's `BENCH.md`. F5 adds
> an ADR here because the change is a self-contained firmware decision; radio-server's companion
> ADR **0126** closes its "Chain B" from the host side.

## Context

radio-server drives this radio as a "dock": it holds full-control and does everything through
BK4819 register reads/writes over the UART protocol in `App/app/dock.c`. It keys TX by writing
`REG_30` (the TX-enable word `0xC1FE`) and confirms with a read-back (`0xC1FE`).

F4 Chain B (radio-server bench) proved that keying **produces no radiated RF**: with an antenna,
an objective UHF receiver (a kv4p, inches away) saw carrier `False` through a confirmed 5.7 s key
(9 polls keyed-WITHOUT-RF, 0 with); on a dummy load it saw only near-field chip RF. The modulator
keys; nothing reaches the antenna.

### Root cause — a bare `REG_30` write skips the PA chain

Stock TX (`FUNCTION_Transmit` → `RADIO_SetTxParameters`, `App/radio.c:972`) does far more than
write `REG_30`. Register-by-register, a bare `REG_30=0xC1FE` write **omits**:

- **`REG_33` GPIO1 `PA_ENABLE`** (`BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, true)`,
  `radio.c:1017`) — the external PA rail / antenna-switch enable. **The primary missing step.**
- **`REG_36` PA bias** (`BK4819_SetupPowerAmplifier(TXP_CalculatedSetting, freq)`, `radio.c:1021`)
  — the calibrated analog PA drive.
- **`REG_50`/`REG_37`/`REG_52`** un-mute (`BK4819_PrepareTransmit`, `bk4819.c:1166`).
- dropping `REG_33` GPIO0 `RX_ENABLE` and setting the LNA/RF-filter path GPIOs.

**Correction to the F4 framing.** The radio-server HANDOFF called these "MCU-side GPIOs," by
analogy to the F3a RX gap where `GPIOA8` (an MCU pin) was genuinely un-dockable. That is not the
case here: on this **PY32F071 port there are no MCU GPIOs in the TX path** — PA-enable, the T/R
switch, and the LNAs are all **internal BK4819 GPIOs written through `REG_33`**, and PA bias is
`REG_36`. Both are BK4819 registers reachable over the dock in principle; radio-server simply never
writes them. The mechanism (no PA) stands; the "un-dockable" label was imprecise.

## Decision — slave the PA to `REG_30`: decode in the core, drive in the glue

**1. Core (`App/app/dock.c` + `dock.h`) — a per-write TX-state seam.** `dock_hal_t` gains one
optional `tx_set(user, on)` callback and `dock_ctx_t` a cached `tx_on`. In the `WRITE_REGS`
dispatch, a write to `REG_30` edge-detects `ENABLE_TX_DSP` (bit 1: set in `0xC1FE`, clear in the
RX word `0xBFF1`) and calls `tx_set(on)` **before** completing the register write. `tx_set(false)`
is also forced at `0x0870` enter and `0x0871` exit (fail-safe against a stale key). Edge-detected,
so repeat writes and redundant force-offs never re-fire. This adds **zero wire bytes** — no new
opcodes, no replies on writes — so radio-server's `FirmwareFakeSerial`/`Uvk5Decoder` byte-compat
and the whole F2 invariant hold; `dock_hal_t` is internal firmware state the host never sees.

**2. Glue (`App/app/uart.c`) — `Dock_ForceTx` / `Dock_EndTx`, bound to `tx_set`.** These add
exactly the stock PA steps, reusing the firmware's own helpers (not raw pokes):

```c
// key: add what a bare REG_30 write skips, in stock order
BK4819_ToggleGpioOut(RX_ENABLE, false);
BK4819_PrepareTransmit();                 // REG_50/37/52
SYSTEM_DelayMs(10);
BK4819_PickRXFilterPathBasedOnFrequency(freq);
BK4819_ToggleGpioOut(PA_ENABLE, true);    // the missing rail
SYSTEM_DelayMs(5);
BK4819_SetupPowerAmplifier(TXP_CalculatedSetting, freq);   // REG_36 bias
SYSTEM_DelayMs(10);

// un-key: stock order — bias to 0 BEFORE the rail (radio.c:782->784), then restore RX
BK4819_SetupPowerAmplifier(0, 0);
BK4819_ToggleGpioOut(PA_ENABLE, false);
BK4819_ToggleGpioOut(RX_ENABLE, true);
Dock_ForceRxAudioAlive();                 // PrepareTransmit muted REG_47 — re-open RX audio
```

The `SYSTEM_DelayMs` settle points mirror stock and target the F4 first-key settle flake
(`REG_30=0xBFF1` on the first key, retry passes). `Dock_EndTx` re-runs the F3a RX force-open so a
service announcement (a TX cycle) does not leave the receiver deaf.

**Why not call `RADIO_SetTxParameters()` wholesale?** It re-tunes from `gCurrentVfo->pTX->Frequency`
(`radio.c:1006`), which in dock mode may differ from the frequency the host tuned via `REG_38/39` —
it would fight the dock-tuned frequency. The surgical path leaves `REG_30` and frequency to the
host and only adds the missing PA chain.

## Consequences

- **radio-server is byte-identical.** The fix is entirely in the fork; no `frames.py`/`transport.py`/
  backend change. Its only radio-server deliverable is docs ADR 0126.
- **Host tests: 19 → 31 checks** (`tests/host/`), all green under `-Wall -Wextra -Werror`. The new
  cases prove the state machine: key/un-key edges fire `tx_set` before the register write (`"1w"` /
  `"1w0w"` ordering), edge-detect suppresses repeats (`"1www"`), non-`REG_30` writes never key, and
  `0x0870`/`0x0871` force the PA off. The PA-driving itself (`uart.c`) is bench-verified, not
  host-compiled.
- **Fail-safe.** PA strictly slaved to `REG_30`, forced off at enter/exit. A host crash mid-key is
  covered by the existing TOT/RF-guards (untouched) — not by this change. (The `dock_rx_byte`
  buffer-overflow guard is unreachable for valid frames, `DOCK_RX_BUF 264 > max total 262`, and the
  firmware uses the top-level UART deframer, not `dock_consume`; no force-off was added there.)

## Verify-on-bench (marked, not asserted)

- Which `OUTPUT_POWER` level dock TX radiates, and confirming the `gCurrentVfo` frequency source for
  the PA bias (`Dock_ForceTx` comment). On the UHF bench (both radios 445.800) the band matches; a
  VHF/UHF mismatch would only mis-scale power, not prevent keying.
- The staged bench acceptance (dummy load → `--key-test` → carrier watch shows RF where F4 showed
  none → browser TX + service → antenna range proof) is in `BENCH.md` (F5 section).

## Source of truth

Fork `kbennett2000/uv-k1-k5v3-firmware-custom`, branch `f5-dock-force-tx`, based on the
`f3-rx-audio-fix` tip `79f9b21` (same pin v5.7.0 / `3bd3ebba`, Apache-2.0). Symbols reused, all
pre-existing: `BK4819_ToggleGpioOut`/`BK4819_SetupPowerAmplifier`/`BK4819_PrepareTransmit`/
`BK4819_PickRXFilterPathBasedOnFrequency` (`App/driver/bk4819.h`), `gCurrentVfo.TXP_CalculatedSetting`
(`App/radio.h`), `Dock_ForceRxAudioAlive` (F3a, `App/app/uart.c`).
