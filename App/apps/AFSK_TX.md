# Optional resident AFSK TX service (API level 2)

This extension provides bounded, manual AX.25 AFSK transmission for overlays.
It owns channel preparation, RF, TIM2, physical PTT cancellation and RX restore.
An overlay supplies a complete frame and polls the result; no overlay code,
callback or caller buffer is retained by the interrupt handler.

The `ENABLE_FEAT_F4HWN_OVERLAY_AFSK_TX` option requires overlay apps. It is enabled
in Labs and absent in the feature-off Fusion build. The capability is
`APP_CAP_AFSK_TX = 0x00000008`. ABI major remains 1; API level becomes 2. The
first 284 bytes of the API table remain unchanged, including `beam_draw` at
offset 280. Four callbacks append at offsets 284, 288, 292 and 296; the ARM
table is 300 bytes. Existing API1 apps retain their original callback layout.

Apps requiring AFSK must pack with `--api-min 2 --require afsk`. The loader
rejects unavailable capabilities. Before reading extension pointers, also
check ABI 1, API >=2, `api_size` covering `afsk_cancel`, and all four non-null
callbacks. Feature-off firmware does not advertise the capability.

## Public contract

Every caller supplies the exact structure size in its `size` field. The SDK
asserts stable pointer-free layouts; firmware-specific `VFO_Info_t` is private.

| Type | Bytes | Fields |
| --- | ---: | --- |
| `app_tx_info_t` | 20 | size, denial, flags, selected TX/RX frequencies in 10 Hz units, channel token, modulation, power, bandwidth, zero reserved byte |
| `app_afsk_opts_t` | 16 | size, flags, channel token, preamble flags, tail flags, tone gain, max PA-on milliseconds, zero reserved field |
| `app_afsk_status_t` | 16 | size, state, error, bits sent, PA-on microseconds, maximum lateness in 48 MHz input-clock cycles |

`tx_info(out)` snapshots the selected **TX VFO**, not an unrelated current
receive VFO, and returns the same public error as `out->denial`. Frequency,
modulation, power and bandwidth use stable SDK values. Flags are `BUSY=1` and
`SIMPLEX=2`. The opaque token changes when relevant selected-channel fields
change. The first implementation accepts FM simplex only. A token does not
grant permission: submit checks current TX restrictions, battery state, BCL
and physical PTT as well.

`afsk_submit(frame, length, opts)` accepts one job. The frame must contain
18..128 bytes including its little-endian AX.25 FCS, with reflected X.25 CRC
residue `0xF0B8`. Supply no HDLC flags or stuffed bits. APRS address/payload
validation belongs to the app. Submit copies the frame and options into
resident memory before returning. A caller may immediately reuse its buffers.

| Option | Accepted value | Typical value |
| --- | --- | ---: |
| flags | exactly `APP_AFSK_REQUIRE_PHYSICAL_PTT` (1); mandatory, unknown bits rejected | 1 |
| channel token | current `tx_info` token | snapshot token |
| preamble flags | 1..120 | 45 |
| tail flags | 1..10 | 3 |
| tone gain | 1..127; hardware calibration required | 66 |
| max PA-on time | 100..2000 ms | 2000 |
| reserved | zero | 0 |

An accepted submission returns `OK`; terminal outcome comes from
`afsk_poll(out)`. Poll takes a consistent snapshot and performs pending RX
restore after ISR PA-off. Poll frequency controls neither symbol timing nor
the PA-off deadline. RF ownership remains held until pending restore finishes.

`afsk_cancel()` synchronously disables PA, stops timing and completes restore.
It is safe to repeat. Call it on every app exit and cancellation path. The
loader also calls it before VFO restore and deferred external-flash commits.
SDK radio/audio services are guarded while AFSK owns RF to prevent foreground
and ISR software-SPI transactions from interleaving. Apps should limit active
TX foreground work to key input, poll and cancel, and redraw after cleanup.

