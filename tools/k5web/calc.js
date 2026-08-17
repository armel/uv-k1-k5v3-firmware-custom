/**
 * 多普勒计算模块（浏览器 / Node 通用，纯函数）
 * UMD: 浏览器挂 window.K5WEB.calc（依赖 window.satellite），Node require('./calc.js')。
 *
 * 公式（ECI 惯性系相对速度法）：
 *   vr = (v_sat - v_obs) · r̂   （>0 = 卫星远离观测者）
 *   r̂ 从观测者指向卫星，v_obs = ω × r_obs（地球自转）
 *
 *   下行（卫星 → 对讲机）：对讲机接收频率 = f_down × (1 - vr/c)
 *   上行（对讲机 → 卫星）：对讲机发射频率 = f_up / (1 + vr/c)
 *        （保证卫星端收到恰好 f_up）
 *
 * 频率单位：Hz（表内存储时 /10 转 10Hz 单位）
 */

// satellite 库由 UMD 注入（浏览器 window.satellite / Node require）
let satellite = null;

const C_KM_S = 299792.458; // 光速 km/s
const OMEGA = 7.2921159e-5; // 地球自转角速度 rad/s

/** 观测者 ECI 速度（自转）: ω × r_obs */
function observerEciVelocity(obsPosEci) {
  return {
    x: -OMEGA * obsPosEci.y,
    y: OMEGA * obsPosEci.x,
    z: 0,
  };
}

/** 卫星相对观测者的径向速度 vr (km/s)，>0 = 远离 */
function radialVelocity(satPosEci, satVelEci, obsPosEci) {
  const rx = satPosEci.x - obsPosEci.x;
  const ry = satPosEci.y - obsPosEci.y;
  const rz = satPosEci.z - obsPosEci.z;
  const range = Math.sqrt(rx * rx + ry * ry + rz * rz);
  if (range < 1e-9) return 0;
  const vObs = observerEciVelocity(obsPosEci);
  const vrx = satVelEci.x - vObs.x;
  const vry = satVelEci.y - vObs.y;
  const vrz = satVelEci.z - vObs.z;
  return (vrx * rx + vry * ry + vrz * rz) / range;
}

/** 对讲机侧需要使用的上行频率（保证卫星收到 fUp） */
function uplinkFreq(fUpHz, vr) {
  return fUpHz / (1 + vr / C_KM_S);
}

/** 对讲机侧收到的下行频率 */
function downlinkFreq(fDownHz, vr) {
  return fDownHz * (1 - vr / C_KM_S);
}

/**
 * 从 t0 起查找最近一次可见过境窗口（仰角 > minElevation 度）。
 * 返回 { start: Date, end: Date, entries: [{unix, uplink, downlink}] }
 * entries 每 2 秒一条（多普勒已补偿，10Hz 单位），最多 1920 条（32 分钟）。
 */
