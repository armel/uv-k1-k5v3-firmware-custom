# BK4829 Register Lab

`BK4829 Register Lab` is a dependency-free Web Serial tool for reading and
temporarily changing live BK4829 registers on a UV-K1 / UV-K5 V3 running the
Fusion firmware.

It uses the existing debug commands:

- `0x0601`: read a BK4829 register;
- `0x0602`: write a BK4829 register.

No additional resident firmware code is required. The commands are available
only when the test firmware is built with `ENABLE_UART_RW_BK_REGS=ON`.

## Prepare the radio

Build the Fusion test firmware:

```sh
./compile-firmware.sh Fusion -DENABLE_UART_RW_BK_REGS=ON
```

Flash it and start the radio normally. Close UV Studio, K5Viewer, and any other
program that could already own the serial port.

## Open the tool

Open `tools/bkreg/index.html` directly in desktop Chrome, Edge, or Brave. The
tool uses classic scripts so it can be loaded from a local `file://` URL without
the CORS restriction applied to JavaScript modules.

If the browser policy disables Web Serial for local files, serve the directory
on localhost:

```sh
python3 -m http.server 8000 --directory tools/bkreg
```

Then open <http://localhost:8000>. Select the radio after pressing
**Connect radio**.

No web service or remote backend is involved. The browser opens the radio USB
CDC port at 38400 baud and all register values stay on the local computer.

## Data exchange

The page uses the same framed and obfuscated serial transport as the existing
firmware tools.

For a read, the page sends command `0x0601` with a one-byte register address.
The firmware replies with the register address followed by its 16-bit value in
little-endian order.

For a write, the page sends command `0x0602` with the register address and a
16-bit little-endian value. That command has no acknowledgement, so the page
waits 50 ms, reads the same register with `0x0601`, and reports whether the
returned value matches.

## Features

- read any register from `0x00` through `0x7F`;
- watch `REG_28`, `REG_29`, and `REG_31` at a selectable interval;
- change only selected bits with a 16-bit write mask;
- show the current value and calculated value before confirmation;
- read the register back after every write and report a mismatch;
- keep a timestamped log that can be copied into a test report.

The write operation is a masked read-modify-write:

```text
new = (current & ~mask) | (requested & mask)
```

A mask bit set to `0` preserves the corresponding current register bit.

## Safety and limits

Writes change the live transceiver state only; they do not modify EEPROM or the
firmware image. Firmware activity may rewrite a register before verification.
Unsuitable values can temporarily disrupt receive or transmit behavior, so use
the narrowest possible mask and power-cycle the radio to restore its normal
initialization if needed.

This tool exposes raw register values. It deliberately does not assign names or
meanings to undocumented fields.

## Protocol self-test

The transport encoder and reply parser have a dependency-free Node.js test:

```sh
node tools/bkreg/protocol.test.mjs
```

It checks known read and write packets and a fragmented register reply. It does
not open a serial port or contact the radio.
