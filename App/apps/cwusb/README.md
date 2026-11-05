# CW USB overlay app

`CW USB` is the USB-C-input variant of CW Keyer. It transmits a manually
keyed A1A-style carrier on the current TX VFO and is entirely contained in its
own 4 KiB overlay. The implementation is shared with `CW Keyer`, but this
binary contains only the USB-C paddle input path.

## Requirements and install

- F4HWN Fusion firmware with overlay ABI 1 and API level 3, including the
  `tx_prepare` service (API table of at least 368 bytes). On an older API
  table the app never arms, so it cannot transmit.
- Build: `./compile-app.sh cwusb` from the repository root, output
  `build/Apps/CWUSB.app`; install it in a free slot with UV Studio.
- The VFO must be in FM. The transmitted signal is an unmodulated carrier, so
  the other station needs a CW or SSB receiver to hear a tone.

## Migrating from CW Keyer 1.0

The recommended upgrade is to keep or update CW Keyer in its current slot and
install CW USB in a free slot. CW Keyer keeps its existing settings because its
name is unchanged.

Settings are stored in the configuration area of the slot where the app is
installed, tagged with the name of the app that saved them. CW USB therefore
starts with defaults, even when it is installed in the former CW Keyer slot:
the configuration left there is tagged `CW Keyer`, so CW USB ignores it.
Configure CW USB once: use the same `XTAL` correction as CW Keyer and
CW Decode, then set its WPM, tone, break-in, mode, keyer, contact order and
speaker preference.

Settings are written to flash when you leave the app with `EXIT`. Switching
the radio off or restarting it while CW USB is open loses the changes made in
that session.

## Arming the transmitter

Transmission is locked each time CW USB starts: the padlock is shown in the
status bar, and neither the USB-C contacts nor `PTT` can key the carrier. To
arm it:

1. Release every contact and `PTT` for at least 20 ms.
2. Hold the tip contact closed for five seconds: the straight key, the dit
   paddle, or both paddles together. Releasing it earlier restarts the count.
3. When the check mark replaces the padlock, release everything again for at
   least 20 ms.

The gesture is the same in `STRAIGHT` and `PADDLE`, and nothing is
transmitted while you make it. The ring contact is ignored during the
gesture, so a mono plug can arm too. Once armed, the app stays armed until you
leave it, across every return to receive. Arming is never saved: the next
launch asks for the gesture again. `EXIT` works during the gesture, and the
help page (`MENU`) cancels an arming in progress.

## Controls

- `PTT`: straight-key carrier in every mode, once armed.
- USB-C D+ (tip): straight key in `STRAIGHT`, dit in `PADDLE`.
- USB-C D- (ring): dah in `PADDLE`.
- `1`: toggle `STRAIGHT` / `PADDLE`.
- `8`: cycle the paddle keyer `AUTO` / `IAMB A` / `IAMB B` (shown on the main
  screen). Refused while the transmitter is prepared: wait for the return to
  receive.
- `0`: enable or mute the CW receive audio path.
- `2`: raise the keyer speed (5 to 60 WPM).
- `3`: raise the receive CW tone by 50 Hz.
- `4`: raise the semi-break-in delay by 50 ms.
- `5`: swap the dit and dah contacts.
- `6`: raise the crystal correction by one BK4829 `REG_3B` step.
- `F`, then `2`, `3`, `4` or `6`: lower the corresponding setting.
- `UP` / `DOWN`: tune locally in 100 Hz steps, up to +/-50 kHz.
- `MENU`: show or close the help page.
- `EXIT`: leave the app.

## Paddle keyer modes

The USB variant samples both contacts together, so a squeeze is visible. Key
`8` selects how the paddles are read; the choice is saved with the other
settings.

| Mode | Paddles read | Squeeze | Opposite paddle tapped during an element |
|---|---|---|---|
| `AUTO` | after each element and its space | alternates dits and dahs, stops as soon as you release | ignored if released before the space ends |
| `IAMB A` | also during the element and the space | alternates, stops as soon as you release | remembered when pressed alone, sent next |
| `IAMB B` | as `IAMB A` | alternates; a squeeze still held during an element adds one opposite element after release | as `IAMB A` |

`AUTO` is the keyer of CW USB 1.2: automatic repeat and no paddle memory.
In `IAMB A`, a tap on the opposite paddle while the current one is still held
reads as a squeeze and is not remembered.

## USB-C paddle adapter

![USB-C paddle adapter wiring](usbc-paddle-wiring.svg)

Wire a USB-C plug to a 3.5 mm stereo jack as follows:

| USB-C line | MCU pin | Jack | Function |
|---|---|---|---|
| D+ | PA12 | tip | dit or straight key |
| D- | PA11 | ring | dah |
| GND | - | sleeve | common |
| VBUS, CC, shield | - | insulated, not connected | - |

Contacts close to ground; the app supplies the pull-ups. The adapter works in
both USB-C plug orientations. A mono plug can be used only in `STRAIGHT`, where
the ring is ignored. In `PADDLE`, its ring-to-sleeve short holds the ring
contact closed permanently: dah in `DI-DA`, dit in `DA-DI`.

