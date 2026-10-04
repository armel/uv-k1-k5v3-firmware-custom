# APRSTX-R: resident-assisted position overlay

This position-only overlay depends on the companion **resident AFSK API level
2** change. It requires ABI 1, API >=2 and `APP_CAP_AFSK_TX` (`0x00000008`).
Stock v6.0.0 rejects the `.app` header before executing it. The runtime checks
also reject a short API table or missing extension callbacks. This PR changes
neither the resident firmware nor its canonical SDK/packer.

The app contains no MMIO accesses, interrupt handlers, tone writes or private
resident symbol references. It builds a complete AX.25 UI frame with FCS in
its overlay buffer, submits it with the selected channel's opaque token, then
polls the resident service. Submit must copy the frame/options before returning.
Symbol timing, quiet TX preparation, physical PTT release detection, permission
checks, RF ownership and the 2 second PA-on ceiling belong to the resident API.
Cancellation runs after every attempt and on app exit; the companion loader
also cancels before RX restore and deferred flash commit.

## Build and install

From the repository root:

```sh
./compile-app.sh aprstx-resident
bash tests/aprstx-resident/run.sh
```

The output is `build/Apps/APRSTX-R.app`. Install with UVStudio in one of the
eight visible app slots, and launch from Apps. Installing replaces that slot's
previous app and settings. Use firmware containing the resident extension.

`resident_api.h` wraps the unchanged API1 prefix when building against the
baseline SDK, or uses the canonical API2 header after the resident PR merges.
The ARM build asserts offsets 284/296 and the 20/16/16 byte wire types. The local
packer imports the canonical file-format constants, and always emits API2 and
capability 8; it cannot silently downgrade the app for stock firmware.
`protocol.c/h` and `ui.c/h` are identical shared sources with the stock overlay
PR. No stock backend or direct hardware access is linked into APRSTX-R.

## Operation

Configure the selected radio TX channel before opening the app. Its actual TX
frequency is displayed. APRS transmission requires FM and a simplex RX/TX
snapshot. Rejected offset/reverse snapshots show both RX and TX frequencies.
The app selects no universal APRS frequency, output power or repeater path.

On first use, enter your callsign, SSID, latitude, longitude, path and symbol.
MENU opens setup, arrows select a field and MENU starts/accepts its draft.
Draft text/numeric fields start empty; EXIT discards a draft or returns from
setup. Callsigns accept six uppercase letters/digits: 2..9 use a 700 ms
multi-tap window and F switches to digits. STAR deletes. Coordinate inputs are
six digits `DDMMhh` for latitude and seven `DDDMMhh` for longitude; F changes
N/S or E/W. Minutes >=60 and coordinates beyond +/-90 or +/-180 are rejected.
Path values are 0 direct, 1 WIDE1-1, 2 WIDE1-1,WIDE2-1 and 3 ARISS. Direct is
the default. Symbol arrows select a printable character and F changes table.

Exit the app to stage a valid edited configuration in the existing 16 byte
ConfigV1 format. The loader commits it after returning from overlay RAM;
there is no external-flash write during app execution. Invalid or corrupt
configuration opens setup with empty callsign and unconfirmed coordinates.

After all keys have been released for 60 ms, press and hold PTT to send one
position frame. Starting with PTT held, leaving setup, or continuing to hold
PTT after completion cannot send another frame. Release and make a new press
for another attempt. Releasing PTT or pressing another key cancels waiting or
an active attempt. The resident service independently checks physical PTT.

CCA uses the selected simplex receiver's RSSI below `APRS_CCA_DBM` (default
-110 dBm) continuously for 200 ms, with nominal 100..400 ms backoff after busy
samples. The attempt is bounded to 250 foreground slices of 20 ms, about 5
seconds plus callback overhead. RSSI is a laboratory heuristic, not AX.25 DCD;
calibrate its threshold for the receiver and noise floor. Resident permission,
BCL, battery and channel-token checks run again before PA-on. No LCD, audio,
RSSI, battery or backlight callbacks run while the resident service owns RF.
`SENT` means waveform completion; it does not imply reception or decoding.

## Validation and remaining bench work

The GNU ARM 13.3.rel1 Docker build uses `-Os -flto -fstack-usage`. The linker
asserts the complete code/rodata/data/bss workspace against 4096 bytes at
`0x20000280`, not just file length. The measured build is **3948 bytes**, leaving
148 bytes. This is below the LLD's desired 384 byte reserve; optional messages
and status text are omitted. `.map`, ELF and LTO `.su` reports are generated.
The largest reported app frames are 56 bytes each for entry and drawing, and
32 for coordinate formatting; resident/API/loader/ISR stack usage is additional.
Only a hardware stack watermark can establish the real available margin.

Host tests exercise short/missing API guards, frame FCS and submission options,
200 ms quiet CCA, busy timeout, simplex/FM rejection, channel-token refusal,
release during CCA and active TX, terminal timing errors, foreground timeout,
held-on-entry PTT, setup exit, success while held, repeated fresh presses,
corrupt config, deferred configuration staging, offset-frequency display and
API2/capability-8 header/CRC metadata. CI runs these tests and builds the ARM
artifact. Pure framing/config/editor vectors are also covered by the shared
protocol tests in the stock overlay PR.

No radio or RF bench validation has been performed. Before releasing, measure
tone frequencies/deviation/twist, timer lateness, software-SPI WCET, RF
frequency, PTT cancellation latency, channel-state restore and stack margin;
decode fixed frames with two independent receivers. The resident timer's
foreground-hang bound assumes interrupts remain enabled; arbitrary faults need
separately verified watchdog/reset or hardware PA protection.

The shared SDK and linker model originate from Armel F4HWN's Apache-2.0
repository; this implementation keeps Apache-2.0 notices. The shared pure
protocol implementation is written from the AX.25/APRS wire specifications.
