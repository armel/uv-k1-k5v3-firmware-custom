# Tab5 K1Viewer

K1Viewer turns an M5Stack Tab5 into a remote touchscreen display for a
UV-K1/K5V3 running the Fusion firmware. The Tab5 receives the screen stream
through its USB-A host port and reproduces the radio's 128 × 64 display with a
pixelated LCD effect.

This project targets early Tab5 units fitted with an ESP32-P4 revision 1.x. Do
not flash this image on ESP32-P4 revision 3.x hardware.

## Requirements

- M5Stack Tab5 with an ESP32-P4 revision 1.x
- USB-C data cable for flashing
- USB-A to USB-C data cable for connecting the Tab5 to the K1
- Fusion firmware from the `feature_update_v7` branch installed on the K1

## Build

The build script automatically installs ESP-IDF 5.5.6 and the ESP32-P4
toolchain under `~/Library/Caches/tab5-k1viewer`. The first build requires an
Internet connection. Later builds reuse the installed toolchain and compiled
dependencies.

From the repository root, run:

```sh
cd tools/m5stack/tab5-k1viewer
./build-local.sh
```

The script verifies that both the bootloader and application target ESP32-P4
revisions 1.0 through 1.99. The flash-ready image is written to:

```text
dist/tab5-k1viewer-esp32p4-rev1.bin
```

This is a merged image containing the bootloader, partition table, and
application. It must be flashed at address `0x0`.

## Flash with ESP Launchpad

1. Connect the Tab5 USB-C port to the computer with a data cable.
2. Press and hold **Reset** for approximately two seconds.
3. Release **Reset** when the internal green LED starts flashing rapidly. The
   Tab5 is now in download mode.
4. Open [ESP Launchpad](https://espressif.github.io/esp-launchpad/) in a Web
   Serial-compatible browser such as Chrome or Edge.
5. Select the **DIY** tab.
6. Click **Connect** and select the Tab5 serial port.
7. In **Flash Address**, replace the default value with **`0x0`**.
8. Click **Browse files** and select
   `dist/tab5-k1viewer-esp32p4-rev1.bin`.
9. Click **Program** and wait for the flash operation to complete.
10. Restart the Tab5 after flashing has finished.

> Do not keep ESP Launchpad's default `0x1000` address. This merged image must
> be written at `0x0`.

## Connect the K1

After restarting, connect the Tab5 USB-A host port to the K1 USB-C port. Do not
use the Tab5 USB-C programming port for this connection.

`K1 CONNECTED` appears when the radio has been detected. The touch controls at
the bottom of the screen select the grey, orange, and blue palettes.

## Known limits

- This build is restricted to Tab5 ESP32-P4 revision 1.x hardware.
- The USB protocol allows up to 75 received frames per second.
- The Tab5 renderer displays up to 50 updates per second.
