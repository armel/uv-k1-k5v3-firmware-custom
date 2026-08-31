/**
 * Node 单元测试：星历导出（纯网页端，无需串口）。
 * 用真实 TLE + findPass 计算过境，执行与 app.js 导出按钮相同的
 * buildEphemExport / buildEphemCsv 逻辑，校验 JSON / CSV 内容。
 * 运行：node tools/k5web/test_ephem_export.mjs
 */
import { strict as assert } from "node:assert";
import calc from "./calc.js";

// ---- 与 app.js 导出段保持一致的序列化逻辑 ----
const fmtExportBeijing = (date) => {
  const bj = new Date(Math.round(date.getTime() / 1000) * 1000 + 8 * 3600 * 1000); // 北京时间（UTC+8），显示按秒
  const p = (n, l = 2) => String(n).padStart(l, "0");
  return `${bj.getUTCFullYear()}-${p(bj.getUTCMonth() + 1)}-${p(bj.getUTCDate())} ${p(bj.getUTCHours())}:${p(bj.getUTCMinutes())}:${p(bj.getUTCSeconds())}`;
};

function buildEphemExport({ passData, tleText, satName, fUp, fDown, ctcss, lat, lon, alt, minEl }) {
  const tleLines = tleText.trim().split(/\r?\n/).filter((l) => l.trim());
  if (tleLines.length < 2) throw new Error("缺少 TLE 两行数据");
  const tle1 = tleLines[tleLines.length - 2].trim();
  const tle2 = tleLines[tleLines.length - 1].trim();
  if (!/^1 [0-9]{5}/.test(tle1) || !/^2 [0-9]{5}/.test(tle2)) {
    throw new Error("TLE 格式不正确（应为 1/2 开头的两行数据）");
  }
  const pass = passData;
  const entries = pass.entries.map((en, i) => {
    const bj = new Date((en.unix + 8 * 3600) * 1000);
    return {
      t: i,
      unix: en.unix,
      beijing: bj.toISOString().replace("T", " ").slice(0, 19),
      uplink10Hz: en.uplink,
      downlink10Hz: en.downlink,
      uplinkMHz: +(en.uplink / 1e5).toFixed(5),
      downlinkMHz: +(en.downlink / 1e5).toFixed(5),
      altitudeKm: en.altitudeKm,
      distanceKm: en.distanceKm,
      azimuthDeg: +en.azimuthDeg.toFixed(1),
      elevationDeg: +en.elevationDeg.toFixed(1),
    };
  });
  return {
    tool: "k5web 多普勒星历导出（调试诊断）",
    exportedAt: new Date().toISOString(),
    satellite: {
      name: satName,
      tle1, tle2,
      uplinkMHz: parseFloat(fUp),
      downlinkMHz: parseFloat(fDown),
      ctcssHz: (parseInt(ctcss, 10) || 0) / 10,
    },
    observer: {
      latDeg: parseFloat(lat),
      lonDeg: parseFloat(lon),
      altM: parseFloat(alt),
      minElevationDeg: parseFloat(minEl),
    },
    pass: {
      startBeijing: fmtExportBeijing(pass.start),
      endBeijing: fmtExportBeijing(pass.end),
      durationS: pass.durationS,
      entryCount: entries.length,
      startUnix2000: calc.unixToFw(Math.floor(pass.start.getTime() / 1000)),
    },
    entries,
  };
}

function buildEphemCsv(data) {
  const head = ["t_sec", "unix", "beijing", "uplinkMHz", "downlinkMHz",
                "altitudeKm", "distanceKm", "azimuthDeg", "elevationDeg"];
  const lines = [head.join(",")];
  for (const e of data.entries) {
    lines.push([e.t, e.unix, e.beijing, e.uplinkMHz, e.downlinkMHz,
                e.altitudeKm, e.distanceKm, e.azimuthDeg, e.elevationDeg].join(","));
  }
  return "\uFEFF" + lines.join("\r\n") + "\r\n";
}

// ---- 用真实 TLE 计算过境 ----
const tle1 = "1 25544U 98067A   26080.00000000  .00016717  00000-0  10270-3 0  9000";
const tle2 = "2 25544  51.6400 234.2345 0006043 141.3553 275.5311 15.50836567000002";
const pass = calc.findPass({
  tle1, tle2,
  latDeg: 31.23, lonDeg: 121.47, altKm: 0.011, // 上海
  uplinkMHz: 145.99, downlinkMHz: 437.8,
  minElevation: 0,
  searchStart: new Date(Date.now() + 60 * 60 * 1000),
  maxSearchHours: 24,
});
assert.ok(pass, "24h 窗口内应有 ISS 过境");

