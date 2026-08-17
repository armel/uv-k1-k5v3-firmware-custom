/**
 * F4HWN 多普勒星历写入工具 - 浏览器逻辑（普通 script，file:// 可用）
 * 依赖：window.satellite（vendor/satellite.min.js UMD）
 *       window.K5WEB.protocol（protocol.js UMD）
 *       window.K5WEB.calc（calc.js UMD）
 */
(function () {
  "use strict";

  const proto = window.K5WEB.protocol;
  const calc = window.K5WEB.calc;
  const $ = (id) => document.getElementById(id);

  let port = null;
  let reader = null;
  let replyQueue = [];
  let passData = null; // findPass 结果

  const log = (msg, cls) => {
    const el = $("log");
    el.textContent += (cls ? `[${cls}] ` : "") + msg + "\n";
    el.scrollTop = el.scrollHeight;
  };
  const setStatus = (msg, cls) => {
    const el = $("status");
    el.className = cls || "info";
    el.textContent = msg;
  };

  // ---------- 自动获取 TLE ----------
  // 常见 FM 卫星频率表（上行/下行 MHz）
  const KNOWN_SATS = {
    "iss": [145.99, 437.8], "international space station": [145.99, 437.8],
    "ao-91": [145.96, 435.25], "ao-92": [145.88, 435.35],
    "fo-29": [145.9, 435.3], "so-50": [145.85, 436.795],
    "po-101": [145.9, 436.5], "cas-4b": [145.925, 436.875],
    "ao-7": [145.85, 432.1], "rs-44": [145.9, 435.6],
    "to-108": [145.88, 436.62], "lilacsat-2": [145.9, 437.2],
    "io-117": [145.9, 436.5], "uvsqsat": [145.94, 436.88],
    "cas-4a": [145.925, 436.875], "ao-73": [145.95, 435.14],
    "ao-109": [145.9, 435.6],
  };
  let satList = [];

  async function fetchTLE() {
    // 两个源都返回纯文本 TLE：
    // 源 1：GitHub API raw 模式（Accept: raw+json 直接给文件内容，含 CORS）
    // 源 2：jsdelivr 代理同一仓库（兜底）
    const sources = [
      { url: "https://api.github.com/repos/satvisorcom/satvisor-data/contents/celestrak/tle/amateur.tle", headers: { Accept: "application/vnd.github.raw+json" } },
      { url: "https://cdn.jsdelivr.net/gh/satvisorcom/satvisor-data@master/celestrak/tle/amateur.tle", headers: {} },
    ];
    let lastErr = null;
    for (const src of sources) {
      try {
        const resp = await fetch(src.url, { headers: src.headers });
        if (!resp.ok) throw new Error("HTTP " + resp.status);
        const text = await resp.text();
        const lines = text.split(/\r?\n/);
        const sats = [];
        for (let i = 0; i + 2 < lines.length; i += 3) {
          const name = lines[i].trim();
          if (name && lines[i + 1].startsWith("1 ") && lines[i + 2].startsWith("2 ")) {
            sats.push({ name, tle1: lines[i + 1], tle2: lines[i + 2] });
          }
        }
        if (sats.length > 0) return sats;
        throw new Error("TLE 解析为空");
      } catch (e) {
        lastErr = e;
        log("TLE 源失败：" + src.url.split("/").slice(0, 3).join("/") + " -> " + e.message);
      }
    }
    throw lastErr || new Error("所有 TLE 源均不可用");
  }

  $("btnFetch").addEventListener("click", async () => {
    const btn = $("btnFetch");
    btn.disabled = true;
    btn.textContent = "获取中...";
    try {
      satList = await fetchTLE();
      const sel = $("satSelect");
      sel.innerHTML = "";
      for (const s of satList) {
        const opt = document.createElement("option");
        opt.value = s.name;
        opt.textContent = s.name.trim();
        sel.appendChild(opt);
      }
      sel.disabled = false;
      setStatus(`✅ 已获取 ${satList.length} 颗业余卫星 TLE（epoch 为 Celestrak 最新）`, "ok");
      log(`TLE 获取成功：${satList.length} 颗卫星`);
      sel.onchange = () => {
        const s = satList.find((x) => x.name === sel.value);
        if (!s) return;
        $("tle").value = s.tle1 + "\n" + s.tle2;
        const key = s.name.toLowerCase();
        let freqFound = false;
        for (const [k, v] of Object.entries(KNOWN_SATS)) {
          if (key.includes(k)) {
            $("fUp").value = v[0];
            $("fDown").value = v[1];
            freqFound = true;
            break;
          }
        }
        log(`已选 ${s.name.trim()}：TLE 已填充${freqFound ? "，频率已自动填入" : "，请手动填写频率"}`);
      };
    } catch (e) {
      setStatus("获取失败：" + e.message + "（可手动粘贴 TLE）", "err");
    } finally {
      btn.disabled = false;
      btn.textContent = "⬇️ 获取 TLE";
    }
  });

  // ---------- 地图选点（高德瓦片，懒加载） ----------
  const coord = window.K5WEB.coord;
  let map = null, marker = null;

  function initMap() {
    if (map) return;
    map = L.map("map").setView([parseFloat($("lat").value) || 31.23, parseFloat($("lon").value) || 121.47], 10);
    L.tileLayer("https://webrd{s}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}", {
      subdomains: ["01", "02", "03", "04"],
      maxZoom: 18,
      attribution: "© 高德地图",
    }).addTo(map);
    marker = L.circleMarker([parseFloat($("lat").value) || 31.23, parseFloat($("lon").value) || 121.47], {
      radius: 6, color: "#c62828", fillColor: "#c62828", fillOpacity: 0.9,
    }).addTo(map);
    map.on("click", onMapClick);
  }

  async function onMapClick(e) {
    // 点击坐标是 GCJ-02（高德瓦片），反算为 WGS-84 填入表单
    const wgs = coord.gcj02ToWgs84(e.latlng.lat, e.latlng.lng);
    $("lat").value = wgs[0].toFixed(5);
    $("lon").value = wgs[1].toFixed(5);
    if (marker) marker.setLatLng(e.latlng);
    log(`地图选点：${wgs[0].toFixed(5)}, ${wgs[1].toFixed(5)}（WGS-84）`);
    // 查询海拔（open-elevation，失败则保留手动值）
    try {
      const resp = await fetch(
        "https://api.open-elevation.com/api/v1/lookup?locations=" + wgs[0].toFixed(4) + "," + wgs[1].toFixed(4),
        { headers: { "accept": "application/json" } }
      );
      if (resp.ok) {
        const data = await resp.json();
        const h = data.results && data.results[0] && data.results[0].elevation;
        if (typeof h === "number") {
          $("alt").value = (h / 1000).toFixed(3); // 米 -> km
          log(`海拔查询：${h.toFixed(0)} m`);
        }
      }
    } catch (err) {
      log("海拔查询失败（可手动填写）：" + err.message);
    }
  }

  $("btnMap").addEventListener("click", () => {
    $("mapWrap").style.display = "block";
    setTimeout(() => initMap(), 50); // 等容器可见后再初始化
    setTimeout(() => map && map.invalidateSize(), 200);
  });

  $("btnLocate").addEventListener("click", () => {
    if (!navigator.geolocation) {
      setStatus("当前浏览器不支持 GPS 定位", "err");
      return;
    }
    navigator.geolocation.getCurrentPosition(
      (pos) => {
        $("lat").value = pos.coords.latitude.toFixed(5);
        $("lon").value = pos.coords.longitude.toFixed(5);
        setStatus("✅ 已使用设备定位（WGS-84）", "ok");
        log(`GPS 定位：${pos.coords.latitude.toFixed(5)}, ${pos.coords.longitude.toFixed(5)}`);
        if (map) map.setView([pos.coords.latitude, pos.coords.longitude], 12);
      },
      (err) => setStatus("GPS 定位失败：" + err.message + "（可改用地图选点）", "err"),
      { enableHighAccuracy: true, timeout: 10000 }
    );
  });


  // ---------- 计算过境 ----------
  $("btnCalc").addEventListener("click", () => {
    const showErr = (msg) => {
      const r = $("result");
      r.style.display = "block";
      r.innerHTML = `<span class="err"><b>⚠️ ${msg}</b></span>`;
      setStatus(msg, "err");
    };
    const tle = $("tle").value.trim().split(/\r?\n/);
    if (tle.length < 2 || !tle[0].trim()) {
      showErr("请先获取 TLE（点上方 ⬇️ 获取 TLE 按钮）或手动粘贴两行 TLE");
      return;
    }
    const lat = parseFloat($("lat").value), lon = parseFloat($("lon").value);
    const minEl = parseFloat($("minEl").value);
    if (isNaN(lat) || isNaN(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) {
      showErr("观测位置经纬度无效（纬度 -90~90，经度 -180~180）");
      return;
    }
    if (isNaN(minEl) || minEl < 0 || minEl > 89) {
      showErr("最低仰角无效（0~89 度）");
      return;
    }
    const btn = $("btnCalc");
    btn.disabled = true;
    btn.textContent = "⏳ 计算中...";
    // 先渲染"计算中"，再同步执行 SGP4 解算
    setTimeout(() => {
    try {
      const pass = calc.findPass({
        tle1: tle[0],
        tle2: tle[1],
        latDeg: lat,
        lonDeg: lon,
        altKm: parseFloat($("alt").value) || 0,
        uplinkMHz: parseFloat($("fUp").value),
        downlinkMHz: parseFloat($("fDown").value),
        minElevation: minEl,
        searchStart: new Date(),
        maxSearchHours: 24,
        maxPassSeconds: 32 * 60,
      });
      if (!pass) {
        showErr("未来 24 小时内未找到可见过境。检查：TLE 是否当天最新、经纬度是否正确、最低仰角是否过高");
        btn.disabled = false;
        btn.textContent = "🔭 计算最近过境";
        return;
      }
      passData = pass;
      const r = $("result");
      r.style.display = "block";
      const fmt = (d) => d.toLocaleString();
      const first = pass.entries[0], last = pass.entries[pass.entries.length - 1];
      r.innerHTML =
        `<b>过境时间：</b>${fmt(pass.start)} → ${fmt(pass.end)}（本地）<br>` +
        `时长 ${pass.durationS}s，频率表 ${pass.entries.length} 条（每 2 秒）<br>` +
        `下行 ${(first.downlink / 1e5).toFixed(5)} ~ ${(last.downlink / 1e5).toFixed(5)} MHz<br>` +
        `上行 ${(first.uplink / 1e5).toFixed(5)} ~ ${(last.uplink / 1e5).toFixed(5)} MHz<br>` +
        `<span class="ok">可以写入。写入后请在过境开始前开机，按 F+0 输入当前时间（UTC）开始跟踪。</span>`;
      $("btnWrite").disabled = false;
      log(`过境 ${pass.start.toISOString()} → ${pass.end.toISOString()}，${pass.entries.length} 条`);
    } catch (e) {
      showErr("计算失败：" + e.message);
      log("计算异常：" + e.stack);
    }
    btn.disabled = false;
    btn.textContent = "🔭 计算最近过境";
    }, 30);
  });

  // ---------- 串口 ----------
  async function readLoop() {
    const frameDec = new proto.FrameDecoder();
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value && value.length) {
          const frames = frameDec.push(value); // Uint8Array，直接喂帧解码器
          for (const f of frames) replyQueue.push(f);
        }
      }
    } catch (e) {
      if (e.name !== "AbortError") log("读取中断：" + e.message, "err");
    } finally {
      reader.releaseLock();
    }
  }

  async function sendCommand(id, payload) {
    const frame = proto.buildFrame(id, payload);
    await port.write(frame);
    // 等待对应回复（带 5s 超时）
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      if (replyQueue.length) {
        const reply = replyQueue.shift();
        const parsed = proto.parseReply(reply);
        if (parsed.id === id + 3) return parsed; // 回复 ID = 命令 ID + 3
        // 其他回复（如 K5Viewer 流）忽略
        continue;
      }
      await new Promise((r) => setTimeout(r, 20));
    }
    throw new Error("回复超时 (0x" + id.toString(16) + ")");
  }

  $("btnConnect").addEventListener("click", async () => {
    if (port) {
      try { await port.close(); } catch (e) { /* ignore */ }
      port = null;
      $("btnConnect").textContent = "连接串口";
      log("串口已断开");
      return;
    }
    if (!navigator.serial) {
      setStatus("当前浏览器不支持 Web Serial，请用 Chrome/Edge（需 https 或 localhost 环境）", "err");
      return;
    }
    try {
      port = await navigator.serial.requestPort();
      await port.open({ baudRate: 115200 });
      reader = port.readable.getReader();
      readLoop();
      $("btnConnect").textContent = "断开串口";
      setStatus("串口已连接 ✓", "ok");
      log("串口已连接");
    } catch (e) {
      setStatus("连接失败：" + e.message, "err");
    }
  });

  // ---------- 写入星历 ----------
  $("btnWrite").addEventListener("click", async () => {
    if (!port) { setStatus("请先连接串口", "err"); return; }
    if (!passData) { setStatus("请先计算过境", "err"); return; }

    $("btnWrite").disabled = true;
    $("progress").style.display = "block";
    const bar = $("progressBar");
    bar.style.width = "0%";
    try {
      // 1. 擦除
      log("擦除多普勒数据区...");
      const e = await sendCommand(proto.CMD.DOPPLER_ERASE, new Uint8Array(0));
      if (e.status !== 0) throw new Error("擦除失败 status=" + e.status);
      log("擦除完成");

      // 2. 卫星信息块
      const start = new Date(passData.start.getTime());
      const end = new Date(passData.end.getTime());
      const sat = proto.buildSatelliteBlock({
        name: "SAT",
        startTime: calc.dateToFwTime(start),
        endTime: calc.dateToFwTime(end),
        sumTime: passData.durationS,
        sendCtcss: parseInt($("ctcss").value, 10),
        startUnix: calc.unixToFw(Math.round(start.getTime() / 1000)),
      });
      log("写入卫星信息块...");
      const s = await sendCommand(proto.CMD.DOPPLER_WRITE_SAT, sat);
      if (s.status !== 0) throw new Error("卫星块写入失败 status=" + s.status);
      log("卫星块完成");

      // 3. 频率表条目（分批）
      const total = passData.entries.length;
      const batch = 64;
      for (let i = 0; i < total; i += batch) {
        const n = Math.min(batch, total - i);
        for (let j = 0; j < n; j++) {
          const en = passData.entries[i + j];
          const payload = new Uint8Array(12);
          const dv = new DataView(payload.buffer);
          dv.setUint16(0, i + j, true); // index
          payload.set(proto.buildEntry(en.uplink, en.downlink), 4);
          const r = await sendCommand(proto.CMD.DOPPLER_WRITE_ENTRY, payload);
          if (r.status !== 0) throw new Error(`条目 ${i + j} 写入失败`);
        }
        bar.style.width = ((i + n) / total * 100).toFixed(1) + "%";
        log(`条目 ${i + n}/${total}`);
      }
      bar.style.width = "100%";
      setStatus("✅ 星历写入完成！对讲机按 F+0 进入多普勒模式，输入当前 UTC 时间即可跟踪", "ok");
      log("全部完成");
    } catch (err) {
      setStatus("写入失败：" + err.message, "err");
      log("写入异常：" + err.message, "err");
    } finally {
      $("btnWrite").disabled = false;
    }
  });
})();
