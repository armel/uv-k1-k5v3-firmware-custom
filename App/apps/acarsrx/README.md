# ACARS RX

Receive-only VHF ACARS overlay for the Fusion firmware. Its presentation and
controls follow APRS RX: a compact status bar, the tuned frequency, signal
level, scrollable payload text and a three-message history.

## Use

1. Select an AM channel on a local ACARS frequency.
2. Launch **ACARS RX** from Apps.
3. Use **UP/DOWN** to scroll a long message and browse the last three messages,
   **\*** to switch between the normal and compact views, **1** to enable or
   mute the speaker, **F** to switch between decoded and raw payloads, **2** to
   clear the history and counters, and **EXIT** to leave. The selected display
   size and speaker state are saved on exit. To avoid noise bursts while
   waiting, the speaker opens only after a complete ACARS header is recognized.

For Paris, start with **131.525 MHz**, then try **131.725 MHz** and
**131.825 MHz**. Reception depends on local traffic, antenna and obstructions.

The normal view uses the regular 18-character font and shows three payload
rows. The compact view uses the 32-character tiny font and shows four rows, as
in APRS RX. The header shows the aircraft address and flight identifier. A
leading `.` used to pad a six-character registration to the seven-character
ACARS address field is hidden. The label and printable application text follow
below. On downlinks, the four-character message sequence and duplicated flight
identifier are hidden; short blocks are shown from their first payload byte.
ACARS payloads are terse operational messages; many are not intended to be
human-friendly.

The bottom line shows the captured signal level, `RX` for the total number of
valid frames received since launch and `FIX` for the subset that required
parity-bit repair before passing the BCS check.

The decoded view identifies link tests (`Q0`), Media Advisory (`SA`), MIAM
(`MA`) and ARINC 622 ATS payloads (`A6`, `AA`, `B6`, `BA`). For `H1`, it also
extracts the standard sublabel and optional Message Function Identifier, for
example `SUB M1 MFI B6`, before showing the remaining application data. Unknown
labels fall back to their raw payload. This deliberately does not claim to
decode proprietary airline fields or the complete ASN.1 ARINC 622 payload.

While no valid message has been accepted, the main payload area stays clear and
the row below the frequency reports the ADC audio peak-to-peak level and three
counters: `SYN` for detected ACARS headers, `CRC` for checksum failures and
`PAR` for parity failures. Counters above 99 are displayed as `99+` so the
line always fits. A changing ADC value with `SYN 0` means audio reaches the
application but the modem has not locked.

## Decoder

The BK4829 supplies AM audio but does not decode ACARS. The overlay samples PA4
at 19.2 kHz, mixes the 1800 Hz MSK centre to complex baseband and applies a
half-sine matched filter.

- **Carrier loop.** The MCU runs from its internal RC oscillator, so the sample
  clock may be 1 % off and the tones appear up to 18 Hz away. The loop is wide
  and its integrator fast until the text starts, then both narrow.
- **Bit clock.** The pre-key is a pure tone and carries no bit timing. Once
  data starts, the quadrature rail of each decision is compared with the
  symbols on either side of it; its sign tells an early clock from a late one
  and steps the clock by 1/6 sample before the text, 1/24 sample inside it.
- **Synchronization.** The receiver slides bit by bit onto `SYN SYN` in either
  polarity, which leaves `+` and `*` for the bit clock to settle, then expects
  `SOH`. It accepts ETX and ETB endings and checks odd parity and the complete
  16-bit BCS. Up to two parity-marked bit errors are repaired only when the
  corrected message also has a valid BCS.
- **Slot work.** A leaky run counter follows the pre-key: noise decays to
  zero, the tone ramps up. While it is up, or a block is being received,
  nothing else runs. The key scan costs about four samples, so it waits for
  the modem only; the screen and the battery also wait one second after the
  BK4829 reports an open squelch, unless a key asked for a redraw. Nothing
  waits longer than three seconds.

`test/model_rx.py` mirrors the receiver line for line and is the place to try
any change first. Its tests cover every sample offset of the bit clock against
eight carrier phases, a sample clock from -1.5 % to +1.5 % on a full-length
block, a weak signal, a frame after one second of open-squelch noise, parity
repair, BCS rejection, and the share of idle slots the modem claims.

```sh
python3 -B test/model_rx.py
```

The model and the Flipper files share one transmitter, written from the ACARS
description. A block received off the air has not been checked against them
yet.

## Flipper Zero bench frames

`test/flipper_acars.py` generates Sub-GHz RAW files in `test/flipper/`. The
stock Flipper cannot transmit on the 118-137 MHz aeronautical band, so the bench
files use **433.650 MHz** and OOK. The keyed carrier follows the sign of the
ACARS MSK waveform; the radio's AM detector recovers the square-wave audio and
its audio filter keeps the useful fundamental. The bench waveform uses a
32-byte pre-key to tolerate the extra distortion introduced by OOK; the host
decoder tests continue to cover the standard 16-byte pre-key.

1. Copy the six `.sub` files from `test/flipper/` into `subghz/` on the
   Flipper Zero.
2. Tune the radio to **433.650 MHz AM** and launch ACARS RX.
3. Open `Sub-GHz > Saved` on the Flipper and send one file at a time.

| File | Expected result |
|---|---|
| `acars_basic.sub` | Displays `FLIPPER ZERO ACARS TEST` |
| `acars_long.sub` | Displays a scrollable multi-row message |
| `acars_h1_arinc.sub` | Displays `SUB M1 MFI B6`, `ARINC 622` and the remaining data |
| `acars_etb.sub` | Accepts an ETB-terminated block |
| `acars_repair.sub` | Displays after repairing one parity-marked bit |
| `acars_badcrc.sub` | Remains hidden and increments `CRC` |

Regenerate them with:

```sh
python3 -B test/flipper_acars.py test/flipper
```

Use only a frequency permitted by the Flipper's configured region and keep the
test brief and local. The script rejects the live aeronautical band and values
outside the built-in CC1101 operating ranges.
