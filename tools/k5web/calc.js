/**
 * 多普勒计算模块（浏览器 / Node 通用，纯函数）
 * UMD: 浏览器挂 window.K5WEB.calc（依赖 window.satellite），Node require('./calc.js')。
 *
 * 公式（ECI 惯性系相对速度法）：
 *   vr = (v_sat - v_obs) · r̂   （>0 = 卫星远离观测者）
 *   r̂ 从观测者指向卫星，v_obs = ω × r_obs（地球自转）
 *
 *   下行（卫星 → 对讲机）：对讲机接收频率 = f_down × (1 - vr/c)
 *   上行（对讲机 → 卫星）：对讲机发射频率 = f_up / (1 - vr/c)
 *        （卫星作为接收端同样被一阶多普勒 f_tx·(1-vr/c) 偏移，
 *         此式保证卫星端收到恰好 f_up；AOS 时应低于标称值发射）
 *
 * 频率单位：Hz（表内存储时 /10 转 10Hz 单位）
 */

// satellite 库由 UMD 注入（浏览器 window.satellite / Node require）
let satellite = null;

const C_KM_S = 299792.458; // 光速 km/s
const OMEGA = 7.2921159e-5; // 地球自转角速度 rad/s
const EARTH_RADIUS_KM = 6371.0; // 地球平均半径，用于海拔近似

/**
 * 地固(ECF)位置 -> 惯性(ECI)位置，按时刻的格林尼治恒星时角 gst 旋转。
 *
 * 必须复用 satellite 库的 ecfToEci（Rz(-gst)）。ECI 相对地球已转过 +gst，
 * 观测者转回 ECI 需 -gst；若手写成 +gst 会把经度反演、观测者位置差数千公里，
 * 导致多普勒频偏符号/幅度全错（曾因此无法接收卫星信号）。
 * 不变量校验：ECI 经度 ≡ 地固经度 + gst（test_calc.mjs 中回归断言）。
 */
function observerEci(obsEcf, date) {
  return satellite.ecfToEci(obsEcf, satellite.gstime(date));
}

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

/** 从地固(ECF)卫星位置、观测者位置计算高度/距离/方位角/仰角。
 *  azimuth: 0..360 deg, elevation: -90..90 deg.
 */
function lookAngles(satPosEcf, obsPosEcf, obsLonRad, obsLatRad) {
  const dx = satPosEcf.x - obsPosEcf.x;
  const dy = satPosEcf.y - obsPosEcf.y;
  const dz = satPosEcf.z - obsPosEcf.z;

  const range = Math.sqrt(dx * dx + dy * dy + dz * dz);
  if (range < 1e-9) {
    return { altitudeKm: 0, distanceKm: 0, azimuthDeg: 0, elevationDeg: 0 };
  }

  const sinLon = Math.sin(obsLonRad);
  const cosLon = Math.cos(obsLonRad);
  const sinLat = Math.sin(obsLatRad);
  const cosLat = Math.cos(obsLatRad);

  // ENU (East-North-Up) from ECF difference
  const east = -sinLon * dx + cosLon * dy;
  const north = -sinLat * cosLon * dx - sinLat * sinLon * dy + cosLat * dz;
  const up = cosLat * cosLon * dx + cosLat * sinLon * dy + sinLat * dz;

  let azimuthDeg = satellite.radiansToDegrees(Math.atan2(east, north));
  if (azimuthDeg < 0) azimuthDeg += 360;
  const elevationDeg = satellite.radiansToDegrees(Math.asin(up / range));
  const altitudeKm = Math.sqrt(satPosEcf.x * satPosEcf.x + satPosEcf.y * satPosEcf.y + satPosEcf.z * satPosEcf.z) - EARTH_RADIUS_KM;

  return {
    altitudeKm: Math.round(altitudeKm),
    distanceKm: Math.round(range),
    azimuthDeg,
    elevationDeg,
  };
}

/** 对讲机侧需要使用的上行频率（保证卫星收到 fUp）；卫星接收 = f_tx·(1 - vr/c) */
function uplinkFreq(fUpHz, vr) {
  return fUpHz / (1 - vr / C_KM_S);
}

/** 对讲机侧收到的下行频率 */
function downlinkFreq(fDownHz, vr) {
  return fDownHz * (1 - vr / C_KM_S);
}