const d = buildEphemExport({
  passData: pass,
  tleText: `ISS (ZARYA)\n${tle1}\n${tle2}\n`, // 模拟页面 textarea 含名称行
  satName: "ISS",
  fUp: "145.99", fDown: "437.8", ctcss: "0",
  lat: "31.23", lon: "121.47", alt: "11", minEl: "0",
});

// ---- JSON 内容断言 ----
assert.equal(d.satellite.name, "ISS", "卫星名");
assert.equal(d.satellite.tle1, tle1, "TLE 第一行（取最后两行，忽略名称行）");
assert.equal(d.satellite.tle2, tle2, "TLE 第二行");
assert.equal(d.satellite.uplinkMHz, 145.99, "上行频率");
assert.equal(d.satellite.downlinkMHz, 437.8, "下行频率");
assert.equal(d.satellite.ctcssHz, 0, "亚音 0 = 无");
assert.equal(d.observer.latDeg, 31.23, "纬度");
assert.equal(d.observer.altM, 11, "海拔");

assert.equal(d.pass.durationS, pass.durationS, "时长一致");
assert.equal(d.pass.entryCount, pass.entries.length, "条目数一致");
assert.equal(d.pass.startBeijing, fmtExportBeijing(pass.start), "过境开始（北京时间）");
assert.equal(d.pass.endBeijing, fmtExportBeijing(pass.end), "过境结束（北京时间）");
assert.equal(d.pass.startUnix2000,
  calc.unixToFw(Math.floor(pass.start.getTime() / 1000)), "固件基准 start_unix");

const e0 = d.entries[0];
assert.equal(e0.t, 0, "第一条 t=0");
assert.equal(e0.unix, pass.entries[0].unix, "unix 秒一致");
assert.equal(e0.uplink10Hz, pass.entries[0].uplink, "10Hz 原始值一致");
assert.equal(e0.uplinkMHz, +(pass.entries[0].uplink / 1e5).toFixed(5), "MHz 换算一致");
assert.equal(e0.altitudeKm, pass.entries[0].altitudeKm, "高度一致");
assert.ok(e0.beijing.length === 19 && /^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/.test(e0.beijing),
  "北京时间格式 YYYY-MM-DD HH:MM:SS");
// 相邻条目时间差 1 秒
const e1 = d.entries[1];
assert.equal(e1.t, 1, "第二条 t=1");
assert.equal(e1.unix - e0.unix, 1, "每秒一条");
// 过境首尾时间顺序（pass.start 带毫秒，表起点 floor 到整秒，允许 ±1 秒）
const startUnixSec = Math.floor(pass.start.getTime() / 1000);
assert.ok(Math.abs(d.entries[0].unix - startUnixSec) <= 1, "首条 unix ≈ 过境开始");
assert.equal(d.entries[0].beijing.slice(0, 19), fmtExportBeijing(new Date(startUnixSec * 1000)).slice(0, 19),
  "首条北京时间 = 过境开始（取整秒）");
// 频率表覆盖全窗口
assert.equal(d.entries.length, Math.min(1020, pass.durationS + 1), "条目数 = min(1020, 时长+1)");

// ---- CSV 断言 ----
const csv = buildEphemCsv(d);
assert.ok(csv.startsWith("\uFEFF"), "CSV 带 UTF-8 BOM（Excel 中文不乱码）");
const csvLines = csv.replace(/^\uFEFF/, "").split("\r\n").filter((l) => l.length > 0);
assert.equal(csvLines.length, d.entries.length + 1, "CSV 行数 = 表头 + 条目");
assert.equal(csvLines[0],
  "t_sec,unix,beijing,uplinkMHz,downlinkMHz,altitudeKm,distanceKm,azimuthDeg,elevationDeg", "表头");
const row1 = csvLines[1].split(",");
assert.equal(row1.length, 9, "数据行 9 列");
assert.equal(row1[0], "0", "首行 t=0");
assert.equal(row1[3], String(e0.uplinkMHz), "首行上行 MHz");
assert.equal(row1[4], String(e0.downlinkMHz), "首行下行 MHz");
// 每行 t 递增且时间连续
for (let i = 1; i < csvLines.length; i++) {
  const r = csvLines[i].split(",");
  assert.equal(r[0], String(i - 1), `第 ${i} 行 t_sec`);
}

console.log(`✓ 导出流程通过：ISS 过境 ${d.pass.durationS}s / ${d.entries.length} 条`);
console.log("✓ JSON 含 TLE/观测位置/过境窗口/频率表，字段换算与计算模块一致");
console.log("✓ CSV 带 BOM、9 列、逐秒连续");
