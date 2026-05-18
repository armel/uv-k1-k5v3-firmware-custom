# Build GOGUFW 0.3.12 with VS Code on macOS

Docker is enough for the firmware build. VS Code is only a convenient front-end.

## Install once

1. Install **Visual Studio Code**.
2. Install and start **Docker Desktop**.
3. In VS Code, install these extensions when prompted:
   - C/C++ by Microsoft
   - CMake Tools by Microsoft

## Open the project

1. Unzip this firmware package.
2. Open VS Code.
3. Use **File > Open Folder...** and select the folder that contains `compile-with-docker.sh`.

## Build

Press:

```text
Cmd + Shift + B
```

Then choose:

```text
GGFW: Build
```

The command run by the task is:

```bash
./compile-with-docker.sh
```

## Clean build

Use:

```text
GGFW: Clean + Build
```

## Output files

After a successful build, run:

```text
GGFW: Show output files
```

The CMake preset is still internally named `Fusion` because GGFW is based on the F4HWN Fusion feature set, but the firmware branding/version shown on the radio is `GOGUFW 0.3.12 / GGFW`.
