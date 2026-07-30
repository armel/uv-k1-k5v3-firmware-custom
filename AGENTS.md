# AGENTS.md

Guidance for AI agents and developers working in this repository. For the human-facing documentation,
start at [README.md](README.md); for the protocol, [PROTOCOL.md](PROTOCOL.md).

## Overview

This is **F4HWN with one addition: a dock control mode** — a serial protocol that lets a host computer
read and write the radio's BK4819 registers, take the radio over, and hand it a whole channel to tune
itself to. Everything else is upstream's.

- **Radio:** UV-K1 / UV-K5 **V3**, the **PY32F071** MCU. Not a classic UV-K5 (DP32G030).
- **Base:** [`armel/uv-k1-k5v3-firmware-custom`](https://github.com/armel/uv-k1-k5v3-firmware-custom)
  tag `v5.7.0` = commit `3bd3ebb`, Apache-2.0. The `Fusion` preset is the one that matters.
- **Consumer:** [radio-server](https://github.com/kbennett2000/radio-server), though the protocol is
  documented and has no dependency on it.

C, CMake, ARM GNU Toolchain 13.3, built in Docker.

## Build

```sh
./compile-with-docker.sh Fusion       # -> build/Fusion/f4hwn.fusion.bin
```

**The script passes `docker run -it`, so it fails without a TTY.** Headless, run it directly:

```sh
docker build -t uvk1-uvk5v3 .
docker run --rm -u $(id -u):$(id -g) -v "$PWD":/src -w /src uvk1-uvk5v3 \
  bash -c "cmake --preset Fusion && cmake --build --preset Fusion -j"
```

The dock mode is behind `ENABLE_DOCK`, enabled in the Fusion preset. Flash region is 118 KB and the
build sits around 87% of it — check the linker's report before adding anything large.

**Builds are reproducible up to a timestamp.** The firmware embeds its build time, so two builds of
the same commit differ in ~5 bytes. Compare with `cmp -l` before concluding a tree differs.

## Test

```sh
make -C tests/host run       # 98 checks; needs only a C compiler, no Docker, no hardware
```

`App/app/dock.c` is deliberately **pure C with no firmware or hardware includes** — all hardware sits
behind a caller-supplied `dock_hal_t`. Keep it that way: it is what lets the protocol core be tested
on a development machine, and what lets third parties lift it as a reference decoder.

**Every protocol change needs a host test, including a byte-exact frame** where the wire changes.
`tests/host/test_dock.c` holds golden vectors that are the oracle for anyone implementing a client.

## The contract that governs everything here

**The dock wire protocol must stay byte-compatible with
[radio-server](https://github.com/kbennett2000/radio-server)'s `radio_server/backends/uvk5/frames.py`.**
That is not a preference — it is the reason this port was done as a port rather than a redesign, and
it is what keeps one host program working against both this firmware and nicsure's classic-UV-K5 Dock.

- A change that alters bytes on the wire is a **cross-repo change**. Nothing checks the two
  implementations stay in step (radio-server ADR 0148 records this as open), so it is on you.
- New commands are additive and must be **ignored silently by older firmware** — that is how a host
  detects firmware level.
- `PROTOCOL.md` is the spec. If you change the wire, change it, and re-verify its golden vectors
  against **both** implementations.

## Where the reasoning lives

**Mostly in the other repository.** Only `adr/0001-dock-force-tx.md` (F5) is local. The rest —
why the fork exists, the protocol port, the RX audio fix, the set-VFO decision — are radio-server
ADRs **0118, 0119, 0120, 0126, 0140, 0142** and its `docs/HANDOFF.md`. Read them before changing the
dock code; several document failure modes that took a bench cycle each to find.

## Firmware levels

Releases are tagged `radio-server-fN-v5.7.0`; branches `fN-*` are kept for history. `main` is the tip.

| Level | Adds | Symptom without it |
|---|---|---|
| **F2** | dock mode: `0x0850`/`0x0851`/`0x0870`/`0x0871` | commands silently ignored |
| **F3** | forces the RX audio path alive on `0x0870` | connects, receives **silence**, all registers read back correct |
| **F5** | engages the PA on the key-up edge | keys cleanly, **radiates nothing usable** |
| **F6** | `0x0873`/`0x0874` set-VFO | tuning does not survive `0x0871`; no power control |
| **F7** | `0x0877`/`0x0878` set-modulation; `0x0873` stops forcing FM | the radio is FM-only — no way to receive AM |

None of F3 or F5's absence looks like a fault from the host — the radio reports success and does
nothing. When something is silent, check the level first.

## Guardrails (do not violate)

1. **Do not fill in `BENCH.md`'s `⚠ CONFIRM AT BENCH` placeholders from inference.** Every one of
   them — flashing (DFU entry, the FTDI cable, the tab-conflict gotcha, the calibration dump), the
   resume-RX behaviour after `0x0871`, and F7's AM receive and PTT-refusal items — is marked because
   **nobody has confirmed it on the radio**. A plausible guess written as fact is worse than the
   placeholder — this is firmware that can brick a radio. Replace a marker only with a bench result.
   Do not restate the count either: it was wrong in both files before F7 added to it.
2. **Validate before acting; refuse, never clamp.** Every `0x0873` field is checked before anything is
   written, and a bad one is refused with a status code. This is load-bearing twice over: a silently
   moved channel can transmit on somebody else's repeater, and the refusal-before-action property is
   what makes an empty `0x0873` a **safe firmware-level probe** for hosts. `FREQUENCY_GetBand()`
   *clamps*, which is why `Dock_FreqInBand()` re-checks.
3. **Any non-zero reply status blanks the frequency fields** — enforced in `dock.c`, not left to each
   HAL, so no binding can publish a channel the radio is not on.
4. **Keep `dock.c` free of firmware includes.** Hardware goes behind `dock_hal_t`.
5. **Touch as little of upstream as possible.** Thirteen files differ from `3bd3ebb`; four of them are
   upstream files that gained a dispatch case, a HAL binding and a build flag. Nothing in the radio's
   own operation changes until a host sends `0x0870`. Keep it that way — it is what makes rebasing on
   a new F4HWN release tractable.
6. **Licence hygiene.** Apache-2.0. The dock port derives from nicsure's Apache-2.0 `quansheng-dock-fw`;
   its GPL-2.0 Windows client is read as a specification and **never** copied. Record derivations in
   [NOTICE](NOTICE).

## `.gitignore` will silently swallow your documentation

Upstream's `.gitignore` ignores **`/docs`**, **`.claude`** and **`.agents`**, and it used to ignore
**`AGENTS.md`** (this fork un-ignores that one deliberately — keep the divergence when rebasing).

`git add -A` skips an ignored path **without a word**, so a doc written into `docs/` commits as
nothing, the push succeeds, and you find out when somebody follows a link to a file that was never
there. That is exactly how the first version of this file failed to land.

**Put documentation at the repo root** (`PROTOCOL.md`, `BENCH.md`, `AGENTS.md`), and after any commit
that adds a file, check it is actually in the commit:

```sh
git show --stat --name-only HEAD
```

## Layout

- `App/app/dock.c`, `App/app/dock.h` — **ours**: the pure protocol core.
- `App/app/uart.c` — upstream, plus our dispatch cases and `Dock_*` HAL glue.
- `App/CMakeLists.txt`, `CMakePresets.json` — upstream, plus `ENABLE_DOCK`.
- `tests/host/` — **ours**: the host harness.
- `PROTOCOL.md`, `BENCH.md`, `NOTICE`, `adr/` — **ours**.
- Everything else is F4HWN's. Leave it alone unless you are rebasing.
