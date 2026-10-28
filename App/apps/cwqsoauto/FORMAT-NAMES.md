# Experimental named Bank16 format (QNM1)

This experiment starts from `CWQSOAuto-menu-left.app`, not the archived PR
release. It preserves early HP, REPEAT 1–15 seconds/default 5/persistent,
SETTINGS PTT straight keying, SIDE 1 repeat the selection captured at activation
and SIDE 2 stop-after-message. See [REPEAT-SELECTED.md](REPEAT-SELECTED.md).
Only `{RST}` is a dynamic token. CALL/NAME remain editable literal CW text.

## Data layout

Offsets below are relative to the assets section, not the file header.

| Section | Offset | Length | Format |
| --- | ---: | ---: | --- |
| CW bank | 512 | 1088 | 16 × 68 bytes, unchanged |
| Name table | 1696 | 256 | 16 × 16 bytes |
| Format marker | 2282 | 4 | ASCII `QNM1` |
| Total assets | 0 | 2286 | Below the 3840-byte limit |

For every index `i` from 0 to 15:

- CW record `512 + 68*i`: bytes 0–3 are `CW`, version 1, message number `i+1`;
  bytes 4–67 hold up to 64 encoded CW bytes (NUL-padded if shorter).
- Name record `1696 + 16*i`: bytes 0–1 are immutable ASCII `01`–`16`, byte 2
  is an immutable space, bytes 3–14 hold 0–12 editable printable ASCII bytes,
  byte 15 is NUL. Unused name bytes must be zero.
- Both tables use the same index, navigation, name reader and CW engine for
  all 16 messages. Names are display metadata only and never reach the CW reader.

The existing fixed label slots are reused; CW offsets, signatures and initial
contents do not change. Initial names are CQ, REPONSE, 73, MSG 4–MSG 14, AGN?, ?.
All selections now show `number name` on the existing label row and the same
bounded content preview below it. MENU stays lower-left; no preview help row.

Empty names show only the fixed number. Names are limited to 12 bytes to fit
the existing 15-character title buffer including number/space. Firmware forces
termination at byte 15 and substitutes `?` for nonzero bytes outside ASCII
32–126. The editor rejects invalid encoding, missing termination and nonzero
padding rather than silently repairing or truncating them.

## File checks and compatibility

The standalone HTML embeds exactly the new experimental `.app`. It checks
FAP1 metadata, resident size limits, code CRC32, asset CRC16 (low CRC32 bits),
approved executable SHA256, fixed asset SHA256, `QNM1`, all CW signatures and
fixed name numbers. The mutable name fields are masked for the fixed-asset
hash. Export may change only CW payloads, name payloads/terminators and asset
CRC bytes 62–63. Code, record headers, number prefixes, format marker and all
other assets remain immutable. Export is validated again before download.
These checks prevent accidental changes; they are not cryptographic signing
against a maliciously modified editor.

Approved old stable/nav16/preview16/repeat01/menu-left profiles remain editable
for CW contents. They have no names; name inputs are explicitly disabled with
an old-format diagnostic. No executable upgrade occurs implicitly. To migrate,
open the old file and the new reference in separate tabs, copy the 16 contents
to the new reference, set names, export, and install the new personalized APP.
Unknown profiles are rejected. Old editors do not accept the new QNM1 file.

Names and contents are stored in the installed application assets; they survive
application exit and radio reboot, and reimporting the saved personalized APP
restores both in the editor. No running overlay writes Flash. Installing a fresh
default APP overwrites personalization; keep the exported APP as backup.

## Build, tests and hardware

Set ARM_CC to the existing Cortex-M0+ GCC toolchain, then run:

```
python App/apps/cwqsoauto/test/experimental.py --browser
```

This uses the strict production linker and targeted replacement tests; the old
`test/run.py` has release/frequency/display assumptions for the PR version.
Host GPIO/audio/Flash mocks are software checks, not physical radio validation.

Final overlay: **4084 bytes including 92 bytes BSS**, 12 spare bytes.
Assets: **2286 bytes**. No new Thumb assembly or compilation flags.

Hardware checks: customize names 01/04/15/16, including empty and 12-character
names; install the exported APP; inspect number/name/preview/MENU; check restart
persistence; PTT must send the CW content without the name. Test an RST message,
SIDE 1 repetition/SIDE 2 stop, SETTINGS PTT and receive audio after TX. These
checks have not yet been performed on a radio for this experiment.
