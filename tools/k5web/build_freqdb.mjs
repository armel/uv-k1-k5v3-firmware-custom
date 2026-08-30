/**
 * 构建内置频率库（freqdb.js）：从 SatNOGS 拉取全部发射机数据，
 * 用 calc.buildFreqMap 挑选每颗星的最优条目，生成静态 JS 数据文件。
 *
 * 用法：
 *   node tools/k5web/build_freqdb.mjs            # 联网拉取 SatNOGS 最新数据
 *   node tools/k5web/build_freqdb.mjs <json路径>  # 用已有 JSON 文件生成（跳过下载）
 *
 * 卫星频率基本不变，此文件定期（比如每月）重新生成一次即可。
 */
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import calc from "./calc.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const outPath = join(__dirname, "freqdb.js");
const SATNOGS_URL = "https://db.satnogs.org/api/transmitters/?format=json&status=active";

let transmitters;
const srcPath = process.argv[2];
if (srcPath) {
  console.log(`读取本地文件：${srcPath}`);
  transmitters = JSON.parse(readFileSync(srcPath, "utf8"));
} else {
  console.log(`拉取 SatNOGS：${SATNOGS_URL}`);
  const resp = await fetch(SATNOGS_URL);
  if (!resp.ok) throw new Error("HTTP " + resp.status);
  transmitters = await resp.json();
}

const map = calc.buildFreqMap(transmitters);
const header = `/**
 * 内置卫星频率库（SatNOGS 数据，构建时生成）
 * 生成时间：${new Date().toISOString()}
 * 来源：${SATNOGS_URL}
 * 覆盖卫星：${Object.keys(map).length} 颗
 *
 * 更新方式：node tools/k5web/build_freqdb.mjs
 * UMD：浏览器挂 window.K5WEB.freqdb，Node require 导出。
 */
`;
const body = `const FREQDB = ${JSON.stringify(map, null, 1)};\n\n(function (root, factory) {\n  if (typeof module === "object" && module.exports) {\n    module.exports = factory();\n  } else {\n    root.K5WEB = root.K5WEB || {};\n    root.K5WEB.freqdb = factory();\n  }\n})(typeof self !== "undefined" ? self : this, function () {\n  return FREQDB;\n});\n`;

writeFileSync(outPath, header + body, "utf8");
console.log(`已生成 ${outPath}：${Object.keys(map).length} 颗卫星，${(Buffer.byteLength(header + body) / 1024).toFixed(1)} KB`);
