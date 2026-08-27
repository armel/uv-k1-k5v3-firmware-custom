/**
 * Node 单元测试：协议封包/解包 + 卫星块/条目构建。
 * 运行：node tools/k5web/test_protocol.mjs
 */
import { strict as assert } from "node:assert";
import proto from "./protocol.js";
const { OBFUSCATION, CMD, crc16, crc8, buildSatelliteBlock, buildEntry, buildFrame, parseReply, FrameDecoder } = proto;

// ---- 写频辅助函数测试 ----
const { CHAN, CODE_TYPE, MODULATION, TX_DIR, BANDWIDTH, POWER, bandFromFrequency, ctcssIndex, dcsIndex, buildChannelBlock, buildChannelAttributes } = proto;

assert.equal(bandFromFrequency(43950000), 5, "439.5 MHz is UHF band 5");
assert.equal(bandFromFrequency(14550000), 2, "145.5 MHz is VHF band 2");
assert.equal(ctcssIndex(885), 8, "88.5 Hz CTCSS index");
assert.equal(ctcssIndex(670), 0, "67.0 Hz CTCSS index");
assert.equal(dcsIndex(0x023), 6, "DCS 023 index");

const chBlock = buildChannelBlock({
  rxFreq10Hz: 43950000, txFreq10Hz: 43450000,
  rxCodeType: CODE_TYPE.CTCSS, rxCode: ctcssIndex(885),
  txCodeType: CODE_TYPE.OFF, txCode: 0,
  modulation: MODULATION.FM, txDir: TX_DIR.OFF,
  bandwidth: BANDWIDTH.WIDE, power: POWER.HIGH,
  txLock: 0, bcl: 0, freqReverse: 0, pttId: 0, step: 4,
});
const chDv = new DataView(chBlock.buffer);
assert.equal(chDv.getUint32(0, true), 43950000, "channel RX freq");
assert.equal(chDv.getUint32(4, true), 43450000, "channel TX freq");
assert.equal(chBlock[10], (CODE_TYPE.OFF << 4) | CODE_TYPE.CTCSS, "tone type nibble");
assert.equal(chBlock[11], (MODULATION.FM << 4) | TX_DIR.OFF, "modulation/dir nibble");
assert.equal(chBlock[12], (POWER.HIGH << 2) | (BANDWIDTH.WIDE << 1), "power/bandwidth byte");

const attr = buildChannelAttributes({ band: 5, compander: 0, exclude: 0, scanlist: 0 });
const attrDv = new DataView(attr.buffer);
assert.equal(attrDv.getUint16(0, true), 5, "attributes band = 5");
const attr2 = buildChannelAttributes({ band: 2, compander: 1, exclude: 1, scanlist: 3 });
const attr2Dv = new DataView(attr2.buffer);
assert.equal(attr2Dv.getUint16(0, true), (2) | (1 << 3) | (1 << 7) | (3 << 8), "attributes full fields");
console.log("✓ 写频辅助函数通过");

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
// 布局（与 doppler.h 一致）：0..3 start_unix | 4..13 name | 14..19 start | 20..25 end | 26..27 sum | 28..29 ctcss | 30 crc | 31 reserved
assert.equal(new DataView(sat.buffer).getUint32(0, true), 840000000, "start_unix at offset 0");
assert.equal(String.fromCharCode(sat[4], sat[5], sat[6]), "ISS", "name at offset 4");
assert.equal(sat[13], 0, "name[9] == 0 (absolute offset 13)");
assert.deepEqual([...sat.subarray(14, 20)], [26, 8, 17, 12, 0, 0], "start_time at offset 14");
assert.deepEqual([...sat.subarray(20, 26)], [26, 8, 17, 12, 10, 0], "end_time at offset 20");
assert.equal(new DataView(sat.buffer).getUint16(26, true), 600, "sum_time at offset 26");
assert.equal(crc8(sat.subarray(0, 30)), sat[30], "CRC8 matches");
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

// ---- 中文字库命令（与 App/app/cnfont.h 一致）----
assert.equal(proto.CN_FONT.FLASH_SIZE, 282752, "font region is 94*94*32");
assert.equal(proto.CN_FONT.SECTOR_COUNT, 70, "font region is 70 sectors");
assert.equal(CMD.REPLY_FONT_ERASE, CMD.CN_FONT_ERASE + 3, "erase reply id = cmd + 3");
assert.equal(CMD.REPLY_FONT_WRITE, CMD.CN_FONT_WRITE + 3, "write reply id = cmd + 3");

// 擦除帧：{u16 sectorIndex, u16 padding}
const eraseFrame = buildFrame(CMD.CN_FONT_ERASE, new Uint8Array([69, 0, 0, 0]));
// 写入帧：{u32 offset, CHUNK 字节 data}（最大分块）
const chunk = new Uint8Array(4 + proto.CN_FONT.CHUNK);
new DataView(chunk.buffer).setUint32(0, 0x120, true);
for (let i = 0; i < proto.CN_FONT.CHUNK; i++) chunk[4 + i] = i & 0xff;
const writeFrame = buildFrame(CMD.CN_FONT_WRITE, chunk);

// 整帧必须严格小于固件 256B 接收环：等于 256 会让写指针回卷后与读指针重合，
// 被 UART_IsCommandAvailable 误判为空而丢帧（字库刷入"回复超时 0x5e7"的根因）
assert.ok(writeFrame.length < 256, `write frame ${writeFrame.length}B must be < 256B RX ring`);

