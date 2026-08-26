#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 GB2312 16x16 点阵字库（供 F4HWN Fusion 固件使用）。

输出格式（与固件 cnfont.h 约定一致）：
  - 94 区 x 94 位 = 8836 个字形，每字形 32 字节，共 235,328 字节
  - 字形索引 = (高字节 - 0xA1) * 94 + (低字节 - 0xA1)
  - 每字形：前 16 字节为上半 8 行（每字节一列，bit n = 第 n 行，LSB 在顶），
            后 16 字节为下半 8 行。与 ST7565 页结构/gFontBig 同序。

用法：
  python make_gb2312_font.py [输出文件] [字体文件] [字号]
  默认：gb2312_16x16.bin，自动在 C:/Windows/Fonts 找 simhei.ttf / msyh.ttc
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

SIZE = 16
BYTES_PER_GLYPH = 32
ZONES = 94
POSITIONS = 94
BASE = 0xA1

FONT_CANDIDATES = [
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\simsun.ttc",
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
    """16x16 点阵 -> 32 字节（页列式：上半 16 列字节 + 下半 16 列字节）"""
    out = bytearray(BYTES_PER_GLYPH)
    for x in range(SIZE):
        for y in range(8):
            if bits[y][x]:
                out[x] |= 1 << y
            if bits[y + 8][x]:
                out[SIZE + x] |= 1 << y
    return bytes(out)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "gb2312_16x16.bin"
    font_path = sys.argv[2] if len(sys.argv) > 2 else None
    size = int(sys.argv[3]) if len(sys.argv) > 3 else SIZE
    font = load_font(font_path, size)

    total = ZONES * POSITIONS
    data = bytearray(total * BYTES_PER_GLYPH)
    rendered = 0
    for hi in range(BASE, BASE + ZONES):
        for lo in range(BASE, BASE + POSITIONS):
            idx = (hi - BASE) * POSITIONS + (lo - BASE)
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
    print(f"完成: {out_path}  {len(data)} 字节, 有效字形 {rendered}/{total}")


if __name__ == "__main__":
    main()
