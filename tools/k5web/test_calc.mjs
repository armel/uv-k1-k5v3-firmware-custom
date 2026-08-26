/**
 * Node 单元测试：多普勒计算模块。
 * 运行：node tools/k5web/test_calc.mjs
 */
import { strict as assert } from "node:assert";
import satellite from "./vendor/satellite.min.js";
import calc from "./calc.js";
const { radialVelocity, uplinkFreq, downlinkFreq, observerEci, findPass, dateToFwTime, unixToFw } = calc;

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
  // 上行：f / (1 + vr/c) = f / (1 - 7/C) → +约 3.41 kHz @145.99MHz
  const expectedU = fUp / (1 - 7 / C);
  assert.ok(Math.abs(u - expectedU) < 0.5, `uplink +0.78kHz, got ${u - fUp} Hz`);
  console.log(`✓ 多普勒数值：下行 ${(d - fDown).toFixed(0)} Hz、上行 ${(u - fUp).toFixed(0)} Hz（接近时频率升高）`);
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
    assert.ok(pass.durationS >= pass.entries.length * 2 - 2, "duration consistent");
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

console.log("\n全部计算测试通过 ✅");
