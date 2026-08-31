// 验证: 用真实 TLE + 观测位置, 算未来 24h 所有 ISS 过境及峰值仰角, 对比 Look4Sat
import { createRequire } from 'module';
const require = createRequire(import.meta.url);
const satellite = require('./vendor/satellite.min.js');

const TLE1 = "1 25544U 98067A   26228.82508390  .00005081  00000+0  98725-4 0  9993";
const TLE2 = "2 25544  51.6333 359.9821 0007604  53.8870 306.2821 15.49464385581159";

// 观测位置: 淮安 (与 Look4Sat 同一位置, 请确认)
const OBS = { lat: 33.553202, lon: 119.033004, altKm: 0.0266 };

const satrec = satellite.twoline2satrec(TLE1, TLE2);

function obsEcef(date) {
  const gmst = satellite.gstime(date);
  const geodetic = {
    latitude: OBS.lat * Math.PI / 180,
    longitude: OBS.lon * Math.PI / 180,
    height: OBS.altKm,
  };
  return satellite.geodeticToEcf(geodetic);
}

function elevationAt(date) {
  const pv = satellite.propagate(satrec, date);
  if (!pv.position) return null;
  const gmst = satellite.gstime(date);
  const ecf = satellite.eciToEcf(pv.position, gmst);
  const obs = obsEcef(date);
  // ENU elevation
  const lat = OBS.lat * Math.PI / 180, lon = OBS.lon * Math.PI / 180;
  const dx = ecf.x - obs.x, dy = ecf.y - obs.y, dz = ecf.z - obs.z;
  const sinLat = Math.sin(lat), cosLat = Math.cos(lat);
  const sinLon = Math.sin(lon), cosLon = Math.cos(lon);
  const up = dx * cosLat * cosLon + dy * cosLat * sinLon + dz * sinLat;
  const range = Math.sqrt(dx * dx + dy * dy + dz * dz);
  return Math.asin(up / range) * 180 / Math.PI;
}

// 扫未来 24h, 步长 30s, 找所有 el>0 的过境段
const now = new Date("2026-08-17T12:35:00Z"); // 北京 20:35
console.log("当前(北京):", now.toLocaleString("zh-CN", { hour12: false }));
console.log("TLE epoch: 26228 (2026 第228天 = 8/16)\n");

let inPass = false, passStart = null, maxEl = -90, passes = [];
for (let t = new Date(now); t < new Date(now.getTime() + 24 * 3600e3); t = new Date(t.getTime() + 30000)) {
  const el = elevationAt(t);
  if (el === null) continue;
  if (el > 0 && !inPass) { inPass = true; passStart = t; maxEl = el; }
  else if (el > 0 && inPass) { maxEl = Math.max(maxEl, el); }
  else if (el <= 0 && inPass) {
    inPass = false;
    passes.push({ start: passStart, end: t, maxEl });
  }
}
console.log("未来 24h 所有过境 (北京时间, 仰角):");
for (const p of passes) {
  const f = (d) => d.toLocaleString("zh-CN", { hour12: false });
  console.log(`  ${f(p.start)} → ${f(p.end)}  峰值仰角 ${p.maxEl.toFixed(1)}°`);
}
