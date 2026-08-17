/**
 * 坐标转换模块（WGS-84 <-> GCJ-02，火星坐标）
 * UMD: 浏览器挂 window.K5WEB.coord，Node require('./coord.js')。
 *
 * 背景：高德地图瓦片使用 GCJ-02（火星坐标），而 SGP4 过境计算需要
 * WGS-84。地图点击得到的坐标先经此模块反算为 WGS-84 再填入表单。
 * 近似反算精度约 1 米，对卫星过境计算完全足够。
 */

(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
  } else {
    root.K5WEB = root.K5WEB || {};
    root.K5WEB.coord = factory();
  }
})(typeof self !== "undefined" ? self : this, function () {
  "use strict";

  const PI = Math.PI;
  const A = 6378245.0; // 长半轴
  const EE = 0.00669342162296594323; // 偏心率平方

  function outOfChina(lat, lon) {
    return lon < 72.004 || lon > 137.8347 || lat < 0.8293 || lat > 55.8271;
  }

  function transformLat(x, y) {
    let ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * Math.sqrt(Math.abs(x));
    ret += (20.0 * Math.sin(6.0 * x * PI) + 20.0 * Math.sin(2.0 * x * PI)) * 2.0 / 3.0;
    ret += (20.0 * Math.sin(y * PI) + 40.0 * Math.sin(y / 3.0 * PI)) * 2.0 / 3.0;
    ret += (160.0 * Math.sin(y / 12.0 * PI) + 320 * Math.sin(y * PI / 30.0)) * 2.0 / 3.0;
    return ret;
  }

  function transformLon(x, y) {
    let ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * Math.sqrt(Math.abs(x));
    ret += (20.0 * Math.sin(6.0 * x * PI) + 20.0 * Math.sin(2.0 * x * PI)) * 2.0 / 3.0;
    ret += (20.0 * Math.sin(x * PI) + 40.0 * Math.sin(x / 3.0 * PI)) * 2.0 / 3.0;
    ret += (150.0 * Math.sin(x / 12.0 * PI) + 300.0 * Math.sin(x / 30.0 * PI)) * 2.0 / 3.0;
    return ret;
  }

  /** WGS-84 -> GCJ-02，返回 [lat, lon] */
  function wgs84ToGcj02(lat, lon) {
    if (outOfChina(lat, lon)) return [lat, lon];
    let dLat = transformLat(lon - 105.0, lat - 35.0);
    let dLon = transformLon(lon - 105.0, lat - 35.0);
    const radLat = (lat / 180.0) * PI;
    let magic = Math.sin(radLat);
    magic = 1 - EE * magic * magic;
    const sqrtMagic = Math.sqrt(magic);
    dLat = (dLat * 180.0) / (((A * (1 - EE)) / (magic * sqrtMagic)) * PI);
    dLon = (dLon * 180.0) / ((A / sqrtMagic) * Math.cos(radLat) * PI);
    return [lat + dLat, lon + dLon];
  }

  /** GCJ-02 -> WGS-84（近似反算，精度约 1 米），返回 [lat, lon] */
  function gcj02ToWgs84(lat, lon) {
    if (outOfChina(lat, lon)) return [lat, lon];
    const g = wgs84ToGcj02(lat, lon);
    return [lat * 2 - g[0], lon * 2 - g[1]];
  }

  return { wgs84ToGcj02, gcj02ToWgs84, outOfChina };
});
