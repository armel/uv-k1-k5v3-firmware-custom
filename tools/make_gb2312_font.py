#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 GB2312 16×16 点阵字库（供 F4HWN Fusion 固件使用，与白头佬多系统共享字库兼容）。

输出格式（与固件 cnfont.h 约定一致）：
  - 最多 8192 个字形（覆盖 GB2312 常用区），每字形 32 字节，共 262,144 字节
  - 字形索引 = (高字节 - 0xA1) * 94 + (低字节 - 0xA1)
  - 每字形：前 16 字节为上半个页面（第 0..7 行，每字节一列，bit n = 第 n 行，
            LSB 在顶），后 16 字节为下半个页面（第 8..15 行）。
            与 ST7565 页结构同序，与白头佬共享 16×16 字库格式一致。

用法：
  python make_gb2312_font.py [输出文件] [字体文件] [字号]
  默认：gb2312_16x16.bin，自动在常见系统字体路径找中文字体。
"""
import os
import sys

# 多系统环境下 stdout 可能不是 UTF-8（例如 Windows cp1252），打印中文会报错。
# 强制使用 UTF-8 输出，无法编码的字符用替换符代替，避免脚本直接崩溃。
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

from PIL import Image, ImageDraw, ImageFont

SIZE = 16
BYTES_PER_GLYPH = 32
MAX_GLYPHS = 8192
ZONES = 94
POSITIONS = 94
BASE = 0xA1

FONT_CANDIDATES = [
    # Windows
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\msyhbd.ttc",
    r"C:\Windows\Fonts\simsun.ttc",
    r"C:\Windows\Fonts\simsunb.ttf",
    # Linux: Noto CJK / WenQuanYi
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttl",
    # macOS
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/STHeiti Light.ttc",
    "/System/Library/Fonts/STHeiti Medium.ttc",
    "/Library/Fonts/Arial Unicode.ttf",
]


def load_font(path, size):
    if path:
        return ImageFont.truetype(path, size)
    for cand in FONT_CANDIDATES:
        if os.path.exists(cand):
            print(f"使用字体: {cand}")
            return ImageFont.truetype(cand, size)
    sys.exit("未找到中文字体，请用参数指定 TTF/TTC 路径")


def render_glyph(font, ch):
    """渲染一个字符为 16x16 点阵（返回 16x16 的 0/1 二维列表）"""
    img = Image.new("1", (SIZE, SIZE), 0)
    d = ImageDraw.Draw(img)
    try:
        bbox = font.getbbox(ch)
    except Exception:
        return None
    if bbox is None:
        return None
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    if w == 0 and h == 0:
        return None
    x = (SIZE - w) // 2 - bbox[0]
    y = (SIZE - h) // 2 - bbox[1]
    d.text((x, y), ch, font=font, fill=1)
    px = img.load()
    return [[1 if px[cx, cy] else 0 for cx in range(SIZE)] for cy in range(SIZE)]


def pack_glyph(bits):
    """16x16 点阵 -> 32 字节（上页面 16 字节：第 0..7 行 + 下页面 16 字节：第 8..15 行）"""
    out = bytearray(BYTES_PER_GLYPH)
    for x in range(SIZE):
        for y in range(8):
            if bits[y][x]:
                out[x] |= 1 << y
        for y in range(8):
            if bits[y + 8][x]:
                out[SIZE + x] |= 1 << y
    return bytes(out)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "gb2312_16x16.bin"
    font_path = sys.argv[2] if len(sys.argv) > 2 else None
    size = int(sys.argv[3]) if len(sys.argv) > 3 else SIZE
    font = load_font(font_path, size)

    total_slots = ZONES * POSITIONS
    data = bytearray(MAX_GLYPHS * BYTES_PER_GLYPH)
    rendered = 0
    for hi in range(BASE, BASE + ZONES):
        for lo in range(BASE, BASE + POSITIONS):
            idx = (hi - BASE) * POSITIONS + (lo - BASE)
            if idx >= MAX_GLYPHS:
                break  # 截断到共享字库 8192 字形上限
            try:
                ch = bytes([hi, lo]).decode("gb2312")
            except UnicodeDecodeError:
                continue  # GB2312 未定义位置，留空
            bits = render_glyph(font, ch)
            if bits is None:
                continue
            data[idx * BYTES_PER_GLYPH:(idx + 1) * BYTES_PER_GLYPH] = pack_glyph(bits)
            rendered += 1

    with open(out_path, "wb") as f:
        f.write(data)
    print(f"完成: {out_path}  {len(data)} 字节, 有效字形 {rendered}/{MAX_GLYPHS} (GB2312 总槽位 {total_slots})")


if __name__ == "__main__":
    main()
