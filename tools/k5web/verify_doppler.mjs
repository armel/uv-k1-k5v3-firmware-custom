// 独立核对：多普勒频率表正确性
// 方法1: 解析径向速度 vs 数值距离微分 (验证 radialVelocity)
// 方法2: 下行 vs satellite.js dopplerFactor (库内独立实现)
// 方法3: 上行闭环仿真 — 电台按表发射, 卫星实际收到多少 (验证 uplinkFreq 符号)
import { createRequire } from 'module';
const require = createRequire(import.meta.url);
const satellite = require('./vendor/satellite.min.js');
const calc = require('./calc.js');

const TLE1 = "1 25544U 98067A   26228.82508390  .00005081  00000+0  98725-4 0  9993";
const TLE2 = "2 25544  51.6333 359.9821 0007604  53.8870 306.2821 15.49464385581159";
const OBS = { lat: 33.553202, lon: 119.033004, altKm: 0.0266 }; // 淮安
const F_UP = 145.990e6, F_DOWN = 437.800e6; // ISS 跨段中继
const C = 299792.458;

const pass = calc.findPass({
  tle1: TLE1, tle2: TLE2,
  latDeg: OBS.lat, lonDeg: OBS.lon, altKm: OBS.altKm,
  uplinkMHz: F_UP / 1e6, downlinkMHz: F_DOWN / 1e6,
  minElevation: 0,
  searchStart: new Date(Date.now() + 3600e3),
});
if (!pass) { console.log("24h 内无过境，换个时间再试"); process.exit(1); }

const satrec = pass.satrec;
const obsGd = { longitude: satellite.degreesToRadians(OBS.lon), latitude: satellite.degreesToRadians(OBS.lat), height: OBS.altKm };
const obsEcf = satellite.geodeticToEcf(obsGd);

// 某时刻的独立参考量
function refAt(date) {
  const gst = satellite.gstime(date);
  const pv = satellite.propagate(satrec, date);
  const posEcf = satellite.eciToEcf(pv.position, gst);
  // 数值距离微分: (r(t+0.5s) - r(t-0.5s)) / 1s
  const range = (d) => {
    const p = satellite.propagate(satrec, d).position;
    const pe = satellite.eciToEcf(p, satellite.gstime(d));
    return Math.hypot(pe.x - obsEcf.x, pe.y - obsEcf.y, pe.z - obsEcf.z);
  };
  const vrNum = (range(new Date(+date + 500)) - range(new Date(+date - 500))) / 1.0;
  // 库独立实现: dopplerFactor(观测者ECF, 卫星ECF位置, ECI速度按ECF旋转) = 1 - vr/c
  const velRot = satellite.eciToEcf(pv.velocity, gst); // 纯旋转(库用法如此)
  const dopFactor = satellite.dopplerFactor(obsEcf, posEcf, velRot);
  const vrAna = calc.radialVelocity(pv.position, pv.velocity, calc.observerEci(obsEcf, date));
  return { vrNum, dopFactor, vrAna };
}

console.log(`过境: ${pass.start.toISOString()} → ${pass.end.toISOString()} (${pass.durationS}s)\n`);

let maxVrErr = 0, maxDownErrHz = 0;
const rows = [];
for (const frac of [0.02, 0.25, 0.5, 0.75, 0.98]) {
  const t = new Date(pass.start.getTime() + frac * (pass.end - pass.start));
  const { vrNum, dopFactor, vrAna } = refAt(t);

  // 1) 解析 vr vs 数值距离微分
  maxVrErr = Math.max(maxVrErr, Math.abs(vrAna - vrNum));

  // 2) 下行: calc vs 库 dopplerFactor
  const downCalc = calc.downlinkFreq(F_DOWN, vrAna);
  const downRef = F_DOWN * dopFactor;
  maxDownErrHz = Math.max(maxDownErrHz, Math.abs(downCalc - downRef));

  // 3) 上行闭环: 电台发 f_tx, 卫星收到 f_tx·(1 - vr/c) (互易, 同一 vr)
  //    目标: 卫星恰好收到 F_UP。残差越小公式越对。
  const txCode = calc.uplinkFreq(F_UP, vrAna);        // 现公式 f/(1+vr/c)
  const txAlt  = F_UP / (1 - vrAna / C);               // 对照公式 f/(1-vr/c)
  const rxCode = txCode * (1 - vrAna / C);             // 卫星实际收到
  const rxAlt  = txAlt  * (1 - vrAna / C);

  rows.push({
    at: `${(frac * 100).toFixed(0)}%`,
    vr: vrAna.toFixed(2),
    downShift: ((downCalc - F_DOWN) / 1e3).toFixed(2),
    upShiftCode: ((txCode - F_UP) / 1e3).toFixed(2),
    upShiftAlt: ((txAlt - F_UP) / 1e3).toFixed(2),
    satRxErrCode: ((rxCode - F_UP) / 1e3).toFixed(2),
    satRxErrAlt: ((rxAlt - F_UP) / 1e3).toFixed(2),
  });
}

console.log("时刻 | vr(km/s) | 下行偏移kHz | 上行偏移kHz(现/对照) | 卫星收到误差kHz(现/对照)");
for (const r of rows)
  console.log(`${r.at.padStart(4)} | ${r.vr.padStart(8)} | ${r.downShift.padStart(11)} | ${r.upShiftCode.padStart(8)}/${r.upShiftAlt.padStart(8)} | ${r.satRxErrCode.padStart(10)}/${r.satRxErrAlt.padStart(8)}`);

console.log(`\n[1] 解析vr vs 数值距离微分 最大误差: ${(maxVrErr * 1e3).toFixed(3)} m/s  (应 <1 m/s)`);
console.log(`[2] 下行 calc vs 库 dopplerFactor 最大偏差: ${maxDownErrHz.toFixed(1)} Hz  (应 <1 Hz)`);
const worstCode = Math.max(...rows.map(r => Math.abs(parseFloat(r.satRxErrCode))));
const worstAlt  = Math.max(...rows.map(r => Math.abs(parseFloat(r.satRxErrAlt))));
console.log(`[3] 上行闭环 卫星接收最大误差: 现公式 ${worstCode.toFixed(2)} kHz vs 对照公式 ${worstAlt.toFixed(4)} kHz`);
console.log(worstAlt < worstCode
  ? "=> 结论: 上行应用 f/(1 - vr/c) (对照公式); 现公式符号相反, 过境边缘偏差约 2 倍多普勒量"
  : "=> 结论: 现上行公式正确");
