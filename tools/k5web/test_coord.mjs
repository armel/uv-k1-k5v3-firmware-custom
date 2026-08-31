/**
 * Node 单元测试：坐标转换（GCJ-02 <-> WGS-84）。
 * 运行：node tools/k5web/test_coord.mjs
 */
import { strict as assert } from "node:assert";
import coord from "./coord.js";

const { wgs84ToGcj02, gcj02ToWgs84, outOfChina } = coord;

// ---- 已知点验证（上海外滩约 31.240, 121.490 WGS-84） ----
// 往返测试：WGS -> GCJ -> WGS 误差应 < 5 米（约 5e-5 度）
{
  const wgs = [31.240, 121.490];
  const gcj = wgs84ToGcj02(wgs[0], wgs[1]);
  const back = gcj02ToWgs84(gcj[0], gcj[1]);
  const dLat = Math.abs(back[0] - wgs[0]) * 111000;
  const dLon = Math.abs(back[1] - wgs[1]) * 111000 * Math.cos(wgs[0] * Math.PI / 180);
  console.log(`  上海往返误差: ${dLat.toFixed(1)} m / ${dLon.toFixed(1)} m`);
  assert.ok(dLat < 5 && dLon < 5, "roundtrip within 5 m");
}

// GCJ 相对 WGS 应有明显偏移（中国区域约 100-600 米）
{
  const wgs = [31.240, 121.490];
  const gcj = wgs84ToGcj02(wgs[0], wgs[1]);
  const d = Math.hypot((gcj[0] - wgs[0]) * 111000, (gcj[1] - wgs[1]) * 111000 * Math.cos(wgs[0] * Math.PI / 180));
  console.log(`  上海 GCJ 偏移: ${d.toFixed(0)} m`);
  assert.ok(d > 50 && d < 1500, "GCJ offset in expected range");
}

// 中国境外不应偏移（旧金山）
{
  const wgs = [37.7749, -122.4194];
  assert.deepEqual(wgs84ToGcj02(wgs[0], wgs[1]), wgs, "outside China no offset");
  assert.deepEqual(gcj02ToWgs84(wgs[0], wgs[1]), wgs, "outside China no offset");
  assert.ok(outOfChina(37.77, -122.42), "outOfChina true");
}

// 北京、广州、乌鲁木齐往返
{
  for (const [lat, lon] of [[39.9042, 116.4074], [23.1291, 113.2644], [43.8256, 87.6168]]) {
    const gcj = wgs84ToGcj02(lat, lon);
    const back = gcj02ToWgs84(gcj[0], gcj[1]);
    const dLat = Math.abs(back[0] - lat) * 111000;
    assert.ok(dLat < 5, `roundtrip ${lat},${lon} within 5m`);
  }
  console.log("  北京/广州/乌鲁木齐往返均 < 5 m");
}

console.log("\n全部坐标转换测试通过 ✅");
