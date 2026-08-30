/**
 * Node 单元测试：多普勒计算模块。
 * 运行：node tools/k5web/test_calc.mjs
 */
import { strict as assert } from "node:assert";
import satellite from "./vendor/satellite.min.js";
import calc from "./calc.js";
const { radialVelocity, uplinkFreq, downlinkFreq, observerEci, findPass, dateToFwTime, unixToFw, noradId, tleEpoch, mergeTleList, mergeSatelliteSources, pickBestTransceiver, buildFreqMap, isAmateurBandHz } = calc;

const C = 299792.458;

// ---- 径向速度符号与数值 ----
// 卫星在观测者正上方 400km 处，沿 +x 方向运动（地平线方向）
{
  const obs = { x: 0, y: 0, z: 6371 };          // ECI，观测者在地轴上（简化）
  const sat = { x: 0, y: 0, z: 6771 };          // 正上方
  const vel = { x: 7.5, y: 0, z: 0 };           // 沿 +x（垂直于视线 → vr 应≈0）
  const vr = radialVelocity(sat, vel, obs);
  assert.ok(Math.abs(vr) < 1e-6, `正上方横向速度 vr≈0, got ${vr}`);
  console.log("✓ 横向速度 vr≈0");

  // 沿视线方向远离（+z）：vr = +7.5
  const vel2 = { x: 0, y: 0, z: 7.5 };
  const vr2 = radialVelocity(sat, vel2, obs);
  assert.ok(Math.abs(vr2 - 7.5) < 1e-6, `vr = +7.5, got ${vr2}`);
  console.log("✓ 远离时 vr = +7.5 km/s");
}

// ---- 多普勒频率数值（ISS 典型值） ----
{
  // 接近时 vr = -7 km/s
  const fDown = 437.8e6;
  const fUp = 145.99e6;
  const d = downlinkFreq(fDown, -7);
  const u = uplinkFreq(fUp, -7);
  // 下行：f × (1 - vr/c) = f × (1 + 7/299792.458) → +约 10.22 kHz @437.8MHz（ISS 70cm 段典型值）
  const expectedD = fDown * (1 + 7 / C);
  assert.ok(Math.abs(d - expectedD) < 1, `downlink +2.3kHz, got ${d - fDown} Hz`);
  // 上行：f / (1 - vr/c) = f / (1 + 7/C) → 约 -3.41 kHz @145.99MHz（接近时卫星蓝移你的信号，须降低发射频率）
  const expectedU = fUp / (1 + 7 / C);
  assert.ok(Math.abs(u - expectedU) < 0.5, `uplink -3.41kHz, got ${u - fUp} Hz`);
  console.log(`✓ 多普勒数值：下行 ${(d - fDown).toFixed(0)} Hz（接近升高）、上行 ${(u - fUp).toFixed(0)} Hz（接近降低）`);
}

// ---- 10Hz 单位转换 ----
{
  const d = downlinkFreq(437.8e6, -7);
  const u = uplinkFreq(145.99e6, -7);
  assert.ok(d / 10 < 0xffffffff, "fits u32");
  assert.ok(u / 10 > 0, "positive");
  console.log("✓ 10Hz 单位换算在 u32 范围内");
}

// ---- 时间转换（北京时间 UTC+8） ----
{
  const d = new Date(Date.UTC(2026, 7, 17, 12, 0, 0)); // 2026-08-17 12:00 UTC = 北京 20:00
  const fw = dateToFwTime(d);
  assert.deepEqual(fw, [26, 8, 17, 20, 0, 0]); // 北京时间
  // unixToFw 加 8h 使固件按北京时间算的秒数与之匹配: 840283200 + 28800
  assert.equal(unixToFw(d.getTime() / 1000), 840283200 + 28800);
  console.log("✓ 时间转换与固件一致（北京时间基准）");
}

// ---- 观测者 ECI 旋转方向（回归：曾用 Rz(+gst) 使经度反演、频偏全错） ----
{
  const obsGd = {
    longitude: satellite.degreesToRadians(121.47),
    latitude: satellite.degreesToRadians(31.23),
    height: 0.01,
  };
  const obsEcf = satellite.geodeticToEcf(obsGd);

  for (const date of [
    new Date(Date.UTC(2026, 7, 17, 0, 0, 0)),
    new Date(Date.UTC(2026, 7, 17, 12, 0, 0)),
    new Date(Date.UTC(2026, 7, 17, 23, 59, 59)),
  ]) {
    const gst = satellite.gstime(date);
    const eci = observerEci(obsEcf, date);

    // 物理不变量：ECI 经度 = 地固经度 + gst（正规化到 [-π, π]）
    const lonEcf = obsGd.longitude;
    const lonEci = Math.atan2(eci.y, eci.x);
    let dlon = lonEci - (lonEcf + gst);
    dlon = Math.atan2(Math.sin(dlon), Math.cos(dlon)); // wrap
    assert.ok(Math.abs(dlon) < 1e-9,
      `ECI 经度不变量不成立 @${date.toISOString()}: dlon=${dlon.toFixed(6)} rad`);

    // 必须与库标准转换逐元素一致（Rz(-gst)）
    const ref = satellite.ecfToEci(obsEcf, gst);
    assert.ok(Math.abs(eci.x - ref.x) < 1e-9 && Math.abs(eci.y - ref.y) < 1e-9,
      `observerEci 与库 ecfToEci 不一致 @${date.toISOString()}`);
  }
  console.log("✓ 观测者 ECI 旋转方向正确（ECI 经度 ≡ 地固经度 + gst）");
}