States are `IDLE=0`, `ACTIVE=1`, `DONE=2`, `ABORTED=3`, `ERROR=4`. Errors are
`OK=0`, `BUSY=1`, `BAD_ARGUMENT=2`, `TX_DENIED=3`, `LOW_BATTERY=4`,
`HIGH_VOLTAGE=5`, `CHANNEL_CHANGED=6`, `PTT_RELEASED=7`, `TIMING=8`,
`TIMEOUT=9`, `CANCELED=10`, `INTERNAL=11`. These values do not reinterpret the
legacy `tx_state()` denial codes. `DONE` means waveform completion, not reception.

## Preparation, timing and cancellation

Preparation keeps PA off. It uses the selected snapshot's frequency, bandwidth
and calibrated power, disables microphone input and voice modifiers including
subaudio, VOX, scramble, compander and DTMF, and primes the hardware tone.
It avoids voice `PrepareTX`, PTT ID and roger behavior. The tone settles for
50 ms while quiet, with physical PTT checked every millisecond; total
preparation is limited to 100 ms. Permission, token and PTT are checked again
before enabling PA. After completion, cancellation or error, ordinary channel
RX registers are restored.

BCL samples the selected simplex receiver's live RSSI **before** preparation.
RX is stopped during quiet preparation, so the final permission check retains
that BCL observation; there is a roughly 50 ms observation gap before PA-on.
This is a conservative RSSI check, not decoded AX.25 DCD. An app may perform
additional simplex CCA before submitting, but should not claim continuous
receive observation during the quiet TX setup interval.

TIM2 is exclusive to this optional service. Acquisition rejects an already
enabled timer/IRQ, masked interrupts, or a clock tree whose real TIM2 input
and HCLK are not both 48 MHz. APB timer-clock doubling is accounted for.
TIM2 is a 16 bit free-running counter with `PSC=399`, `ARR=65535`: 120 kHz,
one tick = 8.333 microseconds. Overflow extends it in resident state. Compare
channel 1 advances by exactly 100 ticks per symbol, giving 1200 baud without
relative delay accumulation. TIM2 uses priority 0 like SysTick; it restores
the previous timer priority and releases the peripheral on cleanup.

The first tone is prepared with PA off, and the final symbol receives its full
interval before PA-off. An interrupt more than two ticks (16.667 microseconds)
late aborts; the modem does not send catch-up symbols. The maximum reported
lateness is in original 48 MHz cycles. After each tone write, the service
checks the absolute next deadline and requires more than two ticks of margin
before rearming compare; IRQ lateness and SPI duration consume the same budget.
A conservative reserve of one symbol
plus 1 ms is applied before the requested PA-on ceiling. Physical PTT release,
lateness, timeout and completion turn PA off in the timer path before deferred
foreground restore. Cancel performs PA-off before restore as well.

## Build checks and hardware acceptance

```sh
bash tests/afsk_tx/run.sh
./compile-firmware.sh Labs -DENABLE_FEAT_F4HWN_OVERLAY_AFSK_TX=ON
./compile-firmware.sh Fusion -DENABLE_FEAT_F4HWN_OVERLAY_AFSK_TX=OFF
./compile-app.sh All
```

The focused PR workflow runs host service/hardware-adapter tests, builds both
feature configurations and rebuilds legacy overlays. Tests cover validation,
copied buffers, permission/token/PTT rejection, ownership, final-symbol
duration, deadline reserve, lateness, timer clock/ownership and overflow,
quiet preparation, cancellation and cleanup ordering. Linker checks retain
the overlay VMA `0x20000280` and enforce firmware memory regions. Flash/RAM
cost is reported by each firmware build; no preliminary size estimate is a
release guarantee.

RF hardware validation remains pending. Measure software-SPI WCET, interrupt
latency and jitter, actual PA-off latency, tone frequency/deviation/twist,
selected TX frequency, modifiers and restored RX state. Decode fixed frames
with two independent receivers and establish a stack watermark with nested
interrupts. The two-tick tolerance and 1 ms reserve require bench evidence.

The timer can bound a stalled foreground while interrupts execute. Masked
interrupts or arbitrary faults can prevent that path. This change enables no
hard watchdog and provides no CPU-independent PA cutoff; reset/watchdog or
hardware fault protection requires separate implementation and measurement.
