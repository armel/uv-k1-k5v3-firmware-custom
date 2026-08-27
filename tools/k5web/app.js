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
  let writer = null;
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

  // ---------- 选项卡切换 ----------
  document.querySelectorAll(".tabbtn").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll(".tabbtn").forEach((b) => b.classList.remove("active"));
      document.querySelectorAll(".tabpanel").forEach((p) => p.classList.remove("active"));
      btn.classList.add("active");
      $(btn.dataset.tab).classList.add("active");
    });
  });

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
    // 查询海拔（多源 + 超时；失败有明显提示，且不影响使用）
    setStatus("📡 查询海拔...");
    const h = await fetchElevation(wgs[0], wgs[1]);
    if (h !== null) {
      $("alt").value = h.toFixed(1); // 米
      setStatus(`✅ 已选点 ${wgs[0].toFixed(4)}, ${wgs[1].toFixed(4)}，海拔 ${h.toFixed(0)} m`, "ok");
    } else {
      setStatus("⚠️ 海拔查询失败（保留手动值，海拔对过境计算影响可忽略）", "err");
  }
  }

  $("btnMap").addEventListener("click", () => {
    $("mapWrap").style.display = "block";
    setTimeout(() => initMap(), 50); // 等容器可见后再初始化
    setTimeout(() => map && map.invalidateSize(), 200);
  });

  // 查询海拔：open-elevation API（8s 超时），失败返回 null
  async function fetchElevation(lat, lon) {
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), 8000);
    try {
      const resp = await fetch(
        "https://api.open-elevation.com/api/v1/lookup?locations=" + lat.toFixed(4) + "," + lon.toFixed(4),
        { headers: { "accept": "application/json" }, signal: ctrl.signal }
      );
      if (!resp.ok) throw new Error("HTTP " + resp.status);
      const data = await resp.json();
      const h = data.results && data.results[0] && data.results[0].elevation;
      return typeof h === "number" ? h : null;
    } catch (e) {
      log("海拔查询失败：" + e.message);
      return null;
    } finally {
      clearTimeout(timer);
    }
  }


  $("btnLocate").addEventListener("click", () => {
    if (!navigator.geolocation) {
      setStatus("当前浏览器不支持 GPS 定位", "err");
      return;
    }
    navigator.geolocation.getCurrentPosition(
      async (pos) => {
        const lat = pos.coords.latitude, lon = pos.coords.longitude;
        $("lat").value = lat.toFixed(5);
        $("lon").value = lon.toFixed(5);
        log(`GPS 定位：${lat.toFixed(5)}, ${lon.toFixed(5)}`);
        // 优先 API 查精确地形海拔；API 失败时用设备海拔兜底
        // （注意：很多浏览器拿不到高度时 altitude 返回 0，需排除）
        let h = await fetchElevation(lat, lon);
        if (h === null) {
          const a = pos.coords.altitude;
          if (typeof a === "number" && isFinite(a) && a > 0 && a < 9000) h = a;
        }
        if (h !== null) {
          $("alt").value = h.toFixed(1); // 米
          setStatus(`✅ 已使用设备定位：${lat.toFixed(4)}, ${lon.toFixed(4)}，海拔 ${h.toFixed(0)} m`, "ok");
        } else {
          setStatus(`✅ 已使用设备定位：${lat.toFixed(4)}, ${lon.toFixed(4)}（海拔查询失败，可手动填写）`, "ok");
        }
        if (map) map.setView([lat, lon], 12);
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
    if (isNaN(lat) || isNaN(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) {
      showErr("观测位置经纬度无效（纬度 -90~90，经度 -180~180）");
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
        altKm: (parseFloat($("alt").value) || 0) / 1000, // 输入为米，内部用 km
        uplinkMHz: parseFloat($("fUp").value),
        downlinkMHz: parseFloat($("fDown").value),
        searchStart: new Date(),
        maxSearchHours: 24,
        maxPassSeconds: 32 * 60,
      });
      if (!pass) {
        showErr("未来 24 小时内未找到可见过境。检查：TLE 是否当天最新、经纬度是否正确");
        btn.disabled = false;
        btn.textContent = "🔭 计算最近过境";
        return;
      }
      passData = pass;
      const r = $("result");
      r.style.display = "block";
      // 固定按 Asia/Shanghai 显示，与系统时区无关（对比 Look4Sat 时不串时区）
      const fmt = (d) => d.toLocaleString("zh-CN", { hour12: false, timeZone: "Asia/Shanghai" });
      const first = pass.entries[0], last = pass.entries[pass.entries.length - 1];
      r.innerHTML =
        `<b>过境时间（北京时间）：</b>${fmt(pass.start)} → ${fmt(pass.end)}<br>` +
        `时长 ${pass.durationS}s，频率表 ${pass.entries.length} 条（每秒）<br>` +
        `下行 ${(first.downlink / 1e5).toFixed(5)} ~ ${(last.downlink / 1e5).toFixed(5)} MHz<br>` +
        `上行 ${(first.uplink / 1e5).toFixed(5)} ~ ${(last.uplink / 1e5).toFixed(5)} MHz<br>` +
        `<span class="ok">可以写入。写入后请在过境开始前开机，长按 0 输入当前北京时间开始跟踪。</span>`;
      $("btnWrite").disabled = false;
      log(`过境 ${fmt(pass.start)} → ${fmt(pass.end)}（北京时间），${pass.entries.length} 条`);
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
    await writer.write(frame);
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
      try { if (writer) { writer.releaseLock(); writer = null; } } catch (e) { /* ignore */ }
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
      await port.open({ baudRate: 38400 }); // F4HWN UART protocol is 38400 baud
      writer = port.writable.getWriter();
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
        name: ($("satSelect").value || "SAT").trim().slice(0, 9),
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
      setStatus("✅ 星历写入完成！对讲机长按 0 进入多普勒模式，输入当前北京时间即可跟踪", "ok");
      log("全部完成");
    } catch (err) {
      setStatus("写入失败：" + err.message, "err");
      log("写入异常：" + err.message, "err");
    } finally {
      $("btnWrite").disabled = false;
    }
  });
  // ---------- 中文字库刷入 ----------
  let fontData = null;

  $("fontFile").addEventListener("change", async (e) => {
    const f = e.target.files[0];
    fontData = null;
    $("btnFont").disabled = true;
    if (!f) return;
    const buf = new Uint8Array(await f.arrayBuffer());
    if (buf.length > proto.CN_FONT.FLASH_SIZE) {
      setStatus(`字库文件过大：${buf.length} 字节，应为 ${proto.CN_FONT.FLASH_SIZE}`, "err");
      return;
    }
    fontData = buf;
    $("btnFont").disabled = false;
    log(`字库文件已加载：${f.name}（${buf.length} 字节）`);
    if (buf.length !== proto.CN_FONT.FLASH_SIZE)
      log("提示：文件小于标准尺寸，未覆盖区域将保持空白", "info");
  });

  $("btnFont").addEventListener("click", async () => {
    if (!port) { setStatus("请先连接串口", "err"); return; }
    if (!fontData) { setStatus("请先选择字库文件", "err"); return; }

    $("btnFont").disabled = true;
    $("fontProgress").style.display = "block";
    const bar = $("fontProgressBar");
    bar.style.width = "0%";
    try {
      // 1. 逐扇区擦除（70 个，进度 0~30%）
      const sectors = proto.CN_FONT.SECTOR_COUNT;
      log(`擦除字库区（${sectors} 个扇区）...`);
      for (let s = 0; s < sectors; s++) {
        const payload = new Uint8Array(4);
        new DataView(payload.buffer).setUint16(0, s, true);
        const r = await sendCommand(proto.CMD.CN_FONT_ERASE, payload);
        if (r.status !== 0) throw new Error(`扇区 ${s} 擦除失败 status=${r.status}`);
        bar.style.width = ((s + 1) / sectors * 30).toFixed(1) + "%";
        if (s % 10 === 9 || s === sectors - 1) log(`擦除 ${s + 1}/${sectors}`);
      }

      // 2. 分块写入（240 字节/帧，进度 30~100%）
      const total = fontData.length, chunk = proto.CN_FONT.CHUNK;
      log("写入字库数据...");
      for (let off = 0; off < total; off += chunk) {
        const n = Math.min(chunk, total - off);
        const payload = new Uint8Array(4 + n);
        new DataView(payload.buffer).setUint32(0, off, true);
        payload.set(fontData.subarray(off, off + n), 4);
        const r = await sendCommand(proto.CMD.CN_FONT_WRITE, payload);
        if (r.status !== 0) throw new Error(`偏移 0x${off.toString(16)} 写入失败 status=${r.status}`);
        bar.style.width = (30 + (off + n) / total * 70).toFixed(1) + "%";
        if ((off / chunk) % 100 === 99 || off + n === total) log(`写入 ${off + n}/${total}`);
      }
      bar.style.width = "100%";
      setStatus("✅ 字库刷入完成！机内菜单 SetLng 选择 Chinese 即可（渲染支持随固件更新提供）", "ok");
      log("字库刷入完成");
    } catch (err) {
      setStatus("字库刷入失败：" + err.message, "err");
      log("字库刷入异常：" + err.message, "err");
    } finally {
      $("btnFont").disabled = false;
    }
  });
  // ---------- 校准数据导出 / 导入（EEPROM 仿真区 0xB000..0xB200，512 字节） ----------

  // 会话握手：发 0x0514 建立时间戳，等 0x0515 版本回复
  async function ensureSession() {
    const ts = new Uint8Array(4);
    new DataView(ts.buffer).setUint32(0, proto.CALIB.TS, true);
    await writer.write(proto.buildFrame(proto.CMD.DEV_INFO_REQ, ts));
    const resp = await waitForMsg(proto.CMD.DEV_INFO_RESP, 1500);
    if (!resp) throw new Error("握手超时（请确认对讲机处于正常开机状态，不是刷机模式）");
    let ver = "";
    for (let i = 4; i < Math.min(resp.length, 20) && resp[i] !== 0; i++) ver += String.fromCharCode(resp[i]);
    return ver;
  }

  $("btnCalExp").addEventListener("click", async () => {
    if (!port) { setStatus("请先连接串口", "err"); return; }
    $("btnCalExp").disabled = true;
    try {
      const ver = await ensureSession();
      log(`固件版本：${ver}`);
      const C = proto.CALIB;
      const out = new Uint8Array(C.SIZE);
      for (let off = 0; off < C.SIZE; off += C.READ_CHUNK) {
        const payload = new Uint8Array(8);
        const dv = new DataView(payload.buffer);
        dv.setUint16(0, C.OFFSET + off, true);
        dv.setUint8(2, C.READ_CHUNK);
        dv.setUint32(4, C.TS, true);
        await writer.write(proto.buildFrame(proto.CMD.READ_EEPROM, payload));
        const resp = await waitForMsg(proto.CMD.READ_EEPROM_RESP, 1500);
        if (!resp) throw new Error(`读取 0x${(C.OFFSET + off).toString(16)} 超时`);
        const rdv = new DataView(resp.buffer, resp.byteOffset, resp.byteLength);
        if (rdv.getUint16(4, true) !== C.OFFSET + off) throw new Error("读取偏移回显不一致");
        out.set(resp.subarray(8, 8 + C.READ_CHUNK), off);
        log(`读取 0x${(C.OFFSET + off).toString(16)} ~ 0x${(C.OFFSET + off + C.READ_CHUNK).toString(16)}`);
      }
      const blob = new Blob([out], { type: "application/octet-stream" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = "calibration.dat";
      a.click();
      URL.revokeObjectURL(a.href);
      setStatus("✅ 校准数据已导出为 calibration.dat", "ok");
      log("校准数据导出完成");
    } catch (err) {
      setStatus("导出失败：" + err.message, "err");
      log("导出异常：" + err.message, "err");
    } finally {
      $("btnCalExp").disabled = false;
    }
  });

  let calData = null;

  $("calFile").addEventListener("change", async (e) => {
    const f = e.target.files[0];
    calData = null;
    $("btnCalImp").disabled = true;
    if (!f) return;
    const buf = new Uint8Array(await f.arrayBuffer());
    if (buf.length !== proto.CALIB.SIZE) {
      setStatus(`校准文件大小无效：${buf.length} 字节，应为 ${proto.CALIB.SIZE}`, "err");
      return;
    }
    calData = buf;
    $("btnCalImp").disabled = false;
    log(`校准文件已加载：${f.name}（${buf.length} 字节）`);
  });

  $("btnCalImp").addEventListener("click", async () => {
    if (!port) { setStatus("请先连接串口", "err"); return; }
    if (!calData) { setStatus("请先选择校准文件", "err"); return; }
    if (!confirm("确认导入？写错校准数据会导致频率/功率/电量异常，请确认文件来自本机。")) return;

    $("btnCalImp").disabled = true;
    $("calProgress").style.display = "block";
    const bar = $("calProgressBar");
    bar.style.width = "0%";
    try {
      const ver = await ensureSession();
      log(`固件版本：${ver}`);
      const C = proto.CALIB;
      for (let off = 0; off < C.SIZE; off += C.WRITE_CHUNK) {
        const payload = new Uint8Array(8 + C.WRITE_CHUNK);
        const dv = new DataView(payload.buffer);
        dv.setUint16(0, C.OFFSET + off, true);
        dv.setUint8(2, C.WRITE_CHUNK);
        dv.setUint8(3, 1); // bAllowPassword 标志位（uvtools 约定）
        dv.setUint32(4, C.TS, true);
        payload.set(calData.subarray(off, off + C.WRITE_CHUNK), 8);
        await writer.write(proto.buildFrame(proto.CMD.WRITE_EEPROM, payload));
        const resp = await waitForMsg(proto.CMD.WRITE_EEPROM_RESP, 1500);
        if (!resp) throw new Error(`写入 0x${(C.OFFSET + off).toString(16)} 超时`);
        const wdv = new DataView(resp.buffer, resp.byteOffset, resp.byteLength);
        if (resp.length < 6 || wdv.getUint16(4, true) !== C.OFFSET + off)
          throw new Error(`写入 0x${(C.OFFSET + off).toString(16)} 回显不一致`);
        bar.style.width = ((off + C.WRITE_CHUNK) / C.SIZE * 100).toFixed(1) + "%";
        if (off % 64 === 48) log(`写入 ${off + C.WRITE_CHUNK}/${C.SIZE}`);
      }
      bar.style.width = "100%";
      log("校准数据写入完成，发送重启命令...");
      await writer.write(proto.buildFrame(proto.CMD.REBOOT, new Uint8Array(0)));
      setStatus("✅ 校准数据已导入，对讲机正在重启生效", "ok");
      log("已重启");
    } catch (err) {
      setStatus("导入失败：" + err.message, "err");
      log("导入异常：" + err.message, "err");
    } finally {
      $("btnCalImp").disabled = false;
    }
  });

  // ---------- 写频（写信道） ----------
  async function readEepromBlock(offset, size) {
    const payload = new Uint8Array(8);
    const dv = new DataView(payload.buffer);
    dv.setUint16(0, offset, true);
    dv.setUint8(2, size);
    dv.setUint32(4, proto.CALIB.TS, true);
    await writer.write(proto.buildFrame(proto.CMD.READ_EEPROM, payload));
    const resp = await waitForMsg(proto.CMD.READ_EEPROM_RESP, 1500);
    if (!resp) throw new Error(`读取 0x${offset.toString(16)} 超时`);
    const rdv = new DataView(resp.buffer, resp.byteOffset, resp.byteLength);
    if (rdv.getUint16(4, true) !== offset) throw new Error("读取偏移回显不一致");
    return resp.subarray(8, 8 + size);
  }

  async function writeEepromBlock(offset, data, flag = 1) {
    if (data.length % 8 !== 0) throw new Error("EEPROM 写入长度必须是 8 的倍数");
    for (let i = 0; i < data.length; i += 8) {
      const payload = new Uint8Array(8 + 8);
      const dv = new DataView(payload.buffer);
      dv.setUint16(0, offset + i, true);
      dv.setUint8(2, 8);
      dv.setUint8(3, flag);
      dv.setUint32(4, proto.CALIB.TS, true);
      payload.set(data.subarray(i, i + 8), 8);
      await writer.write(proto.buildFrame(proto.CMD.WRITE_EEPROM, payload));
      const resp = await waitForMsg(proto.CMD.WRITE_EEPROM_RESP, 1500);
      if (!resp) throw new Error(`写入 0x${(offset + i).toString(16)} 超时`);
      if (resp.length < 6) throw new Error("写入回复过短");
      const wdv = new DataView(resp.buffer, resp.byteOffset, resp.byteLength);
      if (wdv.getUint16(4, true) !== offset + i) throw new Error("写入偏移回显不一致");
    }
  }

  async function writeChannel(channel, params) {
    const C = proto.CHAN;
    if (channel < 0 || channel >= C.MAX_COUNT) throw new Error(`信道号 ${channel} 超出范围 0~${C.MAX_COUNT - 1}`);

    // 1. 写频率/参数区
    const freqBlock = proto.buildChannelBlock(params);
    await writeEepromBlock(channel * C.SIZE, freqBlock);

    // 2. 写名称区（GB2312 编码，最多 10 字节）
    let nameBytes;
    const gb = window.K5WEB && window.K5WEB.gb2312;
    if (gb) {
      const nameEnc = gb.encode(params.name || "");
      if (!nameEnc.ok) throw new Error(`信道名包含无法编码的字符："${nameEnc.char}"，请使用中文字库已覆盖的汉字或 ASCII`);
      if (nameEnc.bytes.length > 10) throw new Error(`信道名编码后 ${nameEnc.bytes.length} 字节，超过 10 字节限制（中文每个字 2 字节）`);
      nameBytes = nameEnc.bytes;
    } else {
      // fallback：仅 ASCII
      const ascii = new TextEncoder().encode((params.name || "").replace(/[^\x20-\x7E]/g, ""));
      if (ascii.length > 10) throw new Error("信道名超过 10 字节（GB2312 编码表未加载，仅支持 ASCII）");
      nameBytes = ascii;
    }
    const nameBuf = new Uint8Array(C.NAME_SIZE);
    nameBuf.set(nameBytes, 0);
    await writeEepromBlock(C.NAME_BASE + channel * C.NAME_SIZE, nameBuf);

    // 3. 属性区：8 字节对齐读写，避免跨属性覆盖
    const attrOffset = C.ATTR_BASE + channel * C.ATTR_SIZE;
    const alignBase = attrOffset - (attrOffset % C.ATTR_ALIGN); // 8 字节对齐基址
    const attrInBlock = attrOffset - alignBase; // 在 8 字节块内的偏移（0 或 2）
    const attrBlock = new Uint8Array(await readEepromBlock(alignBase, C.ATTR_ALIGN));
    const attr = proto.buildChannelAttributes({
      band: proto.bandFromFrequency(params.rxFreq10Hz),
      compander: 0,
      exclude: 0,
      scanlist: params.scanlist || 0,
    });
    attrBlock.set(attr, attrInBlock);
    await writeEepromBlock(alignBase, attrBlock);
  }

  function parseToneInput(value, type) {
    const s = (value || "").trim();
    if (!s || type === proto.CODE_TYPE.OFF) return { code: 0, codeType: proto.CODE_TYPE.OFF };
    if (type === proto.CODE_TYPE.CTCSS) {
      const hz = Math.round(parseFloat(s) * 10);
      return { code: proto.ctcssIndex(hz), codeType: proto.CODE_TYPE.CTCSS };
    }
    // DCS：支持 "023" / "D023" / "023N" / "I023" / "023I"
    const m = s.match(/^[DI]?(\d{3})$/i);
    if (!m) return { code: 0, codeType: proto.CODE_TYPE.OFF };
    const code = parseInt(m[1], 10);
    const idx = proto.dcsIndex(code);
    const codeType = (type === proto.CODE_TYPE.DCS_REV || /^I/i.test(s)) ? proto.CODE_TYPE.DCS_REV : proto.CODE_TYPE.DCS;
    return { code: idx, codeType };
  }

  /** 从字符串自动推断亚音类型：含小数点→CTCSS；3 位数字/Dxxx/Ixxx→DCS；空→OFF */
  function autoToneInput(value) {
    const s = (value || "").trim();
    if (!s) return { code: 0, codeType: proto.CODE_TYPE.OFF };
    if (/[.,]/.test(s) || /^\d{2,3}$/.test(s) && parseInt(s, 10) > 100) {
      const hz = Math.round(parseFloat(s.replace(",", ".")) * 10);
      return { code: proto.ctcssIndex(hz), codeType: proto.CODE_TYPE.CTCSS };
    }
    const m = s.match(/^[DI]?(\d{3})$/i);
    if (m) {
      const idx = proto.dcsIndex(parseInt(m[1], 10));
      const codeType = /^I/i.test(s) ? proto.CODE_TYPE.DCS_REV : proto.CODE_TYPE.DCS;
      return { code: idx, codeType };
    }
    return { code: 0, codeType: proto.CODE_TYPE.OFF };
  }

  function collectChannelParams() {
    const rxMHz = parseFloat($("chRxFreq").value);
    let txMHz = parseFloat($("chTxFreq").value);
    const txDir = parseInt($("chTxDir").value, 10);
    const txOffset = parseFloat($("chTxOffset").value) || 0;
    if (isNaN(rxMHz) || rxMHz <= 0) throw new Error("接收频率无效");
    if (isNaN(txMHz) || txMHz <= 0) {
      if (txDir === proto.TX_DIR.OFF) txMHz = rxMHz;
      else txMHz = rxMHz + (txDir === proto.TX_DIR.ADD ? txOffset : -txOffset);
    }
    const rxTone = parseToneInput($("chRxTone").value, parseInt($("chRxToneType").value, 10));
    const txTone = parseToneInput($("chTxTone").value, parseInt($("chTxToneType").value, 10));

    return {
      name: $("chName").value,
      rxFreq10Hz: Math.round(rxMHz * 100000),
      txFreq10Hz: Math.round(txMHz * 100000),
      rxCodeType: rxTone.codeType,
      rxCode: rxTone.code,
      txCodeType: txTone.codeType,
      txCode: txTone.code,
      modulation: parseInt($("chModulation").value, 10),
      txDir: txDir,
      bandwidth: parseInt($("chBandwidth").value, 10),
      power: parseInt($("chPower").value, 10),
      txLock: 0, // 默认允许发射；如需禁用可后续扩展
      bcl: 0,
      freqReverse: 0,
      pttId: 0,
      step: parseInt($("chStep").value, 10),
      scanlist: 0,
    };
  }

  $("btnChProg").addEventListener("click", async () => {
    if (!port) { setStatus("请先连接串口", "err"); return; }
    const channel = parseInt($("chNum").value, 10);
    if (isNaN(channel) || channel < 0 || channel >= proto.CHAN.MAX_COUNT) {
      setStatus(`信道号无效，应为 0~${proto.CHAN.MAX_COUNT - 1}`, "err"); return;
    }
    $("btnChProg").disabled = true;
    $("chProgProgress").style.display = "block";
    $("chProgProgressBar").style.width = "0%";
    $("chReadResult").style.display = "none";
    try {
      await ensureSession();
      const params = collectChannelParams();
      await writeChannel(channel, params);
      $("chProgProgressBar").style.width = "100%";
      setStatus(`✅ 信道 ${channel} 写入完成！建议重启对讲机或切换信道使其生效`, "ok");
      log(`写频完成：CH${channel} ${(params.rxFreq10Hz / 100000).toFixed(5)} MHz，名称字节：${Array.from((window.K5WEB.gb2312 || { encode: () => ({ ok: true, bytes: new Uint8Array() }) }).encode(params.name || "").bytes).map(b => b.toString(16).padStart(2, "0")).join(" ") || "(空)"}`);
    } catch (err) {
      setStatus("写频失败：" + err.message, "err");
      log("写频异常：" + err.message, "err");
    } finally {
      $("btnChProg").disabled = false;
    }
  });

  $("btnChRead").addEventListener("click", async () => {
    if (!port) { setStatus("请先连接串口", "err"); return; }
    const channel = parseInt($("chNum").value, 10);
    if (isNaN(channel) || channel < 0 || channel >= proto.CHAN.MAX_COUNT) {
      setStatus(`信道号无效，应为 0~${proto.CHAN.MAX_COUNT - 1}`, "err"); return;
    }
    $("btnChRead").disabled = true;
    $("chReadResult").style.display = "none";
    try {
      await ensureSession();
      const C = proto.CHAN;
      const nameBytes = await readEepromBlock(C.NAME_BASE + channel * C.NAME_SIZE, C.NAME_SIZE);
      const freqBytes = await readEepromBlock(channel * C.SIZE, C.SIZE);
      const rx10 = new DataView(freqBytes.buffer, freqBytes.byteOffset).getUint32(0, true);
      const tx10 = new DataView(freqBytes.buffer, freqBytes.byteOffset).getUint32(4, true);
      const hex = Array.from(nameBytes).map((b) => b.toString(16).padStart(2, "0")).join(" ");
      let decoded = "";
      for (let i = 0; i < nameBytes.length; i++) {
        const b = nameBytes[i];
        if (b === 0) break;
        if (b >= 0xA1 && i + 1 < nameBytes.length && nameBytes[i + 1] >= 0xA1) {
          decoded += `[${b.toString(16)}${nameBytes[i + 1].toString(16)}]`;
          i++;
        } else if (b >= 0x20 && b < 0x7F) {
          decoded += String.fromCharCode(b);
        } else {
          decoded += `?0x${b.toString(16)}`;
        }
      }
      const r = $("chReadResult");
      r.style.display = "block";
      r.innerHTML = `
        <b>CH${channel} 读取校验</b><br>
        接收频率：${(rx10 / 100000).toFixed(5)} MHz<br>
        发射频率：${(tx10 / 100000).toFixed(5)} MHz<br>
        名称区十六进制：${hex}<br>
        名称解析（[xxxx]=GB2312）：${decoded || "(空白)"}
      `;
      log(`读取 CH${channel}：RX=${(rx10 / 100000).toFixed(5)} TX=${(tx10 / 100000).toFixed(5)} 名称=[${hex}]`);
    } catch (err) {
      setStatus("读取失败：" + err.message, "err");
      log("读取异常：" + err.message, "err");
    } finally {
      $("btnChRead").disabled = false;
    }
  });

  // CSV 批量导入
  let chCsvData = null;
  $("chCsvFile").addEventListener("change", async (e) => {
    const f = e.target.files[0];
    chCsvData = null;
    $("btnChProgCsv").disabled = true;
    if (!f) return;
    const text = await f.text();
    const lines = text.split(/\r?\n/).filter((l) => l.trim());
    const rows = [];
    let first = true;
    for (const line of lines) {
      const cols = line.split(",").map((s) => s.trim());
      if (first && /信道|channel|freq/i.test(cols[0])) { first = false; continue; }
      first = false;
      if (cols.length < 4) continue;
      const ch = parseInt(cols[0], 10);
      const name = cols[1] || "";
      const rx = parseFloat(cols[2]);
      const tx = parseFloat(cols[3]);
      if (isNaN(ch) || isNaN(rx)) continue;
      rows.push({ ch, name, rx, tx, cols });
    }
    if (rows.length === 0) { setStatus("CSV 中没有可识别的信道行", "err"); return; }
    chCsvData = rows;
    $("btnChProgCsv").disabled = false;
    log(`CSV 已加载：${f.name}，${rows.length} 条信道`);
  });

  $("btnChProgCsv").addEventListener("click", async () => {
    if (!port) { setStatus("请先连接串口", "err"); return; }
    if (!chCsvData || !chCsvData.length) { setStatus("请先选择 CSV 文件", "err"); return; }
    $("btnChProgCsv").disabled = true;
    $("chCsvProgress").style.display = "block";
    const bar = $("chCsvProgressBar");
    bar.style.width = "0%";
    try {
      await ensureSession();
      const C = proto.CHAN;
      for (let i = 0; i < chCsvData.length; i++) {
        const row = chCsvData[i];
        if (row.ch < 0 || row.ch >= C.MAX_COUNT) {
          log(`跳过越界信道 ${row.ch}`, "info"); continue;
        }
        const rxTone = autoToneInput(row.cols[4] || "");
        const txTone = autoToneInput(row.cols[5] || "");
        const params = {
          name: row.name,
          rxFreq10Hz: Math.round(row.rx * 100000),
          txFreq10Hz: Math.round(row.tx * 100000),
          rxCodeType: rxTone.codeType, rxCode: rxTone.code,
          txCodeType: txTone.codeType, txCode: txTone.code,
          modulation: parseInt(row.cols[8] || "0", 10),
          txDir: proto.TX_DIR.OFF,
          bandwidth: parseInt(row.cols[6] || "0", 10),
          power: parseInt(row.cols[7] || "7", 10),
          txLock: 0, bcl: 0, freqReverse: 0, pttId: 0,
          step: 4, scanlist: 0,
        };
        await writeChannel(row.ch, params);
        bar.style.width = ((i + 1) / chCsvData.length * 100).toFixed(1) + "%";
        if ((i + 1) % 10 === 0 || i === chCsvData.length - 1) log(`已写入 ${i + 1}/${chCsvData.length}`);
      }
      bar.style.width = "100%";
      setStatus(`✅ CSV 批量写入完成，共 ${chCsvData.length} 条信道`, "ok");
      log("CSV 写频完成");
    } catch (err) {
      setStatus("CSV 写频失败：" + err.message, "err");
      log("CSV 写频异常：" + err.message, "err");
    } finally {
      $("btnChProgCsv").disabled = false;
    }
  });

  // ---------- 固件刷写（bootloader 协议，参考 Apache-2.0 的 uvtools2/js/flash.js） ----------
  let fwData = null;

  // 从回复队列等一条指定 id 的消息，其余（广播/K5Viewer 流等）丢弃
  async function waitForMsg(id, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      if (replyQueue.length) {
        const f = replyQueue.shift();
        if (f.length >= 2 && (f[0] | (f[1] << 8)) === id) return f;
        continue;
      }
      await new Promise((r) => setTimeout(r, 10));
    }
    return null;
  }

  // 阶段 1：等设备广播（连续 5 条有效 0x0518，相邻间隔 5~1000ms）
  async function waitDeviceInfo(maxMs) {
    const deadline = Date.now() + maxMs;
    let lastTime = 0, valid = 0;
    while (Date.now() < deadline) {
      const f = await waitForMsg(proto.FLASH_MSG.NOTIFY_DEV_INFO, 1200);
      if (!f) return null; // 1.2s 无广播 → 不在刷机模式
      const now = Date.now();
      const dt = now - lastTime;
      valid = !lastTime || (dt >= 5 && dt <= 1000) ? valid + 1 : 1;
      lastTime = now;
      if (valid >= 5) return f;
    }
    return null;
  }

  // 阶段 2：握手（收 0x0518 → 回 0x0530 版本前 4 字符，共 3 次）
  async function flashHandshake(version) {
    const v4 = new TextEncoder().encode(version.slice(0, 4).padEnd(4, "0"));
    for (let i = 0; i < 3; i++) {
      const f = await waitForMsg(proto.FLASH_MSG.NOTIFY_DEV_INFO, 1500);
      if (!f) throw new Error("握手超时（未收到设备广播）");
      await writer.write(proto.buildFlashFrame(proto.FLASH_MSG.NOTIFY_BL_VER, v4));
    }
    await new Promise((r) => setTimeout(r, 200));
    replyQueue.length = 0; // 排空残余广播
  }

  $("fwFile").addEventListener("change", async (e) => {
    const f = e.target.files[0];
    fwData = null;
    $("btnFlash").disabled = true;
    if (!f) return;
    const buf = new Uint8Array(await f.arrayBuffer());
    if (!buf.length || buf.length > proto.FLASH_MSG.APP_MAX_SIZE) {
      setStatus(`固件大小无效：${buf.length} 字节（应 1~${proto.FLASH_MSG.APP_MAX_SIZE}）`, "err");
      return;
    }
    fwData = buf;
    $("btnFlash").disabled = false;
    log(`固件已加载：${f.name}（${buf.length} 字节，${Math.ceil(buf.length / 256)} 页）`);
  });

  $("btnFlash").addEventListener("click", async () => {
    if (!port) { setStatus("请先连接串口", "err"); return; }
    if (!fwData) { setStatus("请先选择固件文件", "err"); return; }

    $("btnFlash").disabled = true;
    $("flashProgress").style.display = "block";
    const bar = $("flashProgressBar");
    bar.style.width = "0%";
    try {
      // ① 等设备（用户需已按住 PTT 开机进入刷机模式）
      log("等待刷机模式设备...（按住 PTT 开机）");
      setStatus("等待刷机模式设备...（按住 PTT 开机）", "info");
      const devMsg = await waitDeviceInfo(20000);
      if (!devMsg) throw new Error("未检测到刷机模式设备。请断开串口，按住 PTT 键开机后重新点击开始刷写");
      const dev = proto.parseDevInfo(devMsg);
      if (!dev) throw new Error("设备信息解析失败");
      log(`设备 UID：${dev.uid}`);
      log(`Bootloader 版本：${dev.version}`);
      if (!proto.blVersionOK(dev.version))
        throw new Error(`Bootloader 版本过低（${dev.version}，要求 ≥ 7.00.07），请先更新 bootloader`);

      // ② 握手
      log("握手中...");
      await flashHandshake(dev.version);
      log("握手完成，开始分页编程");

      // ③ 分页编程（256B/页，逐页 ACK，重试 3 次）
      const PS = proto.FLASH_MSG.PAGE_SIZE;
      const pageCount = Math.ceil(fwData.length / PS);
      const timestamp = Date.now() >>> 0;
      for (let i = 0; i < pageCount; i++) {
        const page = fwData.subarray(i * PS, Math.min((i + 1) * PS, fwData.length));
        const data = proto.buildFwPage(timestamp, i, pageCount, page);
        let done = false, lastErr = "";
        for (let attempt = 1; attempt <= 3 && !done; attempt++) {
          await writer.write(proto.buildFlashFrame(proto.FLASH_MSG.PROG_FW, data));
          const resp = await waitForMsg(proto.FLASH_MSG.PROG_FW_RESP, 3000);
          if (!resp) { lastErr = "ACK 超时"; continue; }
          if (resp.length < 12) { lastErr = "ACK 长度异常"; continue; }
          const dv = new DataView(resp.buffer, resp.byteOffset, resp.byteLength);
          const echoIdx = dv.getUint16(8, true);
          const errCode = dv.getUint16(10, true);
          if (echoIdx === i && errCode === 0) done = true;
          else lastErr = `页号回显 ${echoIdx} 错误码 ${errCode}`;
        }
        if (!done) throw new Error(`第 ${i}/${pageCount} 页写入失败：${lastErr}`);
        bar.style.width = ((i + 1) / pageCount * 100).toFixed(1) + "%";
        if (i % 50 === 49 || i === pageCount - 1) log(`页 ${i + 1}/${pageCount}`);
      }
      bar.style.width = "100%";
      setStatus("✅ 固件刷写完成！请断开串口并重新开机", "ok");
      log("固件刷写完成，请重新开机");
    } catch (err) {
      setStatus("固件刷写失败：" + err.message, "err");
      log("固件刷写异常：" + err.message, "err");
    } finally {
      $("btnFlash").disabled = false;
    }
  });
})();
