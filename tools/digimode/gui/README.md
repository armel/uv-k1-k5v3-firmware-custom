# UV-K1/K5V3 Digmode GUI Release Package

This folder contains three standalone Windows GUI variants for the UV-K1/K5V3 digital mode workflow.
Each version folder is self-contained and can be distributed independently.

## Package Layout

- `v1`: original audio bridge GUI
- `v2`: FT8 UART symbol scheduler GUI
- `v3`: WSJT-X UDP driven FT8 batch scheduler GUI

Each version folder contains the minimum files needed for release:

- Python entrypoint
- `requirements.txt`
- `run.bat`
- `build.bat`
- PyInstaller `.spec`

## General Usage

### Run From Source

1. Open the target version folder.
2. Double-click `run.bat`.
3. The script creates a local `.venv` if needed.
4. The script installs dependencies from `requirements.txt`.
5. The GUI starts.

### Build a Windows Executable

1. Open the target version folder.
2. Double-click `build.bat`.
3. The script creates a local `.venv` if needed.
4. The script installs dependencies from `requirements.txt`.
5. The script runs PyInstaller.
6. The built executable appears in the local `dist` folder.

## Requirements

- Windows
- Python 3.10 or newer
- Python added to `PATH`

## Version Overview

### v1: Audio Bridge Version

#### Positioning

`v1` is the most general-purpose version.
It works by monitoring outgoing PC audio and forwarding detected tone frequency changes to the radio over UART.

#### Best For

- Maximum compatibility with different software
- Situations where the source software does not provide suitable UDP control data
- Tone-following from arbitrary digital mode software
- Users who want a generic bridge rather than a WSJT-X-specific workflow

#### How It Works

- Connect the radio over UART.
- Select the correct Windows playback or loopback source.
- The GUI monitors outgoing audio.
- The GUI estimates the active tone frequency.
- The GUI streams frequency updates to the radio in real time.

#### Usage Notes

- `v1` depends on Windows audio capture configuration.
- It is more flexible than `v3`, but also more dependent on system audio setup.
- This is the version to choose when you want to work with software other than WSJT-X.

#### Typical Workflow

1. Open `v1`.
2. Run `run.bat`.
3. Connect the radio UART in the GUI.
4. Select the correct playback or loopback source.
5. Start bridge mode or manual TX mode.
6. Start your digital mode software.
7. Let the GUI follow the outgoing tones.

#### Technical Reference

See [`v1/TECHNICAL.md`](v1/TECHNICAL.md) for a detailed description of the
signal processing pipeline, including FFT + phase vocoder frequency estimation,
multi-stage debouncing, adaptive symbol timing, and software VOX design.

### v2: Manual FT8 UART Test Version

#### Positioning

`v2` is a manual FT8 testing and validation tool.
It does not depend on live audio and does not require WSJT-X UDP integration.

#### Best For

- Manual FT8 transmission tests
- Firmware protocol validation
- FT8 timing verification
- Bench testing without WSJT-X

#### How It Works

- You enter the target RF frequency manually.
- You enter the FT8 text manually.
- The GUI encodes the FT8 frame locally using PyFT8.
- The GUI synchronizes with the radio clock.
- The GUI sends the remaining symbols of the current FT8 slot over UART.

#### Usage Notes

- `v2` is mainly an engineering and manual test tool.
- It is not intended to be the most convenient day-to-day operating mode.
- Use this when you want to verify firmware behavior without involving WSJT-X.

#### Typical Workflow

1. Open `v2`.
2. Run `run.bat`.
3. Connect the radio UART.
4. Enter the RF frequency and FT8 message manually.
5. Trigger transmission from the GUI.
6. Verify that the radio transmits the expected FT8 symbol sequence.

### v3: WSJT-X Optimized Version

#### Positioning

`v3` is the WSJT-X-specific optimized version.
It is designed for the most automated workflow with the current firmware batch scheduler.

#### Best For

- Daily use with WSJT-X
- Automatic FT8 transmit scheduling
- Automatic RX retune on WSJT-X frequency changes
- Tight integration with the firmware's `SCHED_TX` workflow

#### How It Works

- The GUI listens for WSJT-X UDP status packets.
- On FT8 TX start, it extracts the outgoing text from WSJT-X.
- It converts the text into FT8 symbols with PyFT8.
- It sends only the remaining part of the current FT8 slot as a one-shot UART batch.
- When WSJT-X changes dial frequency while idle, it sends an empty batch so the firmware retunes RX without transmitting.

#### Usage Notes

- `v3` is optimized for WSJT-X.
- This is the preferred version when your workflow is centered on WSJT-X.
- WSJT-X is the validated target.
- JTDX compatibility depends on the UDP fields provided by the specific JTDX build in use.

#### Typical Workflow

1. Open `v3`.
2. Run `run.bat`.
3. Connect the radio UART.
4. In WSJT-X, open `Settings -> Reporting`.
5. Enable `Accept UDP requests`.
6. Set UDP to `127.0.0.1:2237` for unicast.
7. If using multicast with other tools, set UDP to `224.0.0.73:2237` and match the same setting in the GUI.
8. In the `v3` GUI, set the UDP IP, port, and multicast mode to match WSJT-X.
9. Start the UDP listener.
10. Let WSJT-X control the TX workflow.

## Which Version Should You Use?

- Choose `v1` if you need maximum compatibility and want to work with almost any software via audio.
- Choose `v2` if you want manual FT8 testing and firmware validation.
- Choose `v3` if you use WSJT-X and want the most automated and optimized workflow.