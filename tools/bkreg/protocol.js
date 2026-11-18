"use strict";

(function initializeBKRegProtocol() {
const XOR_TABLE = Uint8Array.from([
  0x16, 0x6c, 0x14, 0xe6, 0x2e, 0x91, 0x0d, 0x40,
  0x21, 0x35, 0xd5, 0x40, 0x13, 0x03, 0xe9, 0x80,
]);

const COMMAND_READ_REGISTER = 0x0601;
const COMMAND_WRITE_REGISTER = 0x0602;

function readU16(bytes, offset) {
  return bytes[offset] | (bytes[offset + 1] << 8);
}

function pushU16(bytes, value) {
  bytes.push(value & 0xff, (value >> 8) & 0xff);
}

function crc16(bytes) {
  let crc = 0;
  for (const byte of bytes) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc & 0x8000) !== 0 ? ((crc << 1) ^ 0x1021) : (crc << 1);
      crc &= 0xffff;
    }
  }
  return crc;
}

function xorBytes(bytes) {
  return Uint8Array.from(bytes, (byte, index) => byte ^ XOR_TABLE[index % XOR_TABLE.length]);
}

function makeCommand(commandId, payload) {
  const body = [];
  pushU16(body, commandId);
  pushU16(body, payload.length);
  body.push(...payload);

  if ((body.length & 1) !== 0) {
    body.push(0);
  }

  const checksum = crc16(body);
  const encoded = xorBytes([...body, checksum & 0xff, checksum >> 8]);
  const packet = [0xab, 0xcd];
  pushU16(packet, body.length);
  packet.push(...encoded, 0xdc, 0xba);
  return Uint8Array.from(packet);
}

function makeReadCommand(register) {
  return makeCommand(COMMAND_READ_REGISTER, [register]);
}

function makeWriteCommand(register, value) {
  return makeCommand(COMMAND_WRITE_REGISTER, [register, value & 0xff, value >> 8]);
}

function extractFrames(input) {
  const bytes = input instanceof Uint8Array ? input : Uint8Array.from(input);
  const frames = [];
  let cursor = 0;

  while (cursor + 1 < bytes.length) {
    let start = cursor;
    while (start + 1 < bytes.length && !(bytes[start] === 0xab && bytes[start + 1] === 0xcd)) {
      start += 1;
    }

    if (start + 1 >= bytes.length) {
      const keep = bytes.length > 0 && bytes[bytes.length - 1] === 0xab ? bytes.length - 1 : bytes.length;
      return { frames, remainder: bytes.slice(keep) };
    }

    if (bytes.length - start < 4) {
      return { frames, remainder: bytes.slice(start) };
    }

    const bodyLength = readU16(bytes, start + 2);
    if (bodyLength < 4 || bodyLength > 512) {
      cursor = start + 1;
      continue;
    }

    const frameLength = bodyLength + 8;
    if (bytes.length - start < frameLength) {
      return { frames, remainder: bytes.slice(start) };
    }

    const footer = start + frameLength - 2;
    if (bytes[footer] !== 0xdc || bytes[footer + 1] !== 0xba) {
      cursor = start + 1;
      continue;
    }

    const body = xorBytes(bytes.slice(start + 4, start + 4 + bodyLength));
    const commandId = readU16(body, 0);
    const payloadLength = readU16(body, 2);
    if (payloadLength <= body.length - 4) {
      frames.push({
        commandId,
        payload: body.slice(4, 4 + payloadLength),
      });
    }
    cursor = start + frameLength;
  }

  return { frames, remainder: bytes.slice(cursor) };
}

function decodeRegisterReply(frame) {
  if (frame.commandId !== COMMAND_READ_REGISTER || frame.payload.length !== 3) {
    return null;
  }
  return {
    register: frame.payload[0],
    value: readU16(frame.payload, 1),
  };
}

globalThis.BKRegProtocol = Object.freeze({
  COMMAND_READ_REGISTER,
  COMMAND_WRITE_REGISTER,
  crc16,
  decodeRegisterReply,
  extractFrames,
  makeCommand,
  makeReadCommand,
  makeWriteCommand,
  xorBytes,
});
}());
