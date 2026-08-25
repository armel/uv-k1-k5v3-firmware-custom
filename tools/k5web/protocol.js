/**
 * K5Web-like Doppler programming tool - serial protocol module.
 * UMD: 浏览器挂 window.K5WEB.protocol，Node 用 require('./protocol.js')。
 *
 * Implements the F4HWN USB CDC frame protocol used by App/app/uart.c:
 *
 *   frame = [AB CD][Size LE][payload (XOR-obfuscated)][CRC16 LE][DC BA]
 *
 *   - Header ID 0xCDAB (little-endian), Size = payload length
 *   - payload = command struct (its own Header_t ID+Size + data)
 *   - CRC16 = CCITT-FALSE (poly 0x1021, init 0) over the PLAINTEXT payload
 *   - payload is obfuscated with a 16-byte XOR key before transmission
 *   - Footer ID 0xBADC
 *
 * Doppler commands (see App/app/uart.c):
 *   0x05E0 erase Doppler area           -> reply 0x05E3 {status}
 *   0x05E1 write satellite block (32B)  -> reply 0x05E4 {status}
 *   0x05E2 write table entry (12B)      -> reply 0x05E5 {status}
 *   status: 0 = OK, 1 = rejected
 */

const OBFUSCATION = [
  0x16, 0x6c, 0x14, 0xe6, 0x2e, 0x91, 0x0d, 0x40,
  0x21, 0x35, 0xd5, 0x40, 0x13, 0x03, 0xe9, 0x80,
];

const CMD = {
  DOPPLER_ERASE: 0x05e0,
  DOPPLER_WRITE_SAT: 0x05e1,
  DOPPLER_WRITE_ENTRY: 0x05e2,
  REPLY_ERASE: 0x05e3,
  REPLY_WRITE_SAT: 0x05e4,
  REPLY_WRITE_ENTRY: 0x05e5,
};

/** CRC-16/CCITT-FALSE, matches App/driver/crc.c CRC_Calculate(). */
function crc16(data) {
  let crc = 0;
  for (let i = 0; i < data.length; i++) {
    crc ^= data[i] << 8;
    for (let j = 0; j < 8; j++) {
      crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
    }
  }
  return crc;
}

/** CRC-8 (poly 0x07), matches App/app/doppler.c DOPPLER_Crc8(). */
function crc8(data) {
  let crc = 0;
  for (let i = 0; i < data.length; i++) {
    crc ^= data[i];
    for (let b = 0; b < 8; b++) {
      crc = crc & 0x80 ? ((crc << 1) ^ 0x07) & 0xff : (crc << 1) & 0xff;
    }
  }
  return crc;
}

/** Builds a DOPPLER_Satellite_t block (32 bytes) as stored at 0x1D0000.
 *  Layout (must match doppler.h, no padding):
 *    0..3 start_unix u32 | 4..13 name | 14..19 start_time | 20..25 end_time
 *    26..27 sum_time u16 | 28..29 send_ctcss u16 | 30 crc8 | 31 reserved */
function buildSatelliteBlock({ name, startTime, endTime, sumTime, sendCtcss, startUnix }) {
  const buf = new Uint8Array(32);
  const dv = new DataView(buf.buffer);
  const enc = new TextEncoder();
  dv.setUint32(0, startUnix, true);
  const nb = enc.encode(name.slice(0, 9));
  buf.set(nb, 4); // name[13] stays 0 -> valid marker
  buf.set(startTime, 14); // year(2000-based)/month/day/hour/minute/second
  buf.set(endTime, 20);
  dv.setUint16(26, sumTime, true);
  dv.setUint16(28, sendCtcss, true); // Hz/10, 0 = none
  buf[30] = crc8(buf.subarray(0, 30));
  buf[31] = 0;
  return buf;
}

/** Builds a DOPPLER_Entry_t (8 bytes): uplink/downlink in 10 Hz units. */
function buildEntry(uplink10Hz, downlink10Hz) {
  const buf = new Uint8Array(8);
  const dv = new DataView(buf.buffer);
  dv.setUint32(0, uplink10Hz, true);
  dv.setUint32(4, downlink10Hz, true);
  return buf;
}

/** Builds a full command frame for the radio. */
function buildFrame(commandId, payload) {
  // body = command struct (its own Header_t ID+Size + data)
  const p = new Uint8Array(4 + payload.length);
  const dv = new DataView(p.buffer);
  dv.setUint16(0, commandId, true);
  dv.setUint16(2, payload.length, true);
  p.set(payload, 4);

  const crc = crc16(p);

  // CRITICAL: the firmware decrypts Size+2 bytes = body + CRC (uart.c:802),
  // so the CRC MUST be included in the obfuscated region.
  const enc = new Uint8Array(p.length + 2);
  enc.set(p, 0);
  const edv = new DataView(enc.buffer);
  edv.setUint16(p.length, crc, true);

  const frame = new Uint8Array(8 + enc.length);
  frame[0] = 0xab; frame[1] = 0xcd;
  frame[2] = p.length & 0xff; frame[3] = (p.length >> 8) & 0xff; // Size = body length only
  for (let i = 0; i < enc.length; i++) frame[4 + i] = enc[i] ^ OBFUSCATION[i % 16];
  frame[4 + enc.length] = 0xdc;
  frame[5 + enc.length] = 0xba;
  return frame;
}

/** Parses a reply frame (plaintext payload already accumulated). */
function parseReply(payload) {
  const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const id = dv.getUint16(0, true);
  const size = dv.getUint16(2, true);
  const status = payload.length >= 5 ? payload[4] : 0xff;
  return { id, size, status };
}

/** Frame decoder: feeds bytes, yields complete plaintext payloads. */
class FrameDecoder {
  constructor() {
    this.buf = new Uint8Array(0);
  }
  push(chunk) {
    const out = [];
    this.buf = concat(this.buf, new Uint8Array(chunk));
    for (;;) {
      // find header
      let h = -1;
      for (let i = 0; i < this.buf.length - 1; i++) {
        if (this.buf[i] === 0xab && this.buf[i + 1] === 0xcd) { h = i; break; }
      }
      if (h < 0) { this.buf = this.buf.slice(-1); break; } // keep 1 byte for partial header
      if (h > 0) this.buf = this.buf.slice(h);
      if (this.buf.length < 6) break; // need size + first payload bytes
      const size = this.buf[2] | (this.buf[3] << 8);
      const total = 8 + size;
      if (this.buf.length < total) break;
      // footer check
      if (this.buf[4 + size + 2] !== 0xdc || this.buf[4 + size + 3] !== 0xba) {
        this.buf = this.buf.slice(2); // resync
        continue;
      }
      // 回复帧的 2 字节是 Padding (加密填充), 不是 CRC (uart.c SendReply) - 不校验
      const plain = new Uint8Array(size);
      for (let i = 0; i < size; i++) plain[i] = this.buf[4 + i] ^ OBFUSCATION[i % 16];
      out.push(plain);
      this.buf = this.buf.slice(total);
    }
    return out;
  }
}

function concat(a, b) {
  const c = new Uint8Array(a.length + b.length);
  c.set(a, 0);
  c.set(b, a.length);
  return c;
}


// ---- UMD 导出（浏览器 window.K5WEB / Node module.exports） ----
(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
  } else {
    root.K5WEB = root.K5WEB || {};
    root.K5WEB.protocol = factory();
  }
})(typeof self !== "undefined" ? self : this, function () {
  return {
    OBFUSCATION, CMD, crc16, crc8,
    buildSatelliteBlock, buildEntry, buildFrame, parseReply, FrameDecoder,
  };
});
