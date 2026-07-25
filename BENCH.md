# BENCH.md — flashing this firmware to a UV-K5 V3

The proven flash path for this fork, so every future flash follows the same steps.

> **Radio:** Quansheng **UV-K5 V3** — PY32F071 MCU, bootloader **7.00.07**.
> The V3 is **not** the classic UV-K5 (DP32G030). It flashes over **USB DFU**, not the
> DP32G030 serial bootloader (PTT+flashlight-on-power-up). Do not use classic-K5
> flash procedures on this radio.
>
> **Tool:** `uvtools2`.

---

## ⚠ Provenance of this document

This runbook was **drafted by the F1 build cycle from the kickoff's named steps plus the
public uvtools2/DFU procedure**. The four radio-specific specifics below are marked
**`⚠ CONFIRM AT BENCH`**. They are **not** empirically confirmed here — the project rule is
that hardware facts are verified on the hardware, never asserted from memory (radio-server
CLAUDE.md guardrail 1). Kris confirms/corrects each one on the first real flash, then this
banner comes off.

---

## What you flash

The `.bin` for the target edition. For the F1 build gate that is the **Fusion** build
produced by this fork at tag `v5.7.0` / commit `3bd3ebba`, delivered as a **pre-release
asset** on this repo (`f4hwn.fusion.<...>.bin` + `SHA256SUMS`). Verify the SHA before
flashing:

```bash
sha256sum -c SHA256SUMS
```

For a normal release, flash the upstream `f4hwn.fusion.vX.Y.Z.bin`.

---

## Procedure (as performed today)

### 1. DFU entry  ⚠ CONFIRM AT BENCH
Put the V3 into USB DFU mode.

- **Exact key-combo / power sequence to enter DFU:** `⟨CONFIRM: e.g. hold <key(s)> while
  connecting USB / powering on⟩`
- Confirm the host enumerates the DFU device (e.g. `lsusb` shows the PY32/DFU device;
  record the **USB VID:PID** here once seen: `⟨CONFIRM VID:PID⟩`).
- uvtools2 should detect the radio in DFU mode before you proceed.

