/**
 * Node 单元测试：协议封包/解包 + 卫星块/条目构建。
 * 运行：node tools/k5web/test_protocol.mjs
 */
import { strict as assert } from "node:assert";
import proto from "./protocol.js";
const { OBFUSCATION, CMD, crc16, crc8, buildSatelliteBlock, buildEntry, buildFrame, parseReply, FrameDecoder } = proto;

// ---- CRC 已知值验证 ----
// 固件 CRC_Calculate（crc.c）：init=0, poly=0x1021, 无反射无 xorout
// = CRC-16/XMODEM 变体，check("123456789") = 0x31C3
assert.equal(crc16(new TextEncoder().encode("123456789")), 0x31c3, "CRC16 known vector");
// CRC-8(poly 0x07)("123456789") = 0xF4
assert.equal(crc8(new TextEncoder().encode("123456789")), 0xf4, "CRC8 known vector");
console.log("✓ CRC16/CRC8 已知向量通过");

// ---- 卫星块构建 ----
const sat = buildSatelliteBlock({
  name: "ISS",
  startTime: [26, 8, 17, 12, 0, 0],
  endTime: [26, 8, 17, 12, 10, 0],
  sumTime: 600,
  sendCtcss: 0,
  startUnix: 840000000,
});
assert.equal(sat.length, 32, "satellite block is 32 bytes");
assert.equal(sat[9], 0, "name[9] == 0");
assert.equal(crc8(sat.subarray(0, 30)), sat[30], "CRC8 matches");
assert.equal(new DataView(sat.buffer).getUint32(26, true), 840000000, "start_unix");
console.log("✓ 卫星块构建通过");

// ---- 条目构建 ----
// 43850000 = 0x029D1910, 43750000 = 0x029B9270 (LE)
const entry = buildEntry(43850000, 43750000);
assert.deepEqual([...entry], [16, 25, 157, 2, 112, 146, 155, 2]);
console.log("✓ 条目构建通过 (438.5 MHz / 437.5 MHz)");

// ---- 命令帧构建 + 解码往返 ----
const frames = [];
frames.push(buildFrame(CMD.DOPPLER_ERASE, new Uint8Array(0)));
frames.push(buildFrame(CMD.DOPPLER_WRITE_SAT, sat));
frames.push(buildFrame(CMD.DOPPLER_WRITE_ENTRY, (() => {
  const b = new Uint8Array(12);
  const dv = new DataView(b.buffer);
  dv.setUint16(0, 5, true); // index 5
  b.set(entry, 4);
  return b;
})()));

const dec = new FrameDecoder();
const decoded = [];
for (const f of frames) {
  // 模拟串口分片
  for (let i = 0; i < f.length; i += 3) {
    decoded.push(...dec.push(f.slice(i, i + 3)));
  }
}
assert.equal(decoded.length, 3, "3 frames decoded");
// erase: 载荷只有 4B Header
assert.equal(decoded[0].length, 4);
assert.equal(new DataView(decoded[0].buffer).getUint16(0, true), CMD.DOPPLER_ERASE);
// sat 帧
const satReply = decoded[1];
assert.equal(new DataView(satReply.buffer).getUint16(0, true), CMD.DOPPLER_WRITE_SAT);
assert.deepEqual([...satReply.subarray(4)], [...sat], "satellite payload intact");
// entry 帧
const entReply = decoded[2];
assert.equal(new DataView(entReply.buffer).getUint16(4, true), 5, "entry index intact");
assert.deepEqual([...entReply.subarray(8)], [...entry], "entry payload intact");
console.log("✓ 命令帧构建/分片解码往返通过");

// ---- 回复解析 ----
const reply = new Uint8Array([0xe3, 0x05, 0x01, 0x00, 0x00]); // id=0x05E3 size=1 status=0
const parsed = parseReply(reply);
assert.equal(parsed.id, CMD.REPLY_ERASE);
assert.equal(parsed.status, 0);
console.log("✓ 回复解析通过");

// ---- 乱序/噪声字节下重同步 ----
const noisy = new Uint8Array([0xff, 0x00, ...frames[1], 0xaa, 0xbb]);
const dec2 = new FrameDecoder();
const out2 = dec2.push(noisy);
assert.equal(out2.length, 1, "noisy stream yields 1 frame");
assert.equal(new DataView(out2[0].buffer).getUint16(0, true), CMD.DOPPLER_WRITE_SAT);
console.log("✓ 噪声重同步通过");

console.log("\n全部协议测试通过 ✅");
