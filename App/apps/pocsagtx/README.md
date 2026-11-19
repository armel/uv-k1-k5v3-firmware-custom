# POCSAG TX (overlay app)

> [!WARNING]
> **Hardware Limitation**: The BK4829's FSK modulator is AC-coupled. It cannot hold deviation through the long runs of identical bits required by POCSAG's NRZ encoding. While the preamble transmits perfectly, standard pagers will not decode the data payload on this hardware.

Transmits **POCSAG** pages (CCIR Radio Paging Code No. 1, ITU-R M.584) on the
TX VFO at 1200 bauds — the rate DAPNET sends at (439.9875 MHz). It is the
transmitter counterpart of **POCSAG RX**.

One page is an address (RIC, 21 bits, 0–2097151), a function (0–3) and a text
of up to 45 characters. Function 0 is a numeric page (4 bits a character), the
others alphanumeric (7-bit ASCII). The app builds the POCSAG batches itself —
the 576-bit preamble, the sync codeword `0x7CD215D8`, the BCH(31,21) codewords
with even parity — exactly as `test/pocsag.py` does.

## Modulation

POCSAG is direct FSK: the carrier itself keys between two frequencies. This is
done by the **BK4819's own FSK modem** (the same hardware BEAM / AirCopy use),
not by rewriting the PLL. The CPU packs the whole page — preamble, sync codeword
and batch codewords, MSB first — into the FSK TX FIFO (`REG_5F`, 16-bit words),
sets the frame length (`REG_5D`), then triggers the modem (`REG_59`). The modem
clocks the bits out at the baud rate on its own; the CPU never touches the PLL
per bit (a PLL re-lock needs ~2 ms, far longer than the 0.833 ms a 1200-baud
bit lasts).

The register sequence is the one `driver/bk4829.c` `BK4819_SetupAircopy` uses
(the BK4829 is this radio's chip), with the DAPNET deviation added:

- `REG_70 = 0x00C3` enable Tone2 (the FSK modulator)
- `REG_72 = 0x3065` 1200 baud (the only rate this modem does — 2400 was tried
  and abandoned in the driver, see the comment above `BK4819_SetupAircopy`)
- `REG_58 = 0x00C1` FSK 1.2K TX
- `REG_5C = 0x5625` AirCopy's frame config with the CRC option (bit 6) off —
  POCSAG carries its own BCH, so no external CRC
- `REG_5D` frame length in words (`bytes - 1`)
- `REG_5E = 0x3204` extra FSK config
- `REG_40 = 0x1470` ±4.5 kHz deviation (the DAPNET value, from the
  [DAPNET uv-k5 wiki](https://hampager.de/dokuwiki/doku.php?id=uvk5))
- `REG_51 = 0` clears any CTCSS/DCS so nothing rides under the FSK

A "1" bit is the lower frequency, a "0" the higher, matching what POCSAG RX
reads from the discriminator.

## The 45-character limit

The BK4819 FSK TX FIFO holds 128 words (256 bytes). One POCSAG page needs
36 words of preamble plus 34 words per batch (sync + 16 codewords). Two batches
are 104 words — fits. A 46-character message can land a RIC in frame 7, which
needs a third batch (138 words) and would overflow the FIFO. The app therefore
caps the message at 45 characters, which always fits one FIFO load. Streaming
a longer page (refilling the FIFO as the modem drains it) is a later enhancement.

## Use

Tune the VFO (FM; 439.9875 MHz for DAPNET, or any 2 m / 70 cm frequency your
licence allows), then launch.

| Key | Action |
|---|---|
| `UP`/`DOWN` | Select a field (on UV-K1: `LEFT`/`RIGHT`) |
| `MENU` | Edit the selected field |
| `PTT` | Send the page |
| `EXIT` | Quit (in the editor: back); during a send: abort |

In the message editor:

| Key | Action |
|---|---|
| `0`–`9` | Multi-tap letters/digits |
| `*` | Cycle ABC / abc / 123 |
| `F` | Backspace |
| `UP`/`DOWN` | Move the cursor |

RIC, function and message are saved on exit.

## Status

| Part | State |
|---|---|
| Format (`test/pocsag.py`) | Self-tested; this app's encoder mirrors it |
| Encoder | Mirrors `pocsag.py` bit for bit (address in its frame, message bits LSB-first running across the 20-bit fields, numeric space-padding, trailing idle word) |
| FIFO packing | `test/tx_model.py` checks the 16-bit FIFO word list expands to the exact POCSAG bit stream and stays under 128 words |
| FSK modulation | BK4829 hardware FSK FIFO at 1200 baud, ±4.5 kHz (`REG_40`), AirCopy register set |
| On air | Not tested yet — needs a receiver (POCSAG RX, a DAPNET pager, or `pocsag.py`) |

## Verify

`./compile-app.sh pocsagtx` builds the blob. `test/tx_model.py` cross-checks the
encoder and FIFO packing against `pocsag.py`, the reference shared with POCSAG RX.
