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

## Notes / open items
- Once Kris confirms the five `⚠ CONFIRM AT BENCH` items, replace each placeholder with the
  real value and delete the provenance banner.
- Record any V3-specific surprises here as they're found, so the next flash inherits them.
