# CW QSO Auto architecture and file contract

## Overlay and services

The 4 KiB overlay RAM contains executable code, constants and BSS: **4,092 B,
including 92 B BSS**, linked at `0x20000280`. Read-only assets remain in the
external Flash and are accessed through `asset_read`. Resident functionality
is reached only through `app_api_t`; the app does not link resident symbols or
write message Flash directly. The minimum interface is ABI 1 / API 3 with at
least 368 API-table bytes. The resident firmware is unchanged.

The portable builder is called by `build.sh`, which the repository's existing
`compile-app.sh` discovers. The distributed app is placed under
`archive/apps/v6.3.0/`, following the versioned archive convention. No catalogue
manifest or resident application list is added.

## Flash bank

Assets total **2,282 B**. The bank starts at asset offset **512** and ends at
offset **1600**, exclusive. Each of the 16 records is exactly **68 B**:

| Relative offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 2 | ASCII `CW` |
| 2 | 1 | Record format version `01` |
| 3 | 1 | One-based record number 1–16 |
| 4 | 64 | Fixed-size message payload |

Record `n` (one-based) begins at `512 + (n - 1) * 68`. Payload text uses
uppercase ASCII `A–Z`, digits, space, `/`, `?`, plus **byte `02`** for `{RST}`.
Unused bytes are zero-filled; exactly 64 occupied bytes need no NUL terminator.
The reader is bounded by the payload length. Nonzero bytes after a NUL are
rejected by the editor. RST expands to the three current digits during Morse
generation, without storing them in the bank. The screen preview uses `*` for
the token and a bounded continuation marker without modifying the payload.

Initial messages:

| Slots | Initial text |
| --- | --- |
| 01 | `CQ CQ CQ DE F4XXX F4XXX K` |
| 02 | `DE F4XXX UR {RST} {RST} NAME SEB HW?` |
| 03 | `73 TU E E` |
| 04–14 | `MSG 4` … `MSG 14` |
| 15 | `AGN?` |
| 16 | `?` |

The Morse table, labels, receive register profile and defaults are also assets.
`F4XXX` and `SEB` are folded into default payloads by `gen_assets.py`, not kept
as duplicate executable strings. They are not tokens or restrictions on edits.

## FAP1 container

The `.app` is **6,438 B**: 64-byte header, 4,092-byte overlay image and
2,282-byte assets. Multi-byte fields are little-endian.

| Header offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 4 | `FAP1` magic |
| 4 | 2 | Header format 1 |
| 6 / 7 | 1 each | ABI major 1 / minimum API level 3 |
| 8 / 12 | 4 each | Overlay size / overlay CRC32 |
| 16 / 18 | 2 each | Capabilities / shortcut |
| 20 / 36 | 16 each | Name `CW QSO Auto` / version `0.4`, NUL padded |
| 52 / 56 | 4 each | Overlay VMA / entry offset |
| 60 / 62 | 2 each | Asset size / low 16 bits of the assets' CRC32 |

The asset checksum is the truncated CRC32 used by the existing packer, not a
different CRC16 algorithm. CRC checks detect corruption; the editor additionally
pins SHA-256 for the executable and all assets outside the message bank. It
checks each record header independently. Personalization may change **only**
the 16 payload ranges and header bytes 62–63. Code, code CRC and all loader
metadata are preserved. These checks are not a signature/authentication system
for a maliciously replaced HTML file.

## Editor and persistence

`editor.html` includes the approved `.app` as base64 and validates it through
the same path as imported files. The only validation-core change from the
existing editor is an additional exact profile (`repeat01`); existing formats,
length/character/RST checks, CRCs, hashes and allowed-difference checks remain.
It uses inline JS/CSS and native Web Crypto/Blob services, no external resources,
network access, telemetry, account or persistent browser storage. On validation
failure export is blocked. The downloaded file is the user's backup.

Old approved profiles remain importable without automatic code migration.
Future executable builds require a new exact profile; changing a hash check to
accept arbitrary code is not supported. Installing a different default `.app`
can replace personalized messages; saving/reimporting the customized file is
the supported preservation mechanism, not automatic radio-side migration.

The resident configuration proxy offers a fixed-size/fixed-target edit window
(117 bytes maximum per launch),
not a general on-radio 1 KiB bank editor. This contribution does not extend it.
Computer-side editing avoids both that scope change and a new text-entry UI in
the nearly full overlay. The normal CW settings use the existing resident
configuration service; RST starts at `599` on entry.

## Engine, audio and local size work

The non-blocking message sequencer uses the existing key-on/key-off path,
permissions, RF shaping and RX restoration. Manual STRAIGHT, AUTO and IAMB A/B
remain. A/B retain isolated opposite contacts during active elements/spaces;
B also remembers a squeeze during the RF mark, including the ramps, rather
than treating a squeeze confined to the space as a B tail. Existing input
qualification is retained; arbitrarily short pulses below the scan/qualification
window are not guaranteed.

SIDE 1 and timer expiry share a launch path. A single deadline is set after
the final RF/audio edge, before synchronous RX restoration; expiry requests a
new MSG 01, with the usual TX preparation. SIDE 2 clears repeat state even while
the current message is active. EXIT/paddle still cancel through the normal
cleanup and release guard. Held startup controls cannot initiate a message.

After resident TX preparation and the retained REG_30 calibration cycle, Tone1
is configured at zero frequency, AF_ALAM is selected and the HP path enabled
before the **existing 2 ms PLL wait**. The RF and tone then start normally.
Zero frequency is the existing digital tone-off command; it is not proof of a
transient-free analog output. Slight switching clicks remain a hardware
qualification point. Abandoned event queues and 10/16 ms extra preambles are
not part of this contribution.

Local sharing reduced the repeat implementation from 4,216 to 4,092 B. Six
small Thumb-1 API routines use a common literal pool; five tail-dispatch through
the API and `gainBase` has an aligned call frame. Basic asm, `noipa`, symbolic
linker addresses and compile-time offset assertions protect the ABI. The
linked instructions are separately checked; these checks do not replace full
radio testing. No RF protection or feature was removed to meet the size limit.