// ---- 完整过境流程（用 ISS TLE 示例，跨过境窗口） ----
{
  // TLE 数据（示例：ISS，已过时不影响流程验证）
  const tle1 = "1 25544U 98067A   26080.00000000  .00016717  00000-0  10270-3 0  9000";
  const tle2 = "2 25544  51.6400 234.2345 0006043 141.3553 275.5311 15.50836567000002";
  // 用未来 1 小时窗口内的搜索（避免 epoch 已过太久；若 TLE 太旧 SGP4 会误差大，仅测流程）
  const pass = findPass({
    tle1, tle2,
    latDeg: 31.23, lonDeg: 121.47, altKm: 0.01, // 上海
    uplinkMHz: 145.99, downlinkMHz: 437.8,
    minElevation: 0, // 真实可见过境（仰角>0，才会产生真实多普勒频偏）
    searchStart: new Date(Date.now() + 60 * 60 * 1000),
    maxSearchHours: 24,
  });
  if (pass) {
    assert.ok(pass.entries.length > 0 && pass.entries.length <= 1920, "entries within bounds");
    assert.ok(pass.entries.length === Math.min(1920, pass.durationS + 1), "duration consistent");
    const mid = pass.entries[Math.floor(pass.entries.length / 2)];
    const first = pass.entries[0];
    const last = pass.entries[pass.entries.length - 1];
    console.log(`  ISS 过境：${pass.start.toISOString()} → ${pass.end.toISOString()}，时长 ${pass.durationS}s，${pass.entries.length} 条`);

    // 频率应在合理范围（下行 437.8±、上行 145.99±，10Hz 单位）
    for (const e of [first, mid, last]) {
      assert.ok(e.downlink > 4e7 && e.downlink < 5e7, `downlink in 400-500MHz range, got ${e.downlink / 1e5}`);
      assert.ok(e.uplink > 1.4e7 && e.uplink < 1.5e7, `uplink in 140-150MHz range, got ${e.uplink / 1e5}`);
    }

    // 物理断言：可见过境（仰角>0）必须产生真实多普勒频偏（kHz 量级）。
    // ISS 轨道速度 ~7.5 km/s → 437.8MHz 下行频偏最高 ~10kHz，过境中必有 |Δf| > 1kHz。
    // 曾因观测者 ECI 旋转方向反了导致频偏仅 ±100Hz（错误），此断言可捕获该回归。
    const dfHz = (a, b) => Math.abs(a.downlink - b.downlink) * 10; // 10Hz 单位 -> Hz
    const maxDeviation = Math.max(dfHz(first, mid), dfHz(last, mid));
    assert.ok(maxDeviation > 1000,
      `可见过境应有 kHz 级多普勒频偏, got ${maxDeviation.toFixed(0)} Hz @ ${pass.start.toISOString()}`);
    console.log(`  ✓ 可见过境多普勒频偏 ${maxDeviation.toFixed(0)} Hz（>1kHz，修复生效）`);
    console.log(`  下行频率范围：${first.downlink / 1e5} ~ ${mid.downlink / 1e5} MHz（首/中）`);
  } else {
    console.log("  注意：24h 内未找到可见过境（TLE 过旧时 SGP4 偏差大，跳过断言）");
  }
}

