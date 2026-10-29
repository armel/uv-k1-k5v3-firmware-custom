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

## Build

The build script automatically installs Arduino CLI 1.5.1, ESP32 core 3.3.12,
and M5Cardputer library 1.1.1 under
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
