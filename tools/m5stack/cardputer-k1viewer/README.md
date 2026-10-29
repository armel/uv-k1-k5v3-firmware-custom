# Cardputer K1Viewer

K1Viewer turns an original M5Stack Cardputer or Cardputer V1.1 into a remote
display and keyboard for a UV-K1/K5V3 running the Fusion firmware. The
Cardputer receives the screen stream over USB-C, displays the radio screen
across its full width, and forwards keyboard input to the radio.

## Requirements

- Original M5Stack Cardputer or Cardputer V1.1
- USB-C data cable for flashing
- USB-C to USB-C data cable for connecting the Cardputer to the K1
- Fusion firmware from the `feature_update_v7` branch installed on the K1
- Optional M5Stack Unit Glass (U158) or Unit Glass2 (U158-B), and Grove cable

## Build

The build script automatically installs Arduino CLI 1.5.1, ESP32 core 3.3.12,
M5Cardputer 1.1.1, M5Unified 0.2.28, and M5GFX 0.2.35 under
`~/Library/Caches/cardputer-k1viewer`. The first build requires an Internet
connection.

From the repository root, run:

```sh
cd tools/m5stack/cardputer-k1viewer
./build-local.sh
```

The flash-ready image is written to:

```text
dist/cardputer-k1viewer-esp32s3.bin
```

This is a merged image containing the bootloader, partition table, and
application. It must be flashed at address `0x0`.

## Flash with ESP Launchpad

1. Set the Cardputer side power switch to **OFF**.
2. Press and hold **G0**.
3. Connect the Cardputer to the computer with a USB-C data cable, then release
   **G0**.
4. Open [ESP Launchpad](https://espressif.github.io/esp-launchpad/) in a Web
   Serial-compatible browser such as Chrome or Edge.
5. Select the **DIY** tab.
6. Click **Connect** and select the Cardputer serial port.
7. In **Flash Address**, replace the default value with **`0x0`**.
8. Click **Browse files** and select
   `dist/cardputer-k1viewer-esp32s3.bin`.
9. Click **Program** and wait for the flash operation to complete.
10. Disconnect the USB cable, set the side power switch to **ON**, and restart
    the Cardputer.

> Do not keep ESP Launchpad's default `0x1000` address. This merged image must
> be written at `0x0`.

## Connect the K1

After restarting, connect the K1 directly to the Cardputer with the USB-C to
USB-C data cable. `K1 CONNECTED` appears when the radio USB interface has been
detected.

The Cardputer has only one USB-C port, so it cannot remain connected to the
computer while acting as the K1 USB host.

## Experimental Unit Glass mirror

The original M5Stack Unit Glass (U158, I2C address `0x3D`) and Unit Glass2
(U158-B, factory-default I2C address `0x3C`) can mirror the raw 128 x 64 K1
screen while the Cardputer keeps showing its normal full-width view and status
line. Connect only one Glass display at a time.

1. Turn the Cardputer off.
2. Connect the Unit Glass to the Cardputer Grove port with a standard Grove
   cable.
3. Turn the Cardputer on.

The display is detected automatically on the Cardputer Grove I2C pins
(`SDA=GPIO2`, `SCL=GPIO1`). K1Viewer checks for Glass2 at `0x3C` first, then for
the original Glass at `0x3D`. Each driver first tries the 400 kHz rate documented
by M5GFX and falls back to 100 kHz if detection fails. At startup, the Cardputer
briefly reports `GLASS2 400 KHZ`, `GLASS2 100 KHZ`, `GLASS FW x 400 KHZ`,
`GLASS FW x 100 KHZ`, or `GLASS NOT FOUND`. A rectangle and cross are drawn on a
detected Glass before the first K1 frame replaces them. If no Unit Glass is
connected, K1Viewer behaves as before.

The original Glass keeps its conservative update path: screen changes are sent
one 8 x 8 tile at a time, with 5 ms between transfers, so its STM32-based I2C
protocol leaves enough time for USB reception and the Cardputer display. A
large screen change can therefore appear progressively. Glass2 is driven
directly through its SSD1309 framebuffer. Its image is prepared without keeping
an I2C transaction open, then transferred one 128 x 8 page per main-loop pass.
This keeps each transfer short and lets USB reception run between pages.

Glass2 must use its factory-default `0x3C` address. Changing it to `0x3D` with
the solder bridge would make automatic distinction from the original Glass
ambiguous and is not supported.

## Keyboard controls

| Cardputer key | K1 action |
| --- | --- |
| `M` or Enter | MENU / confirm |
| Backspace or `Fn` + backtick | EXIT / back |
| `0` to `9` | Radio digit |
| `F` | F / `#` key |
| `Fn` + `1` | SIDE1 |
| `Fn` + `2` | SIDE2 |
| `Fn` + `3` | F / `#` key |
| `Fn` + `8` | `*` key |
| `Fn` + `;` or `Fn` + `,` | Up / K1 left |
| `Fn` + `.` or `Fn` + `/` | Down / K1 right |

Hold **Shift** with a mapped radio key to send a long press. The `G`, `O`, and
`B` keys select the grey, orange, and blue palettes locally. **G0/BtnA** cycles
through the same three palettes.

Cardputer Adv compatibility is not claimed until it has been validated on
real hardware.
