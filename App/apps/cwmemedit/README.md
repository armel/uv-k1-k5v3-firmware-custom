# CW Mem Edit overlay app

`CW Mem Edit` sets the callsign and the custom messages (keys `7`, `8` and
`9`) of [`CW Mem`](../cwmem/README.md). Open it from CW Mem with key `0`:
`EXIT` saves and goes back to CW Mem, which then goes back to the keyer that
opened it. It can also be opened from the menu; `EXIT` then returns to the
menu.

## Requirements and install

- F4HWN Fusion firmware with overlay ABI 1 and API level 3.
- `CW Mem` installed: CW Mem Edit writes CW Mem's user block. Without it, the
  app shows `Install CW Mem` and saves nothing.
- Build: `./compile-app.sh cwmemedit`, output `build/Apps/CWMemEdit.app`;
  install it in a free slot with UV Studio.

## Use

The list shows four rows: `CALL`, `7`, `8` and `9`.

- `UP` / `DOWN`: select a row.
- `MENU`: edit it.
- `EXIT`: save the changes and leave.

`CALL` shows the callsign CW Mem sends: the one set here or, when none is set,
the callsign of the boot message, shown as `F4WAT (LINE 1)`, or `NOT SET`.
Editing an empty `CALL` starts from the LINE 1 callsign. Leave it empty to
keep following LINE 1.

In a row, text entry works as in APRS Edit:

| Key | Short press (repeat to cycle) | Long press |
|---|---|---|
| `0` | space | `0` |
| `1` | `. , ? / = -` | `1` |
| `2` to `9` | `ABC`, `DEF`, `GHI`, `JKL`, `MNO`, `PQRS`, `TUV`, `WXYZ` | the digit |

- A character settles after 0.8 s or when another key is pressed.
- `UP` / `DOWN`: move the cursor (underlined); it wraps between both ends.
- `F`: erase the character before the cursor.
- `MENU`: keep the change. `EXIT`: undo it.

The callsign holds up to 10 characters, a message up to 31.

## Storage

The changes are written to CW Mem's user block (`../cwmem/cwmem_user.h`)
through the configuration proxy, after the app closes. Installing or
updating CW Mem with UV Studio resets that block to the defaults.

## Credits

- CW Mem Edit: Johan Denoyer F4WAT ([@jdenoy](https://github.com/jdenoy)).
- Configuration proxy and text entry model: APRS Edit.
