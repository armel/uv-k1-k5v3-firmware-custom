# CW QSO Auto v1.0 architecture

The resident firmware supplies permission checks, silent TX preparation,
PA gating, RX restoration, BK4819 register access, display and deferred config.
The application preserves its nonblocking Morse sequence engine and manual
STRAIGHT/AUTO/IAMB A/IAMB B engine. All modes share keyOn/keyOff and RF ramps.
The local first-mark audio fix enables silent Tone1/AF and the HP during the
existing preparation; no extra RF wait or changed Morse durations.

The 16 CW records stay at assets offset 512, stride 68: CW/v1/number header,
then 64 payload bytes. Name records at 1696, stride 16, contain fixed two-digit
number, space, 12 printable ASCII name bytes and NUL. QNM1 is at 2282.
The same reader and RST expansion serve every message. Names never enter TX.
See FORMAT-NAMES.md for validation and persistence details.

SIDE 1 captures q.selected into the existing q.message. Scheduled repeats
reuse q.message, so pause navigation does not change the repeated payload.
SIDE 2 clears the repeat request and lets an active sequence finish. The
deadline is set after message completion, using the persistent REPEAT seconds.
PTT on the main screen sends the selection once; in SETTINGS it reaches the
existing straight-key path without changing the chosen paddle mode.

The 12-byte configuration keeps its existing layout. Byte 11 stores repeat
seconds in the upper nibble and AUTO/A/B in the lower nibble. Old algorithm
bytes 0/1/2 default to 5 seconds; invalid algorithm values default to AUTO.
The former application frequency-offset RAM byte stores the delay. Other CW
settings and the held-control release/denial guards are preserved.

V7 app_api_t is append-only: ABI 1, resident API 4, app minimum API 3.
tx_prepare remains at 364; cycles at 344; asset_read at 264; print_normal at 60;
BK write/read at 80/76; uidivmod at 272. The six existing Thumb relay stubs
retain their ABI assertions and executable-byte tests. g.A remains at 68.
Overlay VMA is 0x20000280, budget 4096 including BSS, assets budget 3840.
No new assembly, resident hooks or direct Flash writes are introduced.

The single-file HTML embeds the approved reference. It validates the FAP1
header, CRCs, executable/fixed-asset SHA256, CW signatures, name numbers and
padding. Export only permits name/content payload changes plus asset CRC.
This is accidental-corruption protection, not signature-based authentication.
The stable executable and HTML are byte-identical after V7 reconstruction.
