import assert from "node:assert/strict";

import "./protocol.js";

const {
  decodeRegisterReply,
  extractFrames,
  makeReadCommand,
  makeWriteCommand,
  xorBytes,
} = globalThis.BKRegProtocol;

function asHex(bytes) {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

assert.equal(
  asHex(makeReadCommand(0x28)),
  "abcd0600176a15e60691d331dcba",
);
assert.equal(
  asHex(makeWriteCommand(0x31, 0x1234)),
  "abcd0800146a17e61fa51f406bf3dcba",
);

const body = [0x01, 0x06, 0x03, 0x00, 0x28, 0x34, 0x12];
const encoded = xorBytes([...body, 0xff, 0xff]);
const packet = Uint8Array.from([
  0x00,
  0xab, 0xcd, 0x07, 0x00,
  ...encoded,
  0xdc, 0xba,
  0xab,
]);

let parsed = extractFrames(packet.slice(0, 8));
assert.equal(parsed.frames.length, 0);
parsed = extractFrames(Uint8Array.from([...parsed.remainder, ...packet.slice(8)]));

assert.equal(parsed.frames.length, 1);
assert.deepEqual(decodeRegisterReply(parsed.frames[0]), {
  register: 0x28,
  value: 0x1234,
});
assert.deepEqual(Array.from(parsed.remainder), [0xab]);

console.log("BK4829 protocol tests passed");