Never plug this adapter into a charger or computer: a charger energizes VBUS,
and a computer host can also drive or bias D+/D-. The app temporarily
configures PA11/PA12 as inputs and stops the USB clock (a powered USB device
holds D+ for about two seconds after a contact closes), then restores the saved
register bits on exit. There is no PC link while the app is open.

A USB contact change must read the same on five consecutive polls, about 5 ms,
before a straight-key edge or keyer start is accepted.

The common RF keying, CW receive, timing, sidetone and protection behavior is
documented in the sibling [`CW Keyer` README](../cwkeyer/README.md).

## Validation and known USB-C limit

- The USB input path in the original combined CW Keyer was tested on a UV-K1
  (Labs v6.2.0) with a straight key and paddle in both plug orientations. The
  PC link returned after exit, and no false elements appeared with the paddle
  lead close to the antenna at high power into a dummy load. The original
  implementation and test record are in
  [PR #665](https://github.com/armel/uv-k1-k5v3-firmware-custom/pull/665).
- The arming gesture and the `AUTO` / `IAMB A` / `IAMB B` keyer were tested by
  F4LPF on a UV-K5 V3 (Labs, API level 3), and on a UV-K1 by F4WAT.
- Host tests, run from the repository root after a normal build (Python 3 and
  a C11 host compiler, `HOST_CC` overrides `gcc`; they need an ELF host such
  as Linux):

  ```sh
  python App/apps/cwusb/test/arming.py
  python App/apps/cwusb/test/functional.py
  python App/apps/cwusb/test/waveforms.py
  ```

  They compile the app source against simulated GPIO and API callbacks: they
  cover arming, keying and the three keyer modes, not RF or the electrical
  contacts.
- After about 0.5 s without contact, the radio's USB-C path reports a closing
  contact as closed for only 3 to 10 ms, then as open for about 150 to 220 ms
  while it is still held, before it follows the contact again. When that first
  closed reading is shorter than the contact debounce (about 5 ms), the first
  element starts roughly 200 ms late. The debounce timing implies a second
  case, not yet observed on the radio: a longer first reading starts the
  keying at once, then the contact reads open. In `STRAIGHT`, a short carrier
  is then followed by about 200 ms of silence before the intended mark; in
  `PADDLE`, a held paddle can stop after the first element until the window
  ends. The measurements and investigation are tracked in
  [issue #683](https://github.com/armel/uv-k1-k5v3-firmware-custom/issues/683).
  Up to version 1.2, the first paddle press after a return to receive only
  prepared the transmitter and absorbed this window; since the one-time arming
  gesture, that press keys at once.
- The K-plug line PA10 is not used as a paddle input: it follows the PTT
  button on the bench.

## Contributing

CW USB has no keyer source of its own: `cwusb_app.c` sets `CW_KEYER_USB` to 1
and includes [`../cwkeyer/cwkeyer_app.c`](../cwkeyer/cwkeyer_app.c). The
keyer, CW receive, display, settings and RF keying are shared with CW Keyer.

- Any change outside an `#if CW_KEYER_USB` block also changes CW Keyer.
  Behavior that only concerns the USB-C input (contact sampling, debounce or
  start rules, the arming gesture, the iambic keyer, port setup) goes under
  `#if CW_KEYER_USB`, preferably in USB-only functions called from the shared
  loop rather than as conditionals scattered through shared functions.
- A change that benefits both apps stays in the shared code and must be tested
  in both: SIDE and PTT keying in CW Keyer, USB-C and PTT keying in CW USB.
- USB-only texts go in [`../cwkeyer/gen_assets.py`](../cwkeyer/gen_assets.py)
  behind `USB_VARIANT`; each app gets its own generated asset offsets.
- `config_t` is shared. Its last byte, `reservedPort`, now holds the CW USB
  keyer mode (0 `AUTO`, 1 `IAMB A`, 2 `IAMB B`; CW Keyer always saves 0), so
  no spare byte is left. Changing the `config_t` layout or `CFG_MAGIC` resets
  the settings of both apps.
- Each app has its own `APP_VER` in its `build.sh`: a USB-only change bumps
  CW USB, a shared change bumps both.
- Build both apps after every change to the shared source:
  `./compile-app.sh cwkeyer cwusb`. Each build fails if its binary exceeds
  the 4 KiB overlay. CW USB has very little room left.

## Credits

- CW USB application, USB-C paddle port, paddle keyer and contact debounce:
  Johan Denoyer F4WAT ([@jdenoy](https://github.com/jdenoy)).
- One-time arming gesture and `IAMB A` / `IAMB B` keyer modes: Sébastien
  F4LPF ([@lemiller-dev](https://github.com/lemiller-dev)), implemented and
  tested with OpenAI Codex and ChatGPT.
- Original CW Keyer base: Armel F4HWN
  ([@armel](https://github.com/armel)).
- USB-C wiring based on KI6CR's CW adapter for the UV-K5 V3.
- TX sidetone approach: NR7Y's CW implementation.
