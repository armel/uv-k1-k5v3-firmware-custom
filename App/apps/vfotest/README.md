# VFO Test (overlay app)

A test app for the `vfo_stage` service (API level 3): each key stages one VFO
setting through `vfo_stage`, the app returns, and the result shows on the main
screen. It makes no transmission. Like Handoff A/B, it is an API test app:
`compile-app.sh all` does not build it.

## Requirements

| Item | Why |
|---|---|
| A firmware with `vfo_stage`: Labs or Fusion_ built from this branch (both have the BEAM bridge the service uses) | Without it the app shows `NO VFO_STAGE IN THIS FIRMWARE` and waits for EXIT |
| **Sig Finder** installed, under its own name `Sig Finder` | Case 3 hands off to it by name. Without it the case shows `NO SIG FINDER` and returns to the main screen on 121.500 MHz AM, which still tests the staging |
| A memory channel (any frequency, receive only) | Only for the memory-mode check: the radio cannot switch to memory mode without one |
| Dual watch and cross-band settings (menu) | Only for the DW and XB checks |
| Optionally K5Viewer | To watch the screens from a computer; not needed |

## Build and install

```sh
./compile-app.sh vfotest      # -> build/Apps/VFOTest.app
```

Install `VFOTest.app` (and Sig Finder) with UV Studio, then open the Apps menu
(F + 7) and run **VFO Test**.

## Safety

No case transmits: the app only sets the VFO, and the radio stays in receive.
The firmware's own TX checks still apply (121.500 MHz comes back with its TX
lock). Two cases leave the VFO with a split (8: TX 145.000 MHz, 9: TX 433.500
MHz) or a TX tone: put your usual setting back before pressing PTT.

## Cases

Each case starts from the current VFO (`beam_get`), so the fields it does not
set keep their values. After a staging the app shows the result for 1.5 s, then
returns, and the Apps menu hands back to the main screen.

### Page 1

| Key | Stages | Expected |
|---|---|---|
| 1 | 145.500 MHz, FM, wide | main screen `145.500`, `FM`, `W` |
| 2 | 433.500 MHz, FM, narrow | `433.500`, `FM`, `N` (or `N+`); the band changes by itself |
| 3 | 121.500 MHz, AM, then a hand-off to Sig Finder | Sig Finder opens already on `121.500`; on EXIT, the main screen on `121.500`, `AM`, with the TX lock |
| 4 | 145.500 MHz, then 433.500 MHz in the same run | `1ST TRUE`, `2ND FALSE OK`; the VFO ends on `145.500` |
| 5 | 3.000 MHz | `3.000 FALSE OK`, `STILL HERE`: refused, the app stays |
| 6 | 145.500 MHz, CTCSS 88.5 Hz on RX and TX | `CT` and `88.5` |
| 7 | 145.500 MHz, DCS 023N on RX and TX | `DC` and `023N` |
| 8 | RX 145.600 MHz, TX 600 kHz below | `145.600` with `-`; readback TX `145.00000` |
| 9 | RX 145.500 MHz, TX 288 MHz above | `145.500` with `+`; readback TX `433.50000` |
| 0 | nothing: reads the current VFO back | see below |
| EXIT | nothing | back to the Apps menu, VFO unchanged |

### Page 2 (key F, again to come back)

| Key | Stages | Expected |
|---|---|---|
| 1 | 145.5125 MHz with a 25 kHz step | `145.51250` and `25.00K`: the frequency is kept, also after a reboot |
| 2 | 145.500 MHz, CTCSS 88.5 Hz on TX only | readback `C0/0 T1/8` (no RX code, TX tone 88.5) |
| 3 | 145.500 MHz, CTCSS 67.0 Hz on RX, 88.5 Hz on TX | `CT 67.0`; readback `C1/0 T1/8` |
| 4 | 145.500 MHz, DCS 023I (inverted) | `DC` and `023I` |
| 5 | 145.500 MHz, output power MID | `M` |
| 6 | 144.300 MHz, USB | `144.300`, `USB` |

### Readback (key 0)

```
145.60000 R          RX frequency (rx_freq)
145.00000 T          TX frequency (tx_freq), after the firmware applied the offset
OFF 0.60000 -        offset and direction (- down, + up)
C0/0 T0/0 FW P1 S5   RX code type/index, TX code type/index, modulation
                     (F, A, U) and bandwidth (W, N), output power, step
```

Code types: 0 off, 1 CTCSS, 2 DCS, 3 inverted DCS; the index is the firmware's
table (CTCSS 0 = 67.0 Hz, 8 = 88.5 Hz; DCS 0 = 023).

## Radio states

Run one simple case (page 1, key 1 or 2) in each state and check the other
VFO and the setting are kept:

| State | How | Expected |
|---|---|---|
| VFO B active | F + 2 on the main screen | VFO B changes, VFO A is unchanged (F + 2 to check) |
| Memory mode | a memory channel, then F + 3 | the radio comes back in VFO mode on the staged frequency; the memory is unchanged (F + 3 to check) |
| Dual watch | menu | only the active VFO changes, `DW` stays |
| Cross-band | menu | only the active VFO changes, `XB` stays |
| Off-grid after a reboot | page 2, key 1, then power off and on | still `145.51250` |