### 2. The FTDI cable  ⚠ CONFIRM AT BENCH
- **Which cable / adapter and how it's wired:** `⟨CONFIRM: FTDI part, which pins → radio
  jack pins, any level-shift or DTR/RTS caveat⟩`
- **Whether the FTDI path is for flashing, for the post-flash calib dump (step 5), or
  both:** `⟨CONFIRM⟩`
- Note any driver/permissions step (e.g. `dialout` group, the `/dev/ttyUSB*` that appears).

### 3. Flash with uvtools2
- Open uvtools2, select the DFU-mode radio, load the verified `.bin`, write, and let it
  verify.
- Record the exact uvtools2 version/build used: `⟨CONFIRM uvtools2 version⟩`.

### 4. The tab-conflict gotcha  ⚠ CONFIRM AT BENCH
There is a known conflict where **`⟨CONFIRM: describe exactly what conflicts — e.g. two
uvtools2 tabs/panels contending for the same serial port, or a second app/browser tab
holding the port, so the flash/read fails or hangs⟩`**.

- **Symptom:** `⟨CONFIRM⟩`
- **Avoidance / fix:** `⟨CONFIRM: e.g. close the other tab / release the port / use only the
  one panel before flashing⟩`

### 5. Calibration dump — after first boot  ⚠ CONFIRM AT BENCH
After the **first boot** on new firmware, dump the radio's calibration so it's captured
before any further changes.

- **When:** immediately after the first boot completes (radio has re-initialised on the new
  firmware).
- **How:** `⟨CONFIRM: exact tool + command / uvtools2 action to read the calibration region,
  and where the dump is saved⟩`
- **Why:** `⟨CONFIRM: e.g. so calibration can be restored if a later flash disturbs it⟩`
- Keep the dump with the flash record for this radio.

---

## Acceptance (what "flashed clean" means)

Radio **boots**, **receives**, and the **keypad works** — behaviour identical to the
firmware it replaced. For the F1 gate specifically: flashing the F1-built Fusion bin over
the release Fusion must show **no behavioural difference** (the F1 build is the unmodified
upstream tree at the pin). That green-lights F2 (the dock-protocol port).

---

## F2 acceptance — the dock control mode

Flash the **F2** build (`f4hwn.fusion.v5.7.0.f2-dock.bin`, the `ENABLE_DOCK` Fusion image
from the `radio-server-f2-v5.7.0` pre-release; verify its `SHA256SUMS` first). Then, in
order:

1. **Connect probe (the port working).** From radio-server:
   ```
   uv run radio-server doctor --backend uvk5
   ```
   The dock connect probe sends a `ReadRegisters(0x30)` (`0x0851`) and waits for a
   `RegisterInfo` (`0x0951`) answer. **The radio answering that elicit IS the port
   working** — doctor reports "Dock firmware alive". (A stock/no-dock build times out here.)
2. **The four F1 gates, still true with the dock idle:** radio **boots**, **receives**,
   **keypad works**, and behaviour is otherwise identical to plain Fusion until radio-server
   takes control. Entering/leaving full-control (`0x0870`/`0x0871`) should return the radio
   to normal RX on exit.
   - `⚠ CONFIRM AT BENCH`: the exact resume-RX behaviour after `0x0871`
     (`RADIO_SetupRegisters(true)` on loop exit) — confirm the radio returns to normal
     receive cleanly after radio-server disconnects.

Green F2 acceptance green-lights **F3** (the full radio-server end-to-end bench loop).

---

## F3 acceptance — the RX-audio fix (dock entry forces the audio path alive)

Background (F3a live-bench diagnosis, radio-server ADR 0120): with the F2 build, RX audio to
the AIOC read the **noise floor** whenever radio-server held full-control. Proven root cause:
the `0x0870` blocking loop starves the firmware timeslice, so `APP_StartListening()` never
runs — nothing raises **GPIOA8** (the external AF-amp enable, an MCU GPIO the dock protocol
cannot reach) or unmutes `REG_47`. Over the dock we forced `REG_30=0xBFF1` + `REG_47=0x6142` +
`REG_48` loud, **confirmed all three by read-back**, and the AIOC still read ~109 RMS — BK4819
fully RX-alive yet silent ⇒ the gate is outside the BK4819. The fix: `Dock_EnterFullControl`
now calls `GPIO_EnableAudioPath()` + `BK4819_SetAF(BK4819_AF_FM)` + `BK4819_SetRxAudioGain()`
up-front, so RX audio flows the whole session. `+28 bytes` (104,144 B, 86.2%).

Flash the **F3** build (`f4hwn.fusion.v5.7.0.f3-rx-audio.bin` from the `radio-server-f3-v5.7.0`
pre-release; verify its `SHA256SUMS` first). Then, in order:

1. **HT-free RX self-test (the fix working, no second radio):**
   ```
   uv run radio-server doctor --backend uvk5 --rx-noise
   ```
   It enters full-control, force-opens RX at the chip, and measures the AIOC. **Post-fix this
   reads thousands of RMS ("LOUD")**; with the F2 build it read the floor. That jump is the fix.
2. **Live RX:** key an HT on 445.800 → `doctor --backend uvk5 --rx-level` reads loud; browser
   **Listen** produces audio.
3. **Post-fix TX check (the folded F3a Phase 2):** in the browser, key PTT ~2 s and release;
   then re-run `--rx-noise` — it must **still** read thousands. A TX/unkey cycle must not
   re-kill RX (`_key_off` restores only `REG_30`; `REG_47`/GPIOA8 are forced at entry and
   untouched by TX). If it goes silent, note which register `_key_off` left wrong (suspect
   `REG_50=0x3B20`).
4. **Resume-RX on `0x0871` exit — SETTLED by F3a data** (supersedes the F2 `⚠` item): the
   baseline→post-exit register diff showed `RADIO_SetupRegisters(true)` re-applies
   frequency/bandwidth and returns the radio to normal **muted idle** RX (`REG_47`→MUTE,
   audio-path off). Radio returns to normal receive cleanly after radio-server disconnects.

---

## F5 acceptance — the TX fix (dock keying now engages the PA)

Background (F4 Chain B, radio-server HANDOFF + ADR 0126): with the F3 build, dock keying wrote
BK4819 `REG_30` (TX_DSP) and the CONFIRM read-back passed, but **no RF radiated** — on an antenna
the kv4p (an objective UHF receiver inches away) saw carrier `False` through a confirmed 5.7 s key
(9 polls keyed-WITHOUT-RF, 0 with); on a dummy load it saw only near-field chip RF. Root cause: a
bare `REG_30` write lights the modulator but never the **external PA rail** (`REG_33` GPIO1
`PA_ENABLE`) or the **PA bias** (`REG_36`) — those are simply not in radio-server's register
writes. The TX mirror of the F3a RX gap. The fix: `dock.c` edge-detects the `REG_30` TX-enable bit
and calls `Dock_ForceTx`/`Dock_EndTx` (`uart.c`), adding exactly the PA steps stock
`RADIO_SetTxParameters` does — `PrepareTransmit` (REG_50/37/52) → `PickRXFilterPath` →
`ToggleGpioOut(PA_ENABLE, true)` → `SetupPowerAmplifier(TXP_CalculatedSetting, freq)` — and dropping
them (bias→0, then PA-enable off) plus re-opening RX audio on un-key. Host protocol core untouched
on the wire; the dock host tests go **19→31 checks**.

Flash the **F5** build (`f4hwn.fusion.v5.7.0.f5-dock-force-tx.bin` from the
`radio-server-f5-v5.7.0` pre-release; verify its `SHA256SUMS` first). **Kris present for every key;
dummy load mandatory during iteration; antenna only for the final range proof.** Then, in order:

1. **Register keying still passes (no regression):**
   ```
   uv run radio-server doctor --backend uvk5 --key-test        # type CONFIRM when prompted
   ```
   `REG_30` read-back keyed (`0xC1FE`). (A first-attempt settle flake — `0xBFF1`, retry passes —
   was seen in F4; the F5 `SYSTEM_DelayMs` settle points target that. Note if it recurs.)
2. **The RF now radiates — the F5 acceptance number.** Both radios on **UHF 445.800** (the kv4p is
   UHF-only), dummy load on the UV-K5. On the server run the carrier watch, then key a tone:
   ```
   python3 /tmp/dual_tx_watch.py 60        # correlates UV-K5 transmitting vs kv4p carrier
   uv run radio-server doctor --backend uvk5 --tx-tone --seconds 5 --freq 1000   # CONFIRM
   ```
   **PASS = kv4p carrier `True` while the UV-K5 is keyed** (`polls keyed WITH RF > 0`), where F4
   showed 0-with / 9-without. `/tmp/kv4p_audio_probe.py 10` should show a ~1000 Hz tone in the
   modulation.
3. **Browser TX + a service is heard:** browser **Talk** → a second HT (or the kv4p probe) hears
   voice; select a service (e.g. `01#` station-id) → its announcement is heard. This closes F4
   symptoms (2) and (4).
4. **RX survives a TX cycle:** after keying, `doctor --rx-noise` must **still** read thousands —
   `Dock_EndTx` re-runs the F3a RX force-open, so a service announcement doesn't leave the receiver
   deaf.
5. **Final range proof only:** swap the dummy load for the antenna; the HT across the room now
   hears the browser TX and the service. (⚠ verify-on-bench: which `OUTPUT_POWER` level dock TX
   radiates — `Dock_ForceTx` uses `gCurrentVfo`'s calibrated setting.)

RF guards and TOT are untouched; the PA is strictly slaved to `REG_30` and forced off at `0x0870`
enter and `0x0871` exit (a host crash mid-key is covered by the existing TOT, not this change).

---

## Notes / open items
- Once Kris confirms the five `⚠ CONFIRM AT BENCH` items, replace each placeholder with the
  real value and delete the provenance banner.
- Record any V3-specific surprises here as they're found, so the next flash inherits them.
- **F5 verify-on-bench:** the `OUTPUT_POWER` level and `gCurrentVfo` frequency source used for the
  dock-TX PA bias — see the `Dock_ForceTx` comment in `App/app/uart.c`.