/**
 * 从 t0 起查找最近一次可见过境窗口（仰角 > minElevation 度）。
 * 返回 { start: Date, end: Date, entries: [{unix, uplink, downlink}] }
 * entries 每秒一条（多普勒已补偿，10Hz 单位），最多 1020 条（约 17 分钟，
 * 与固件单个 16 KB 星历槽的容量一致）。
 */
function findPass({
  tle1, tle2,
  latDeg, lonDeg, altKm,
  uplinkMHz, downlinkMHz,
  minElevation = 0,
  searchStart = new Date(),
  maxSearchHours = 24,
  maxPassSeconds = 17 * 60 - 1,
}) {
  const satrec = satellite.twoline2satrec(tle1, tle2);
  const obsGd = {
    longitude: satellite.degreesToRadians(lonDeg),
    latitude: satellite.degreesToRadians(latDeg),
    height: altKm,
  };
  const obsEcf = satellite.geodeticToEcf(obsGd);

  // 观测者 ECI 位置随地球自转变化，按时刻精确计算（见模块级 observerEci）。
  function obsEciAt(date) {
    return observerEci(obsEcf, date);
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

  // 细化窗口开始（Look4Sat 同款算法：500ms 步进找首个越过 minElevation 的格点，
  // 不插值；再四舍五入到整秒。若计算时过境已在进行中，最多回溯 32 分钟找到真实 AOS）。
  const backLimit = new Date(coarseStart.getTime() - maxPassSeconds * 1000);
  let passStart = coarseStart;
  for (let tt = new Date(coarseStart.getTime() - 500); tt >= backLimit; tt = new Date(tt.getTime() - 500)) {
    const el = elevationAt(tt);
    if (el === null) { passStart = new Date(tt.getTime() + 500); break; }
    if (el <= minElevation) {
      // 取跨越点之后的第一个格点（首个 > minElevation 的 500ms 格点）
      passStart = new Date(tt.getTime() + 500);
      break;
    }
    passStart = tt;
  }
  // Look4Sat: aos = 1000 * ((time + 500) / 1000)，四舍五入到整秒
  passStart = new Date(Math.round(passStart.getTime() / 1000) * 1000);

  // 细化窗口结束（Look4Sat 同款：500ms 步进，首个回落格点，四舍五入到整秒）
  const maxEnd = new Date(passStart.getTime() + maxPassSeconds * 1000);
  let passEnd = maxEnd;
  for (let tt = new Date(passStart.getTime() + 500); tt < maxEnd; tt = new Date(tt.getTime() + 500)) {
    const el = elevationAt(tt);
    if (el === null) { continue; }
    if (el <= minElevation) {
      passEnd = tt;
      break;
    }
  }
  // Look4Sat: los = 1000 * ((time + 500) / 1000)，四舍五入到整秒
  passEnd = new Date(Math.round(passEnd.getTime() / 1000) * 1000);

  // 生成 1 s 步进表（sum_time + 1 条，包含首尾，供固件插值）。
  // 表起点 floor 到整秒：start_unix 是整秒，固件按整秒索引（index = now - start_unix），
  // AOS 的亚秒小数只用于过境时刻报告（pass.start/end），不进表。
  const entries = [];
  const durS = Math.min(1019, Math.round((passEnd.getTime() - passStart.getTime()) / 1000));
  const count = Math.min(1020, durS + 1);
  const tableStartMs = Math.floor(passStart.getTime() / 1000) * 1000;
  for (let i = 0; i < count; i++) {
    const date = new Date(tableStartMs + i * 1000);
    const pv = satellite.propagate(satrec, date);
    if (pv.position === undefined || pv.velocity === undefined) {
      entries.push({ unix: 0, uplink: 0, downlink: 0, altitudeKm: 0, distanceKm: 0, azimuthDeg: 0, elevationDeg: 0 });
      continue;
    }
    const vr = radialVelocity(pv.position, pv.velocity, obsEciAt(date));
    const posEcf = satellite.eciToEcf(pv.position, satellite.gstime(date));
    const look = lookAngles(posEcf, obsEcf, obsGd.longitude, obsGd.latitude);
    entries.push({
      unix: Math.round(date.getTime() / 1000),
      uplink: Math.round(uplinkFreq(uplinkMHz * 1e6, vr) / 10),
      downlink: Math.round(downlinkFreq(downlinkMHz * 1e6, vr) / 10),
      altitudeKm: look.altitudeKm,
      distanceKm: look.distanceKm,
      azimuthDeg: look.azimuthDeg,
      elevationDeg: look.elevationDeg,
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

/** Date -> 固件 6 字节时间 [年2000, 月, 日, 时, 分, 秒]（固定北京时间 UTC+8）。
 *  显式按 UTC+8 换算北京墙钟，与浏览器/系统时区无关：
 *  系统时区设错时结果仍与 Look4Sat（本地时区显示）和固件输入的北京时间一致。 */
function dateToFwTime(date) {
  const bj = new Date(date.getTime() + 8 * 3600 * 1000);
  return [
    bj.getUTCFullYear() - 2000,
    bj.getUTCMonth() + 1,
    bj.getUTCDate(),
    bj.getUTCHours(),
    bj.getUTCMinutes(),
    bj.getUTCSeconds(),
  ];
}

/** 1970 基准秒 -> 2000 基准秒（固件 start_unix），按北京时间对齐。
 *  固件按用户输入的北京时间算 2000 基准秒, 所以这里加 8 小时使两边基准一致. */
function unixToFw(unix1970) {
  return unix1970 - 946684800 + 8 * 3600;
}

// ---- TLE 增量缓存：按 NORAD 编号 + epoch 比对，只更新变化的条目 ----

/** 从 TLE 第 1 行提取 NORAD 编号（如 "1 67290U ..." -> "67290"）。 */
function noradId(tle1) {
  return tle1.substring(2, 7).trim();
}

/** 从 TLE 第 1 行提取 epoch（字符 19-32，如 "26240.64276285"），用于比对是否更新。 */
function tleEpoch(tle1) {
  return tle1.substring(18, 32);
}

/**
 * 合并缓存列表与网络新拉取的列表（增量）。
 * 以 fresh 的顺序为基准：同 NORAD 编号且 epoch 相同 -> 保留条目；
 * fresh 的 epoch 更新 -> 用新条目；fresh 的 epoch 更旧（如兜底镜像数据）
 * -> 保留缓存里的更新数据，防止降级。fresh 没有的缓存条目（补充星等）追加保留。
 * 返回 { list, updated }，updated = 实际发生变化的条数（新增 + epoch 变新）。
 */
function mergeTleList(cached, fresh) {
  const freshById = new Map();
  for (const s of fresh) freshById.set(noradId(s.tle1), s);
  const byId = new Map(freshById); // 顺序 = fresh 的顺序
  for (const s of cached) {
    const id = noradId(s.tle1);
    if (!byId.has(id)) byId.set(id, s); // 仅缓存有的星（补充星等），追加保留
  }
  const list = [];
  let updated = 0;
  const cachedById = new Map(cached.map((s) => [noradId(s.tle1), s]));
  for (const s of byId.values()) {
    const id = noradId(s.tle1);
    const old = cachedById.get(id);
    if (!old) {
      updated++; // 新增
      list.push(s);
    } else if (old.tle1 === s.tle1 && old.tle2 === s.tle2) {
      list.push(s); // 完全相同
    } else if (tleEpoch(s.tle1) > tleEpoch(old.tle1)) {
      updated++; // epoch 变新 -> 用新数据
      list.push(s);
    } else {
      list.push(old); // fresh 更旧（镜像兜底）：保留缓存，不降级
    }
  }
  return { list, updated };
}

/**
 * 按源优先级合并多个 TLE 源的结果（Look4Sat 同款策略：按 NORAD 编号去重，前面的源优先）。
 * sourceLists: [{ name, sats: [{name, tle1, tle2}] }, ...]（已按优先级从高到低排序）
 * 返回去重后的合并列表（保留第一个出现该 NORAD 编号的条目）。
 */
function mergeSatelliteSources(sourceLists) {
  const seen = new Set();
  const list = [];
  for (const { sats } of sourceLists) {
    for (const s of sats) {
      const id = noradId(s.tle1);
      if (!seen.has(id)) {
        seen.add(id);
        list.push(s);
      }
    }
  }
  return list;
}

// ---- SatNOGS 频率库：按星挑选最优发射机/转发器条目 ----

const AMATEUR_BANDS = [
  [145e6, 146e6],   // 2m
  [435e6, 438e6],   // 70cm
  [1267e6, 1270e6], // 23cm (L 段上行)
];

function isAmateurBandHz(f) {
  return AMATEUR_BANDS.some(([lo, hi]) => f >= lo && f <= hi);
}

function isVhfHz(f) { return f >= 145e6 && f <= 146e6; }
function isUhfHz(f) { return f >= 435e6 && f <= 438e6; }

// 描述关键词：转发器/语音优先；遥测/信标/APRS/航天器内部通信降权
const FREQ_GOOD_KW = ["repeater", "voice", "transponder", "fm"];
const FREQ_BAD_KW = ["aprs", "digipeater", "beacon", "tlm", "telemetry", "crew", "soyuz", "suit", "eva", "dragon", "spacex", "mystery", "control", "communication unit", "status", "test"];

/**
 * 条目评分（越低越好）：FM 优先、业余段优先、转发器/语音关键词加分、
 * 跨段转发器（V/U 或 U/V）加分、同段转发（APRS/宇航员内部通信）大幅降权。
 */
function transceiverScore(e) {
  const desc = (e.description || "").toLowerCase();
  const mode = (e.mode || "").toUpperCase();
  const up = e.uplink_low, dn = e.downlink_low;
  let s = 0;
  if (mode !== "FM") s += 10;
  if (!isAmateurBandHz(dn)) s += 100;
  if (FREQ_GOOD_KW.some((k) => desc.includes(k))) s -= 5;
  if (FREQ_BAD_KW.some((k) => desc.includes(k))) s += 50;
  if (up && dn) {
    const crossBand = (isVhfHz(up) && isUhfHz(dn)) || (isUhfHz(up) && isVhfHz(dn));
    if (crossBand) s -= 3; // 跨段转发器优先
    else if ((isVhfHz(up) && isVhfHz(dn)) || (isUhfHz(up) && isUhfHz(dn))) s += 30; // 同段转发降权
  }
  return s;
}

/**
 * 从一颗星的全部 SatNOGS 发射机条目中挑选最适合多普勒固件（FM 对讲机）的条目。
 * 优先级：Transceiver（双向转发器）> Transmitter（纯下行，上行=下行作单工）。
 * 同优先级按 transceiverScore 排序。线性转发器（频段 low!=high）取中心频率。
 * 返回 { up, down, mode, type, desc }（Hz），无可用条目返回 null。
 */
function pickBestTransceiver(entries) {
  const usable = entries.filter((e) => e && e.downlink_low && (e.alive === undefined || e.alive));
  const center = (e, key) => {
    const lo = e[key + "_low"], hi = e[key + "_high"];
    return lo != null && hi != null && hi !== lo ? (lo + hi) / 2 : lo;
  };

  // 1. 双向转发器（有上行）优先
  const tc = usable.filter((e) => e.type === "Transceiver" && e.uplink_low);
  if (tc.length) {
    tc.sort((a, b) => transceiverScore(a) - transceiverScore(b));
    const e = tc[0];
    return { up: center(e, "uplink"), down: center(e, "downlink"), mode: e.mode || "", type: "Transceiver", desc: e.description || "" };
  }
  // 2. 纯下行（遥测/信标）：上行=下行（单工监听）
  const tx = usable.filter((e) => isAmateurBandHz(e.downlink_low));
  if (tx.length) {
    tx.sort((a, b) => transceiverScore(a) - transceiverScore(b));
    const e = tx[0];
    return { up: center(e, "downlink"), down: center(e, "downlink"), mode: e.mode || "", type: "Transmitter", desc: e.description || "" };
  }
  return null;
}

/**
 * 从 SatNOGS 发射机数组构建 NORAD 编号 -> 最优条目映射（用于选星自动填频率）。
 * 返回对象 { "43770": { up, down, mode, type, desc }, ... }。
 */
function buildFreqMap(transmitters) {
  const byNorad = new Map();
  for (const t of transmitters) {
    if (!t || !t.norad_cat_id || !t.downlink_low) continue;
    const id = String(t.norad_cat_id).padStart(5, "0");
    if (!byNorad.has(id)) byNorad.set(id, []);
    byNorad.get(id).push(t);
  }
  const map = {};
  for (const [id, entries] of byNorad) {
    const best = pickBestTransceiver(entries);
    if (best) map[id] = best;
  }
  return map;
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
    radialVelocity, uplinkFreq, downlinkFreq, observerEci, lookAngles,
    findPass, dateToFwTime, unixToFw, noradId, tleEpoch, mergeTleList, mergeSatelliteSources,
    pickBestTransceiver, buildFreqMap, isAmateurBandHz,
  };
});
