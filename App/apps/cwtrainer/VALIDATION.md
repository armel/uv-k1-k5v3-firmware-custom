# CW Trainer 0.3.3 validation

## Build and compatibility

ARM GCC 13.3.Rel1, Cortex-M0+, ABI1/API3, VMA 0x20000280. The existing repository
build and packer are used unchanged. Direct ARM compilation was executed;
the Docker wrapper/CI was not executed in the preparation environment.

| Resource | Bytes |
|---|---:|
| Overlay including BSS | **4,076 / 4,096** |
| BSS included above | **160** |
| Code/constants/alignment excluding BSS | **3,916** |
| Overlay margin | **20** |
| Read-only assets | **1,488 / 3,840** |
| Persistent configuration | **12 / 16** |
| Complete package including 64-byte header | **5,628** |

The assets are 21 strings of 16 bytes and three 384-byte font-derived command
bitmaps. They contain data only. There is no additional framebuffer or heap.
Own-function drawing stack estimate: 136 bytes, excluding resident calls and
interrupts; no real on-radio stack peak measurement is claimed. Changing the
compiler requires remeasuring the overlay. All 25 used API pointers are checked
individually within the prefix ending at ARM byte 356. API3 tables of 368, 380
and 384 bytes are supported. No extra capabilities or firmware changes.

Reference package SHA256:
`bb6f1e152ed261a2867afe27b83cfe6dbe46b2995c9a943e35f58494dbb2f7f5`.
Code CRC32: `49ce546b`. The build script regenerates the report and package.

## Automated tests

**2,489 cases pass in each of five variants:** complete mock API, actual resident
audio bodies with simulated hardware I/O, and truncated C API tables of
368/380/384 bytes. The suite is repeated, not 12,445 independent cases.

Run `python App/apps/cwtrainer/test/run.py` from the repository root (Python 3
standard library and host GCC; HOST_CC can select the compiler). Generated logs,
JSON and captured framebuffer PNGs are written under `test/out/`, ignored by Git.
Assertions and -Werror are enabled; UBSan is used if installed, otherwise -ftrapv.

Coverage includes STRAIGHT/IAMB A/B, A/S/O/C/SOS at 5–60 WPM, contact filtering,
iambic memory, first element after idle, audio-write latency of 50/500 us,
natural clock wrap, 1,344 RX spacing combinations with unchanged DIT/DAH and
intra-character timing, scoring, correction sounds, settings migration, missing
services, physical-key holds and transitions, F/repeat controls, MENU/EXIT,
continuous text and CLEAR. RX TRAIN SIDE1 is inert; 7 generates/plays, PTT replays
the same target. RX>TX SIDE1 retains replay. A real V0.2 source fixture reproduces
the old A-to-ET timing defect as a negative control.

Real resident font/helper/LCD bodies generate simulated SPI refreshes compared
with all 1,024 framebuffer bytes. The ready label is checked as exactly PTT RPLY
in tiny font; revealed Morse retains its normal font. Command bitmaps are
compared with the resident renderer. RF callbacks remain NULL and resident
audio bodies are audited: no PA GPIO activation and RF PA gain disabled. Direct
BK writes use only 0x70/0x71; PTT PB10 is read-only. These are software checks,
not physical RF measurements.

## Hardware trials reported by F4LPF

Tested on a real Quansheng UV-K5 V3 with a USB-C paddle: IAMB A and IAMB B,
TX FREE, RX TRAIN and RX>TX, ergonomics, generated audio and decoding.
A real straight key was **not tested**: STRAIGHT is validated only in software.
A second receiver tuned to the same frequency detected no RF during the trials.
This practical check is **not** an instrumental demonstration of absolute RF
absence and is **not an RF certification**. No additional measurement results
are inferred from it.

For further hardware verification, follow the README controls and adapter
requirements. Confirm 7 NEW/PTT replay and inactive SIDE1 in RX TRAIN, GAP at a
fixed WPM, A/B keying after idle, RX/TX setting independence, correction sounds,
held-key mode transitions, EXIT and USB restoration. Any instrumental RF check
should use an appropriate dummy load/attenuation and cover startup, PTT,
paddle, playback, correction and exit. Known limitations are in both READMEs.