// ---- TLE 增量缓存：noradId / tleEpoch / mergeTleList ----
{
  const tleA = "1 25544U 98067A   26228.82508390  .00005081  00000+0  98725-4 0  9993";
  const tleB = "1 25544U 98067A   26240.64276285  .00005081  00000+0  98725-4 0  9993"; // 同星，epoch 更新
  const tleC = "1 67290U 25313AU  26240.64276285  .00008490  00000+0  33186-3 0  9996"; // 另一颗星

  assert.equal(noradId(tleA), "25544", "noradId 提取");
  assert.equal(noradId(tleC), "67290", "noradId 提取（5 位）");
  assert.equal(tleEpoch(tleA), "26228.82508390", "tleEpoch 提取");

  const cached = [
    { name: "ISS", tle1: tleA, tle2: "2 25544  ...", fetchedAt: 1000 },
    { name: "RS18S", tle1: tleC, tle2: "2 67290  ...", fetchedAt: 1000 },
  ];
  // epoch 未变 -> 不更新；补充星不在 fresh 里 -> 追加保留
  let r = mergeTleList(cached, [{ name: "ISS", tle1: tleA, tle2: "2 25544  ..." }]);
  assert.equal(r.updated, 0, "epoch 相同不更新");
  assert.equal(r.list.length, 2, "缓存独有的补充星保留");
  assert.equal(r.list[0].name, "ISS", "fresh 顺序优先");
  assert.equal(r.list[1].name, "RS18S", "补充星追加在后");

  // epoch 变化 -> 更新 1 颗
  r = mergeTleList(cached, [{ name: "ISS", tle1: tleB, tle2: "2 25544  ..." }]);
  assert.equal(r.updated, 1, "epoch 变化更新 1 颗");
  assert.equal(r.list[0].tle1, tleB, "用新条目");
  assert.equal(r.list[1].name, "RS18S", "未涉及的缓存星保留");

  // fresh 的 epoch 更旧（兜底镜像数据）-> 保留缓存，不降级
  r = mergeTleList(cached, [{ name: "ISS", tle1: "1 25544U 98067A   26220.00000000  .00005081  00000+0  98725-4 0  9993", tle2: "2 25544  ..." }]);
  assert.equal(r.updated, 0, "旧数据不覆盖缓存");
  assert.equal(r.list[0].tle1, tleA, "保留缓存的更新数据");

  // 新增星 -> updated
  r = mergeTleList([], [{ name: "ISS", tle1: tleB, tle2: "2 25544  ..." }]);
  assert.equal(r.updated, 1, "首次获取全部计入更新");
  assert.equal(r.list.length, 1, "空缓存合并");
  console.log("✓ TLE 增量缓存合并正确（noradId/epoch/保留/更新/新增/防降级）");
}

// ---- 多源合并：按源优先级去重（Look4Sat 策略）----
{
  const issOld = { name: "ISS", tle1: "1 25544U 98067A   26228.82508390  .00005081  00000+0  98725-4 0  9993", tle2: "2 25544  ..." };
  const issNew = { name: "ISS", tle1: "1 25544U 98067A   26241.27263584  .00006962  00000+0  13475-3 0  9997", tle2: "2 25544  ..." };
  const rs18s = { name: "RS18S", tle1: "1 67290U 25313AU  26240.64276285  .00008490  00000+0  33186-3 0  9996", tle2: "2 67290  ..." };
  const ao73 = { name: "AO-73", tle1: "1 39444U 13066AE  26240.50000000  .00000000  00000-0  10000-3 0  9995", tle2: "2 39444  ..." };

  // 源 1 有 ISS(旧)+RS18S，源 2 有 ISS(新)+AO-73：ISS 取源 1（优先级），AO-73 补充
  const merged = mergeSatelliteSources([
    { name: "源1", sats: [issOld, rs18s] },
    { name: "源2", sats: [issNew, ao73] },
  ]);
  assert.equal(merged.length, 3, "合并去重后 3 颗");
  assert.equal(merged[0].tle1, issOld.tle1, "同星取前面源（优先级）");
  assert.ok(merged.some((s) => s.name === "AO-73"), "后面源独有的星保留");
  assert.ok(merged.some((s) => s.name === "RS18S"), "补充星保留");
  // 空源跳过
  const merged2 = mergeSatelliteSources([{ name: "空", sats: [] }, { name: "有", sats: [ao73] }]);
  assert.equal(merged2.length, 1, "空源不影响");
  console.log("✓ 多源合并正确（源优先级/去重/补充/空源）");
}

