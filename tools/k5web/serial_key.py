#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""串口按键注入 (K5Viewer 协议, keyboard.c)
用法: python serial_key.py [COM口] <命令...>
命令:
  long F      长按 F (#) 键  → 解锁键盘锁
  long 0      长按 0        → 进入多普勒
  short F     短按 F (#)
  short 0     短按 0        (F+0 组合: 先 short F 再 short 0 = FM 收音机)
  short MENU  短按 MENU
  long MENU   长按 MENU
  raw <hex..> 自定义按键码

按键码: 0..9=数字, MENU=10, UP=11, DOWN=12, EXIT=13, STAR=14(*), F=15(#)
"""
import sys
import time
import serial
import serial.tools.list_ports

BAUD = 38400

KEYCODES = {"0": 0, "1": 1, "2": 2, "3": 3, "4": 4, "5": 5, "6": 6, "7": 7,
            "8": 8, "9": 9, "MENU": 10, "UP": 11, "DOWN": 12, "EXIT": 13,
            "STAR": 14, "F": 15}


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    port = args[0]
    ops = args[1:] if len(args) > 1 else []
    if not ops:
        print(__doc__)
        return

    print(f"打开 {port} @ {BAUD}")
    ser = serial.Serial(port, BAUD, timeout=0.3)
    time.sleep(0.2)

    # keepalive 激活连接
    ser.write(bytes([0x55, 0xAA, 0x00, 0x00]))
    time.sleep(0.05)

    i = 0
    while i < len(ops):
        op = ops[i].upper()
        if op in ("LONG", "SHORT"):
            key = ops[i + 1].upper()
            code = KEYCODES.get(key)
            if code is None:
                print(f"未知按键: {key}")
                i += 2
                continue
            ktype = 0x04 if op == "LONG" else 0x03
            pkt = bytes([0xAA, 0x55, ktype, code])
            ser.write(pkt)
            print(f"  发送 {'长按' if op == 'LONG' else '短按'} {key}: {pkt.hex(' ')}")
            time.sleep(0.5 if op == "LONG" else 0.3)
            i += 2
        elif op == "RAW":
            code = int(ops[i + 1], 16)
            pkt = bytes([0xAA, 0x55, 0x03, code])
            ser.write(pkt)
            print(f"  发送 raw 0x{code:02X}: {pkt.hex(' ')}")
            time.sleep(0.3)
            i += 2
        else:
            print(f"未知命令: {ops[i]}")
            i += 1

    time.sleep(0.5)
    # 顺便读取返回
    got = ser.read(256)
    if got:
        print(f"  对讲机返回 {len(got)} 字节: {got[:64].hex(' ')}")
    ser.close()
    print("完成。观察对讲机屏幕变化。")


if __name__ == "__main__":
    main()
