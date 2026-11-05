# APRS Edit

APRS Edit is the companion profile editor for APRS TX. It edits the source
callsign, SSID, position, digipeater path, APRS symbol and comment without
duplicating the transmitter, AX.25 encoder or AFSK generator.

The profile is a versioned 117-byte block. Its comment field holds up to 89 printable
ASCII characters, of which the editor uses 64 (what fits on screen without scrolling), and its optional callsign holds up to six uppercase letters or
digits. An empty callsign displays `AUTO` and makes APRS TX use the boot-message
callsign. APRS Edit declares the mutable APRS TX asset block in its own asset
prefix. The resident loader validates and stages the update, then writes APRS
TX's asset and CRC when APRS Edit exits. The application requires API level 3.

## Index

The application opens on a profile summary styled after the Fusion menu. The
left pane scrolls through the previous, current and next entries among
`Call`, `SSID`, `Lat`, `Lon`, `Path`, `Symbol`, `Comm.`; the wider right pane
shows the complete current value in the normal font used during editing,
avoiding a size change when the field is opened. The selected label keeps the
Fusion menu's 8×16 font. Coordinates include degree/minute spacing and their
hemisphere, and the complete longest path uses two lines. The symbol view also
shows its graphical preview, while the comment view shows the whole comment
in the font used to edit it.

| Key | Action |
|---|---|
| `UP` / `DOWN` | Select a field; hold to repeat |
| `MENU` | Open the selected editor |
| `EXIT` | Save the profile if it changed. Return to APRS TX when the editor was opened from TX, otherwise return to the overlay-app index |

`MENU` locks the selected entry in the left pane and edits its draft directly
in the right pane. `MENU` then validates that field and returns to navigation;
`EXIT` discards that field's draft. The comment and preset selector use the
same right pane. A changed profile is saved when the app exits. APRS TX 1.7
opens APRS Edit 1.2 with a one-byte hand-off request; after the save, the loader
commits the proxy update and relaunches TX, which rebuilds its frame from the
updated profile.

## Fields

- `CALLSIGN`: T9 uppercase entry, `*` toggles `ABC`/`123`, a long number key
  inserts its digit, `F` deletes, and `UP`/`DOWN` moves the cursor, wrapping
  between both ends of the text. The cursor stays on a multi-tap character
  until it settles. Empty means `AUTO`.
- `SSID`: number keys set 0-9; `UP`/`DOWN` covers the complete 0-15 range.
- `LATITUDE` / `LONGITUDE`: number keys replace and advance, `UP`/`DOWN` moves the digit
  cursor, and `*` toggles N/S or E/W. Invalid coordinates cannot be validated.
- `PATH`: `UP`/`DOWN` selects `DIRECT`, `WIDE1-1`, `WIDE1-1,WIDE2-1` or
  `ARISS` (the ISS digipeater, 145.825 MHz). APRS Edit 1.2 and APRS TX 1.7 share
  the `APC7` profile marker: paired with an older APRS TX it shows
  `TX/FW MISMATCH` / `UPDATE REQUIRED` instead of saving a path it would reject.
- `SYMBOL`: `UP`/`DOWN` cycles through the 48 APRS table/code pairs with their
  20 × 20 pixel preview and table position; a number key jumps to symbol
  1, 6, 11 ... 46.
- `COMMENT`: 64-character T9 editor in the normal font, six rows of 11
  characters without scrolling, shown the same way in the index. Its length and the input mode sit in two capsules below. `*` cycles `ABC`, `abc`, `123`;
  long number keys insert digits; `F` deletes; `UP`/`DOWN` moves the cursor
  and wraps between the start and the end.
  `PTT` opens `CUSTOM` and four predefined messages. The custom draft remains
  available after previewing a template.

APRS Edit 1.0 and APRS TX 1.4 both require Labs firmware v6.2.0 or later,
which introduces the configuration proxy: earlier firmware refuses to launch
them (`NOT SUPPORTED`). Install APRS TX 1.4 before launching APRS Edit 1.0. If
a compatible APRS TX cannot be reached, the editor remains read-only and
reports a TX/firmware mismatch. Reinstalling APRS TX restores the complete bundled profile.