// ---- SatNOGS 频率库：pickBestTransceiver / buildFreqMap ----
{
  // AO-95 (FOX-1CLIFF, 43770) 真实条目：应选 U/V FM 转发器（up 435.3 / down 145.92）
  const ao95 = [
    { description: "DUV TLM", type: "Transmitter", downlink_low: 145920000, downlink_high: 145920000, uplink_low: null, mode: "DUV", alive: true },
    { description: "AFSK9k6 digital data", type: "Transmitter", downlink_low: 145920000, downlink_high: 145920000, uplink_low: null, mode: "AFSK", alive: true },
    { description: "U/V FM", type: "Transceiver", downlink_low: 145920000, downlink_high: 145920000, uplink_low: 435300000, uplink_high: 435300000, mode: "FM", alive: true },
    { description: "L/V FM", type: "Transceiver", downlink_low: 145920000, downlink_high: 145920000, uplink_low: 1267300000, uplink_high: 1267300000, mode: "FM", alive: true },
  ];
  const best = pickBestTransceiver(ao95);
  assert.equal(best.up, 435300000, "AO-95 选 U/V FM 上行 435.3");
  assert.equal(best.down, 145920000, "AO-95 选 U/V FM 下行 145.92");
  assert.equal(best.type, "Transceiver", "AO-95 为转发器");

  // ISS (25544) 真实条目：应选语音转发器 Voice Repeater（145.990/437.800），
  // 不能选 APRS（145.825 同段）或宇航员通信（crew，同段）
  const iss = [
    { description: "Mode V APRS", type: "Transceiver", downlink_low: 145825000, downlink_high: 145825000, uplink_low: 145825000, uplink_high: 145825000, mode: "AFSK", alive: true },
    { description: "Mode V/V FM (crew R2+3)", type: "Transceiver", downlink_low: 145800000, downlink_high: 145800000, uplink_low: 144490000, uplink_high: 144490000, mode: "FM", alive: true },
    { description: "Mode V/V FM (crew R1)", type: "Transceiver", downlink_low: 145800000, downlink_high: 145800000, uplink_low: 145200000, uplink_high: 145200000, mode: "FM", alive: true },
    { description: "Mode V/U FM - Voice Repeater CTCSS 67.0 Hz", type: "Transceiver", downlink_low: 437800000, downlink_high: 437800000, uplink_low: 145990000, uplink_high: 145990000, mode: "FM", alive: true },
    { description: "Mode U APRS test", type: "Transceiver", downlink_low: 437825000, downlink_high: 437825000, uplink_low: 437825000, uplink_high: 437825000, mode: "AFSK", alive: true },
  ];
  const bestIss = pickBestTransceiver(iss);
  assert.equal(bestIss.up, 145990000, "ISS 选语音转发器上行 145.99");
  assert.equal(bestIss.down, 437800000, "ISS 选语音转发器下行 437.8");
  assert.ok(bestIss.desc.includes("Voice Repeater"), "ISS 选中的条目是 Voice Repeater");

  // 纯下行星（RS18S 类）：Transceiver 缺失时选纯下行，上行=下行
  const rs18s = [
    { description: "GMSK TLM", type: "Transmitter", downlink_low: 437350000, downlink_high: 437350000, uplink_low: null, mode: "GMSK", alive: true },
    { description: "SSTV", type: "Transmitter", downlink_low: 437350000, downlink_high: 437350000, uplink_low: null, mode: "SSTV", alive: true },
  ];
  const best2 = pickBestTransceiver(rs18s);
  assert.equal(best2.up, 437350000, "纯下行星上行=下行");
  assert.equal(best2.down, 437350000, "纯下行星下行");
  assert.equal(best2.type, "Transmitter", "纯下行星类型");

  // 线性转发器（频段 low!=high）取中心频率
  const linear = [
    { description: "Linear", type: "Transceiver", downlink_low: 145950000, downlink_high: 146000000, uplink_low: 435200000, uplink_high: 435250000, mode: "USB", alive: true },
  ];
  const best3 = pickBestTransceiver(linear);
  assert.equal(best3.down, 145975000, "线性转发器取中心频率");
  assert.equal(best3.up, 435225000, "线性转发器上行中心频率");

  // 非业余段且无 Transceiver -> null
  const alien = [
    { description: "S-band", type: "Transmitter", downlink_low: 2200000000, downlink_high: 2200000000, uplink_low: null, mode: "FM", alive: true },
  ];
  assert.equal(pickBestTransceiver(alien), null, "非业余段纯下行返回 null");

  // buildFreqMap：按 norad 分组 + 填充 + padStart(5, "0")
  const map = buildFreqMap([
    { norad_cat_id: 43770, ...ao95[2] },
    { norad_cat_id: 67290, ...rs18s[0] },
    { norad_cat_id: 99999, description: "x", type: "Transmitter", downlink_low: null, uplink_low: null, mode: "FM", alive: true },
  ]);
  assert.equal(map["43770"].up, 435300000, "映射 43770 上行");
  assert.equal(map["67290"].down, 437350000, "映射 67290 下行");
  assert.equal(map["99999"], undefined, "无下行频率的条目不收录");
  assert.ok(isAmateurBandHz(145920000) && !isAmateurBandHz(2000000000), "业余段判定");
  console.log("✓ SatNOGS 频率库选频正确（AO-95/纯下行/线性/非业余/映射）");
}

console.log("\n全部计算测试通过 ✅");