function findPass({
  tle1, tle2,
  latDeg, lonDeg, altKm,
  uplinkMHz, downlinkMHz,
  minElevation = 0,
  searchStart = new Date(),
  maxSearchHours = 24,
  maxPassSeconds = 32 * 60,
}) {
  const satrec = satellite.twoline2satrec(tle1, tle2);
  const obsGd = {
    longitude: satellite.degreesToRadians(lonDeg),
    latitude: satellite.degreesToRadians(latDeg),
    height: altKm,
  };
  const obsEcf = satellite.geodeticToEcf(obsGd);

  // 观测者 ECI 位置随地球自转变化，按时刻精确计算：ECI = Rz(gst) * ECF
  function obsEciAt(date) {
    const gst = satellite.gstime(date);
    const c = Math.cos(gst), s = Math.sin(gst);
    return {
      x: c * obsEcf.x + s * obsEcf.y,
      y: -s * obsEcf.x + c * obsEcf.y,
      z: obsEcf.z,
    };
  }

  function elevationAt(date) {
    const pv = satellite.propagate(satrec, date);
    if (pv.position === undefined) return null;
    const posEcf = satellite.eciToEcf(pv.position, satellite.gstime(date));
    // 观测者 ECF -> 局部 ENU，计算仰角（不依赖库中损坏的 ecfToLookAngles）
    const lam = obsGd.longitude, phi = obsGd.latitude;
    const dx = posEcf.x - obsEcf.x;
    const dy = posEcf.y - obsEcf.y;
    const dz = posEcf.z - obsEcf.z;
    const topZ = Math.cos(phi) * Math.cos(lam) * dx + Math.cos(phi) * Math.sin(lam) * dy + Math.sin(phi) * dz;
    const range = Math.sqrt(dx * dx + dy * dy + dz * dz);
    if (range < 1e-9) return null;
    return satellite.radiansToDegrees(Math.asin(topZ / range));
  }

  // 粗扫（10 s 步进）找首个可见时刻
  const stepMs = 10 * 1000;
  const end = new Date(searchStart.getTime() + maxSearchHours * 3600 * 1000);
  let t = new Date(searchStart.getTime());
  let coarseStart = null;
  for (; t < end; t = new Date(t.getTime() + stepMs)) {
    const el = elevationAt(t);
    if (el !== null && el > minElevation) { coarseStart = t; break; }
  }
  if (!coarseStart) return null;

  // 细化窗口开始（从 coarseStart 前 2 分钟逐秒回溯，找仰角首次 > 阈值的时刻）
  const back = new Date(coarseStart.getTime() - 120 * 1000);
  let passStart = coarseStart;
  for (let tt = back; tt < coarseStart; tt = new Date(tt.getTime() + 1000)) {
    const el = elevationAt(tt);
    if (el !== null && el > minElevation) { passStart = tt; break; }
  }

  // 细化窗口结束（向后逐秒，仰角回落或达到 32 分钟上限）
  const maxEnd = new Date(passStart.getTime() + maxPassSeconds * 1000);
  let passEnd = maxEnd;
  for (let tt = new Date(passStart.getTime() + 1000); tt < maxEnd; tt = new Date(tt.getTime() + 1000)) {
    const el = elevationAt(tt);
    if (el !== null && el <= minElevation) { passEnd = tt; break; }
  }

  // 生成 2 s 步进表
  const entries = [];
  const durS = Math.round((passEnd.getTime() - passStart.getTime()) / 1000);
  const count = Math.min(1920, Math.ceil(durS / 2));
  for (let i = 0; i < count; i++) {
    const date = new Date(passStart.getTime() + i * 2000);
    const pv = satellite.propagate(satrec, date);
    if (pv.position === undefined || pv.velocity === undefined) {
      entries.push({ unix: 0, uplink: 0, downlink: 0 });
      continue;
    }
    const vr = radialVelocity(pv.position, pv.velocity, obsEciAt(date));
    entries.push({
      unix: Math.round(date.getTime() / 1000),
      uplink: Math.round(uplinkFreq(uplinkMHz * 1e6, vr) / 10),
      downlink: Math.round(downlinkFreq(downlinkMHz * 1e6, vr) / 10),
    });
  }

  return {
    satrec,
    start: passStart,
    end: passEnd,
    durationS: durS,
    entries,
  };
}

/** Date -> 固件 6 字节时间 [年2000, 月, 日, 时, 分, 秒]（UTC） */
function dateToFwTime(date) {
  return [
    date.getUTCFullYear() - 2000,
    date.getUTCMonth() + 1,
    date.getUTCDate(),
    date.getUTCHours(),
    date.getUTCMinutes(),
    date.getUTCSeconds(),
  ];
}

/** 1970 基准秒 -> 2000 基准秒（固件 start_unix） */
function unixToFw(unix1970) {
  return unix1970 - 946684800;
}


// ---- UMD 导出 ----
(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    satellite = require("./vendor/satellite.min.js");
    module.exports = factory();
  } else {
    satellite = root.satellite;
    root.K5WEB = root.K5WEB || {};
    root.K5WEB.calc = factory();
  }
})(typeof self !== "undefined" ? self : this, function () {
  return {
    radialVelocity, uplinkFreq, downlinkFreq,
    findPass, dateToFwTime, unixToFw,
  };
});