const dec3 = new FrameDecoder();
const out3 = [...dec3.push(eraseFrame), ...dec3.push(writeFrame)];
assert.equal(out3.length, 2, "2 font frames decoded");
assert.equal(new DataView(out3[0].buffer).getUint16(0, true), CMD.CN_FONT_ERASE);
assert.equal(new DataView(out3[0].buffer).getUint16(4, true), 69, "sector index intact");
assert.equal(new DataView(out3[1].buffer).getUint16(0, true), CMD.CN_FONT_WRITE);
assert.equal(new DataView(out3[1].buffer).getUint32(4, true), 0x120, "write offset intact");
assert.deepEqual([...out3[1].subarray(8)], [...chunk.subarray(4)], "chunk intact");
console.log("✓ 字库命令帧构建/解码通过");

// ---- 固件刷写帧（bootloader 协议）----
const { FLASH_MSG, buildFlashFrame, buildFwPage, parseDevInfo, blVersionOK } = proto;

// 握手帧 0x0530（版本前 4 字符）
const hsFrame = buildFlashFrame(FLASH_MSG.NOTIFY_BL_VER, new TextEncoder().encode("7.00"));
// 编程帧 0x0519：页 3/10，数据 0x00..0xFF
const pageData = new Uint8Array(256);
for (let i = 0; i < 256; i++) pageData[i] = i;
const progFrame = buildFlashFrame(FLASH_MSG.PROG_FW, buildFwPage(0x11223344, 3, 10, pageData));

const dec4 = new FrameDecoder();
const out4 = [...dec4.push(hsFrame), ...dec4.push(progFrame)];
assert.equal(out4.length, 2, "2 flash frames decoded");
assert.equal(new DataView(out4[0].buffer).getUint16(0, true), FLASH_MSG.NOTIFY_BL_VER);
assert.equal(String.fromCharCode(...out4[0].subarray(4, 8)), "7.00", "BL version echo intact");
const progMsg = new DataView(out4[1].buffer);
assert.equal(progMsg.getUint16(0, true), FLASH_MSG.PROG_FW);
assert.equal(progMsg.getUint16(2, true), 268, "page data length 268");
assert.equal(progMsg.getUint32(4, true), 0x11223344, "timestamp intact");
assert.equal(progMsg.getUint16(8, true), 3, "page index intact");
assert.equal(progMsg.getUint16(10, true), 10, "page count intact");
assert.deepEqual([...out4[1].subarray(16)], [...pageData], "256B page intact");

// 0x0518 设备信息解析
const devInfo = new Uint8Array(4 + 32);
new DataView(devInfo.buffer).setUint16(0, FLASH_MSG.NOTIFY_DEV_INFO, true);
new DataView(devInfo.buffer).setUint16(2, 32, true);
for (let i = 0; i < 16; i++) devInfo[4 + i] = 0xa0 + i;
devInfo.set(new TextEncoder().encode("7.00.07"), 4 + 16);
const di = parseDevInfo(devInfo);
assert.equal(di.version, "7.00.07", "BL version parsed");
assert.ok(di.uid.startsWith("a0a1"), "UID parsed");
assert.ok(blVersionOK("7.00.07") && blVersionOK("7.01.00") && blVersionOK("8.00.00"), "acceptable versions");
assert.ok(!blVersionOK("6.99.99") && !blVersionOK("7.00.06") && !blVersionOK("abc"), "rejected versions");
console.log("✓ 固件刷写帧构建/解码通过");

// ---- 校准数据 EEPROM 读写帧 ----
// 读请求 0x051B: {u16 offset, u8 size, u8 pad, u32 ts}
const rdPayload = new Uint8Array(8);
const rdDv = new DataView(rdPayload.buffer);
rdDv.setUint16(0, 0xb000, true);
rdDv.setUint8(2, 128);
rdDv.setUint32(4, proto.CALIB.TS, true);
// 写请求 0x051D: {u16 offset, u8 size, u8 flag, u32 ts, 16B data}
const wrPayload = new Uint8Array(24);
const wrDv = new DataView(wrPayload.buffer);
wrDv.setUint16(0, 0xb100, true);
wrDv.setUint8(2, 16);
wrDv.setUint8(3, 1);
wrDv.setUint32(4, proto.CALIB.TS, true);
for (let i = 0; i < 16; i++) wrPayload[8 + i] = 0x55;

const dec5 = new FrameDecoder();
const out5 = [...dec5.push(buildFrame(CMD.READ_EEPROM, rdPayload)), ...dec5.push(buildFrame(CMD.WRITE_EEPROM, wrPayload))];
assert.equal(out5.length, 2, "2 eeprom frames decoded");
const rMsg = new DataView(out5[0].buffer);
assert.equal(rMsg.getUint16(0, true), CMD.READ_EEPROM);
assert.equal(rMsg.getUint16(4, true), 0xb000, "read offset intact");
assert.equal(out5[0][6], 128, "read size intact");
assert.equal(rMsg.getUint32(8, true), proto.CALIB.TS, "read timestamp intact");
const wMsg = new DataView(out5[1].buffer);
assert.equal(wMsg.getUint16(0, true), CMD.WRITE_EEPROM);
assert.equal(wMsg.getUint16(4, true), 0xb100, "write offset intact");
assert.equal(out5[1][6], 16, "write size intact");
assert.equal(out5[1][7], 1, "write flag intact");
assert.equal(wMsg.getUint32(8, true), proto.CALIB.TS, "write timestamp intact");
assert.deepEqual([...out5[1].subarray(12, 28)], new Array(16).fill(0x55), "16B write data intact");
assert.equal(proto.CALIB.SIZE, 512, "calibration region is 512 bytes");
console.log("✓ 校准数据读写帧构建/解码通过");

console.log("\n全部协议测试通过 ✅");
