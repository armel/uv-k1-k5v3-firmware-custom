#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""解析 K5Viewer 屏幕流, 还原对讲机 LCD 画面 (128x64)
用法: python k5screen.py [COM口] [--save out.png] [--watch N秒]
"""
import sys
import time
import serial
import serial.tools.list_ports

BAUD = 38400
LCD_W, LCD_H = 128, 64

# K5Viewer chunk: 128 chunks = 8 lines x 16 chunks/line
# chunk payload[8]: bit j of column (base + j*8 + k) -> payload[j] bit k
def chunk_to_pixels(chunks, fb):
    """fb[y][x] bool. chunks: dict idx->8 bytes"""
    for idx, payload in chunks.items():
        line = idx // 16
        chunk_in_line = idx % 16
        bit = chunk_in_line // 2
        col_base = (chunk_in_line % 2) * 64
        for j in range(8):
            for k in range(8):
                x = col_base + j * 8 + k
                y = line * 8 + bit
                if x < LCD_W and y < LCD_H:
                    fb[y][x] = (payload[j] >> k) & 1


def render_ascii(fb):
    """把 128x64 位图渲染成 64x32 字符 (每字符 2x2 像素)"""
    shades = " ░▒▓█"
    out = []
    for y in range(0, LCD_H, 2):
        row = []
        for x in range(0, LCD_W, 2):
            n = fb[y][x] + fb[y][x+1] + fb[y+1][x] + fb[y+1][x+1]  # 0..4
            row.append(shades[n])
        out.append("".join(row))
    return "\n".join(out)


def render_png(fb, path):
    from PIL import Image
    img = Image.new("1", (LCD_W, LCD_H), 0)
    px = img.load()
    for y in range(LCD_H):
        for x in range(LCD_W):
            px[x, y] = 1 if fb[y][x] else 0
    img = img.resize((LCD_W * 3, LCD_H * 3))
    img.save(path)


def main():
    args = sys.argv[1:]
    port = None
    save = None
    watch = 15
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--save":
            save = args[i + 1]; i += 2
        elif a == "--watch":
            watch = int(args[i + 1]); i += 2
        else:
            port = a; i += 1
    if not port:
        port = next((p.device for p in serial.tools.list_ports.comports()), None)
    if not port:
        print("未找到串口")
        return

    print(f"打开 {port} @ {BAUD}, 监听屏幕流 {watch} 秒 ...")
    ser = serial.Serial(port, BAUD, timeout=0.3)
    time.sleep(0.2)
    ser.reset_input_buffer()
    ser.write(bytes([0x55, 0xAA, 0x00, 0x00]))  # keepalive 激活

    chunks = {}
    buf = bytearray()
    deadline = time.time() + watch
    while time.time() < deadline:
        buf += ser.read(512)
        # 解析 AA 55 type len_hi len_lo ... 帧
        while True:
            h = buf.find(b"\xaa\x55")
            if h < 0:
                if len(buf) > 4:
                    del buf[:-4]
                break
            if h > 0:
                del buf[:h]
            if len(buf) < 5:
                break
            ftype = buf[2]
            length = (buf[3] << 8) | buf[4]
            if ftype == 0x02:  # TYPE_DIFF 屏幕帧
                # 帧体 = length 字节 chunk 数据 + 0x0A 尾
                total = 5 + length + 1
                if len(buf) < total:
                    break
                body = buf[5:5 + length]
                # 每 9 字节一个 chunk: idx + 8 payload
                for off in range(0, len(body) - 8, 9):
                    idx = body[off]
                    if idx < 128:
                        chunks[idx] = body[off + 1:off + 9]
                del buf[:total]
            else:
                # 其他类型帧, 跳过
                total = 5 + length + 1
                if len(buf) < total:
                    break
                del buf[:total]
    ser.close()

    print(f"共捕获 {len(chunks)} 个 chunk (满帧 128)")
    if not chunks:
        print("没有屏幕数据。确认 K5Viewer 已激活 (keepalive 后画面变化时才发)")
        return

    fb = [[0] * LCD_W for _ in range(LCD_H)]
    chunk_to_pixels(chunks, fb)

    print("\n===== 对讲机屏幕内容 =====")
    print(render_ascii(fb))
    print("==========================")
    if save:
        render_png(fb, save)
        print(f"已保存: {save}")


if __name__ == "__main__":
    main()
