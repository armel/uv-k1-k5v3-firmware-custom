# Unbricking the UV-K5 V1

<img width="2016" height="1512" alt="pcbite" src="https://github.com/user-attachments/assets/b3086bf2-c14c-47da-b68b-a6867268ae78" />

Restoring a UV-K5 V1 accidentally flashed with the F4HWN Fusion 🔥 Edition firmware, using OpenOCD and a ST-LINK programmer.

## Introduction

Some users have accidentally flashed a UV-K5 V1 with the F4HWN Fusion 🔥 Edition firmware. This edition **is only** compatible with:

- UV-K5 V3
- UV-K1

When flashed onto a UV-K5 V1, it causes a complete brick, such as:

- The radio does not power on anymore
- DFU mode is unavailable
- No LED activity
- Flashing tools cannot detect the device

Fortunately, the UV-K5 V1 can be fully restored by using OpenOCD and a ST-LINK programmer.

## Requirements

You will need:

- A ST-LINK V2 programmer (original or clone)
- Four Dupont jumper wires
- A small screwdriver to open the radio
- A computer with OpenOCD installed (Windows / macOS / Linux).

## SWD connection points on the UV-K5 V1

The radio must be opened to access the front side of the PCB.
The UV-K5 V1 exposes a 4-pin SWD (Serial Wire Debug) interface:

|Signal|  UV-K5 V1 Pad|    ST-LINK Pin|
|:-------- |:--------:| --------:|
|GND |GND pad | GND |
|SWCLK   |SWCLK pad |   SWCLK
|SWDIO   |SWDIO pad |  SWDIO
|3.3V |   VCC pad | 3.3V |

<img width="750" height="500" alt="bottom" src="https://github.com/user-attachments/assets/381a9bc5-daac-4547-820e-a5c96c0bd2e1" />

> [!WARNING]
> Do not connect the battery while using the ST-LINK.
The ST-LINK provides 3.3V to the board.

## Installing OpenOCD

### macOS (Homebrew)

`brew install openocd`

### Windows

Download the official build from:

- [Website](https://openocd.org/pages/getting-openocd.html)
- [GitHub](https://github.com/openocd-org/openocd/releases)

### Linux (Ubuntu / Debian)

`sudo apt install openocd`

### Verify installation

`openocd --version`

## Download unbrick toolkit

Download the [unbrick toolkit archive](https://github.com/user-attachments/files/24083102/unbrick_k5_v1.zip) and extract it on your PC.

> [!NOTE]
> For Windows users, Diogo [@dguimaraes88](https://github.com/dguimaraes88) has prepared a [dedicated version](https://1drv.ms/f/c/5d40488ea2119b08/IgAshx87imRRRLuy6jx0-xCzAY25Zj3vSB3H5TlePGPj5Uw?e=orBJJ6) of the unbrick toolkit to make it easier to use. Many thanks to him for the initiative and for the time he invested in it.


## Unbrick procedure

### Connect the ST-LINK

1. Connect the ST-LINK pins to the SWD pads:

	* 3.3V → 3.3V
	* SWDIO → SWDIO
	* SWCLK → SWCLK
	* GND → GND

2. Plug the ST-LINK into your computer.
3. Power on your UV-K5 V1 (normal mode).

### Flash the bootloader

From the `unbrick_k5_v1` directory:

#### Option A — Use the helper script

`
./unbrick_k5_v1.sh
`

#### Option B — Run the OpenOCD command manually

`
openocd -f ./interface/stlink.cfg -f ./target/dp32g030.cfg -c "init; reset halt; uv_flash_bl bootloader.bin; shutdown"
`

> [!NOTE]
> For Windows users, you must either run this command with administrator privileges or launch OpenOCD.exe as an Administrator.

https://github.com/user-attachments/assets/a511fdb3-a3a3-4fe1-91cc-31e765221b22


If no errors appear, the bootloader has been successfully restored. 

- Disconnect and remove the ST-LINK from your UV-K5 V1 SWD port
- Reinstall the battery
- Power on the radio on DFU mode

The device should now start correctly on DFU mode again. You can then flash stock firmware, or the [F4HWN firmware for UV-K5 V1](https://github.com/armel/uv-k5-firmware-custom) (not the Fusion 🔥 Edition).

# Helpful Videos

Here are two videos that can assist you during the unbricking process. If you’re new to ST-LINK, OpenOCD, or SWD debugging, these will give you a clear visual overview:

- [Your UV-K5 V1 is bricked? 🫣 Here’s how to fix it 😌](https://www.youtube.com/watch?v=4cWtYH_bpro) by F4HWN
- [UNBRICK - Process Windows Quansheng UV-K5 , K6](https://www.youtube.com/watch?v=EmF08o5MIE0) by M0FXB

These resources are not mandatory, but they can make the procedure much easier to follow.

# Disclaimer

This procedure requires opening the device and directly manipulating its microcontroller over SWD. Incorrect use may permanently damage the radio. Proceed at your own risk.
