# CW Mem overlay app

`CW Mem` is a message keyer for [`CW Keyer`](../cwkeyer/README.md) and
[`CW USB`](../cwusb/README.md). Press `7` in either keyer: CW Mem opens with
the keyer's settings (WPM, tone, break-in, crystal correction, speaker) and
its UP/DOWN offset; CW USB opens it only once armed. Keys `1` to `9` send stored
messages. Touch the key or paddle (or press `PTT` or `EXIT`) and you are back
in the keyer, on the same frequency, ready to answer by hand.

## Requirements and install

- F4HWN Fusion firmware with overlay ABI 1 and API level 3 (hand-off).
- `CW Keyer` and/or `CW USB` 1.6 or later, which open CW Mem with key `7`.
- Build: `./compile-app.sh cwmem`, output `build/Apps/CWMem.app`; install it
  in a free slot with UV Studio.
- Optional: [`CW Mem Edit`](../cwmemedit/README.md) to set the callsign and
  the custom messages of keys `7`, `8` and `9`.

Opened from the menu, CW Mem has no settings to use and nowhere to go back
to: it only says to open it from a keyer, and `EXIT` leaves.

## Messages

| Key | Message |
|---|---|
| `1` | `CQ CQ CQ DE <call> <call> K` |
| `2` | `<call>` |
| `3` | `5NN <NR>` |
| `4` | `TU <call>`; NR moves on once it is fully sent |
| `5` | `TU 73` |
| `6` | `QRZ?` |
| `7`, `8`, `9` | custom messages, set in CW Mem Edit (defaults: `UR RST 599 599`, `NAME`, `QTH`) |

- `<call>` is the callsign set in CW Mem Edit or, when none is set, the
  callsign of the boot message (LINE 1), as APRS TX does.
- `<NR>` is the serial number, sent with three digits at least (`001`), from
  1 to 9999. It is kept in CW Mem's configuration.
- A message is keyed with the keyer's timing: the same ramps and trims as the
  paddle keyer, 3-dot letter gaps and 7-dot word gaps. Characters without a
  Morse sign are skipped.
- Any other key stops a message. A refused transmission (`TX OFF`, as in the
  keyers) stops it too.

## Controls

- `1` to `9`: send a message.
- `0`: open CW Mem Edit (`NO EDIT` in the status bar if it is not installed).
- `F`, then `UP` / `DOWN`: lower or raise NR.
- `UP` / `DOWN`: tune by 100 Hz, as in the keyers; the offset goes back to the
  keyer.
- `MENU`: show or close the help page.
- `EXIT`, `PTT`, or the key or paddle of the keyer that opened CW Mem
  (`SIDE1` / `SIDE2` for CW Keyer, the USB-C paddle for CW USB), held for about
  4 ms: back to that keyer.

The status bar shows the keyer's speed (`18 WPM`), which CW Mem sends at.
The main screen shows the frequency, the S-meter and nine capsules, one per
message key. Receive audio and the semi-break-in return work as in the
keyers.

## Going back to the keyer

CW Mem hands back the keyer's settings unchanged and the UP/DOWN offset. CW USB
hands over only once armed (key `7` does nothing before), so it comes back
armed and the paddle answers at once. Switching apps takes
the loader's launch time. The loader waits for the keypad (`SIDE1` / `SIDE2`
included) to be released before it starts the keyer; a USB-C paddle still held
when CW USB starts again keys at once.

## Storage

- The callsign and the three custom messages are a 110-byte block at the start
  of CW Mem's assets (`cwmem_user.h`). CW Mem only reads it; CW Mem Edit
  writes it through the configuration proxy, as APRS Edit does for APRS TX.
  Installing or updating CW Mem with UV Studio resets them to the defaults.
- NR and the keyer to go back to after CW Mem Edit are in CW Mem's own
  configuration.

## Contributing

CW Mem is a third build of the CW Keyer source: `cwmem_app.c` sets
`CW_KEYER_MEM` and includes [`../cwkeyer/cwkeyer_app.c`](../cwkeyer/cwkeyer_app.c)
for the receive path, TX keying, sidetone, S-meter and status bar, then adds
its own screen, keys and `app_main`. Its texts and tables come from
[`../cwkeyer/gen_assets.py`](../cwkeyer/gen_assets.py) (`CW_KEYER_MEM=1`). A
change to the shared source must be built and tested in all three apps:
`./compile-app.sh cwkeyer cwusb cwmem`.

## Credits

- CW Mem: Johan Denoyer F4WAT ([@jdenoy](https://github.com/jdenoy)).
- Keying, receive and display code shared with CW Keyer: Armel F4HWN
  ([@armel](https://github.com/armel)) and contributors (see the CW Keyer and
  CW USB READMEs).
