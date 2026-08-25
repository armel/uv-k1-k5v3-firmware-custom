#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
对讲机串口诊断工具（F4HWN Fusion 固件）
用法:
    python probe_radio.py            # 自动选择唯一串口
    python probe_radio.py COM4       # 指定串口
    python probe_radio.py --all      # 列出所有串口

诊断项:
  ① 查询固件版本    (0x0514 -> 0x0515)
  ② 探测多普勒功能  (0x05E1 错误长度 -> 有回复=支持, 无回复=不支持)
  ③ 读取设置区      (0x051B -> 0x051C, 检查 MENU_LOCK)
"""
import sys
import time
import struct

import serial
import serial.tools.list_ports

BAUD = 38400  # F4HWN UART 协议波特率 (App/driver/uart.c)
TIMEOUT_CMD = 3.0

OBFUSCATION = [0x16, 0x6c, 0x14, 0xe6, 0x2e, 0x91, 0x0d, 0x40,
               0x21, 0x35, 0xd5, 0x40, 0x13, 0x03, 0xe9, 0x80]

TS = 0x11223344


def crc16(data: bytes) -> int:
    """CRC-16 XMODEM (poly 0x1021, init 0), 与固件 App/driver/crc.c 一致"""
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xffff if crc >> 15 else (crc << 1) & 0xffff
    return crc


def build_frame(cmd_id: int, payload: bytes) -> bytes:
    body = struct.pack("<HH", cmd_id, len(payload)) + payload
    # 固件解密 Size+2 字节 = body + CRC (uart.c:802), 所以 CRC 必须纳入加密区
    enc = body + struct.pack("<H", crc16(body))
    frame = bytearray([0xAB, 0xCD])
    frame += struct.pack("<H", len(body))  # Size = body 长度
    frame += bytes(b ^ OBFUSCATION[i % 16] for i, b in enumerate(enc))
    frame += bytes([0xDC, 0xBA])
    return bytes(frame)


class RadioLink:
    def __init__(self, port: str, baud: int = BAUD):
        self.ser = serial.Serial(port, baud, timeout=0.3)
        self.rx = bytearray()

    def close(self):
        self.ser.close()

    def set_baud(self, baud: int):
        self.ser.baudrate = baud
        self.ser.reset_input_buffer()
        self.rx = bytearray()

    def drain(self):
        """清空输入缓冲（丢弃对讲机自发的数据）"""
        self.ser.reset_input_buffer()
        self.rx = bytearray()

    def _next_frame(self) -> bytes | None:
        """从 rx 缓冲里取一帧（重同步），返回明文 payload；不足则返回 None"""
        for _ in range(200):
            h = self.rx.find(b"\xab\xcd")
            if h < 0:
                self.rx = self.rx[-1:]  # 保留 1 字节防半截头
                return None
            if h > 0:
                del self.rx[:h]
            if len(self.rx) < 6:
                return None
            size = self.rx[2] | (self.rx[3] << 8)
            total = 8 + size
            if len(self.rx) < total:
                return None
            # 回复帧: [AB CD][Size][body][Padding(2)][DC BA]
            # Padding 在 body 与 DC BA 之间 (uart.c SendReply Footer)
            if self.rx[4 + size + 2] != 0xDC or self.rx[4 + size + 3] != 0xBA:
                del self.rx[:2]  # 尾帧错，重同步
                continue
            payload = bytes(self.rx[4 + i] ^ OBFUSCATION[i % 16] for i in range(size))
            del self.rx[:total]
            # 固件回复的 2 字节是 Padding (加密填充), 不是 CRC (uart.c SendReply) - 不校验
            return payload
        return None

    def send(self, cmd_id: int, payload: bytes) -> None:
        frame = build_frame(cmd_id, payload)
        print(f"  → 发送 0x{cmd_id:04X}  payload={payload.hex(' ')}")
        self.ser.write(frame)

    def recv_reply(self, reply_id: int) -> bytes | None:
        """等待指定 ID 的回复，超时返回 None"""
        deadline = time.time() + TIMEOUT_CMD
        while time.time() < deadline:
            self.rx += self.ser.read(256)
            while True:
                payload = self._next_frame()
                if payload is None:
                    break
                rid, rsize = struct.unpack("<HH", payload[:4])
                print(f"  ← 收到 0x{rid:04X}  size={rsize}  data={payload[4:].hex(' ')}")
                if rid == reply_id:
                    return payload
        return None


def loopback_test(port: str) -> None:
    """回环测试: 发送一串已知字节, 看是否能原样收回来 (CH340 TX 与 RX 短接时)"""
    print("\n[回环测试]")
    print("  请把 CH340 插头处的 TX 与 RX 用镊子/杜邦线短接")
    print("  10 秒后开始测试 ...")
    for i in range(10, 0, -1):
        print(f"  {i} ...", end="\r")
        time.sleep(1)
    print("  开始测试")
    link = RadioLink(port)
    link.drain()
    pattern = bytes(range(256))
    link.ser.write(pattern)
    got = link.ser.read(512)
    if got == pattern:
        print("  ✔ 回环 OK：CH340 线 + 驱动 + 波特率全部正常")
        print("     → 问题在对讲机侧（接线/开机/固件）")
    elif got:
        print(f"  ✘ 收到 {len(got)} 字节但不匹配（可能是没短接好或线有问题）:")
        print("    " + got.hex(' ')[:200])
    else:
        print("  ✘ 一个字节都没收到 → CH340 线/驱动有问题，或 TX/RX 没短接")
    link.close()


def listen_test(port: str, seconds: int = 12) -> None:
    """监听模式: 打开串口持续收数据。对讲机关机后运行本命令, 再开机,
    固件开机时会主动发送 UART_Version 字符串 (main.c:88)"""
    print(f"\n[监听模式] 打开 {port} 监听 {seconds} 秒")
    print("  步骤: 先保持对讲机关机 → 运行本命令 → 听到提示后给对讲机开机")
    print("  等待 {seconds} 秒 ...")
    for i in range(3, 0, -1):
        print(f"  {i} ...", end="\r")
        time.sleep(1)
    link = RadioLink(port)
    link.drain()
    got = bytearray()
    deadline = time.time() + seconds
    while time.time() < deadline:
        chunk = link.ser.read(256)
        if chunk:
            got.extend(chunk)
            print("  收到 " + repr(chunk))
    link.close()
    if got:
        try:
            text = got.decode("ascii", "replace")
            print(f"\n  ✔ 共收到 {len(got)} 字节: {text!r}")
            if "UV-K5" in text or "Firmware" in text or "F4HWN" in text:
                print("  → 这就是固件开机版本串! CH340 线已接对 PA9/PA10, UART 工作正常")
            else:
                print("  → 收到数据但不是版本串, 可能是噪声/其他设备")
        except Exception:
            print(f"\n  ✔ 共收到 {len(got)} 字节 (非文本)")
    else:
        print("\n  ✘ 12 秒内一个字节都没有")
        print("  → CH340 线的 RX 没接到对讲机 PA9(TX), 或线本身不通")


def main():
    args = [a for a in sys.argv[1:]]
    if "--all" in args or "-l" in args:
        for p in serial.tools.list_ports.comports():
            print(f"{p.device:8s} {p.description}")
        return
    if "--loopback" in args:
        others = [a for a in args if a != "--loopback"]
        port = others[0] if others else \
            (next((p.device for p in serial.tools.list_ports.comports()), None))
        if not port:
            print("未找到串口")
            return
        loopback_test(port)
        return
    if "--listen" in args:
        others = [a for a in args if a != "--listen"]
        port = others[0] if others else \
            (next((p.device for p in serial.tools.list_ports.comports()), None))
        if not port:
            print("未找到串口")
            return
        listen_test(port)
        return

    ports = serial.tools.list_ports.comports()
    if args:
        port = args[0]
    elif len(ports) == 1:
        port = ports[0].device
    else:
        print("多个串口，请指定：python probe_radio.py COMx")
        for p in ports:
            print(f"  {p.device}  {p.description}")
        return

    print(f"打开 {port} @ {BAUD} baud ...")
    link = RadioLink(port)
    link.drain()
    time.sleep(0.2)

    # ---------- ① 查询版本（先 38400，失败则扫描常见波特率） ----------
    print("\n[① 查询固件版本]")
    link.send(0x0514, struct.pack("<I", TS))
    r = link.recv_reply(0x0515)
    if r is None:
        # 换波特率重试: 原厂 Quansheng 固件是 19200, 部分机型 115200/9600
        for baud in (19200, 115200, 9600):
            print(f"  38400 无应答，尝试 {baud} baud ...")
            link.set_baud(baud)
            link.drain()
            link.send(0x0514, struct.pack("<I", TS))
            r = link.recv_reply(0x0515)
            if r is not None:
                print(f"  ✔ 该固件工作在 {baud} baud")
                break
    if r is None:
        print("  ✘ 所有波特率均无回复！请确认：")
        print("    1. 对讲机已开机（显示主界面，不是关机/DFU/充电状态）")
        print("    2. 刷机线接线正确（CH340 的 RX/TX 需接对讲机串口，共地）")
        print("    3. 固件是 F4HWN 系列（原厂固件 UART 口通常不输出）")
        link.close()
        return
    ver = r[4:20].split(b"\x00")[0].decode("ascii", "replace")
    print(f"  ✔ 固件版本: \"{ver}\"")
    if "FUSION" in ver.upper():
        print("  → 是 F4HWN Fusion 固件")
    elif ver:
        print("  → 不是 Fusion 固件！请用 UV Studio 刷 build\\Fusion\\f4hwn.fusion.bin")
    else:
        print("  → 版本字符串无法解析")

    # ---------- ② 探测多普勒 ----------
    print("\n[② 探测多普勒功能]")
    # 故意发错误长度 (4 字节): 带多普勒的固件回 0x05E4 Status=1 (拒绝, 不写数据)
    link.drain()
    link.send(0x05E1, b"\x00" * 4)
    r = link.recv_reply(0x05E4)
    if r is None:
        print("  ✘ 无回复 → 固件【不包含】多普勒功能")
        print("    请用 UV Studio 重新刷入 build\\Fusion\\f4hwn.fusion.bin (114,788 字节)")
    else:
        status = r[4]
        print(f"  ✔ 固件支持多普勒功能 (Status={status}, 未写入任何数据)")

    # ---------- ③ 读取设置 ----------
    print("\n[③ 读取设置区 (KEY_LOCK/MENU_LOCK 检查)]")
    # KEY_LOCK/MENU_LOCK 在 flash 0x00A000 = I2C EEPROM 0x0E70 (settings.c:139 注释)
    # CMD_051B_t payload = Offset(2) + Size(1) + Padding(1) + Timestamp(4) = 8 字节
    body = struct.pack("<HBB", 0x0E70, 16, 0) + struct.pack("<I", TS)
    link.drain()
    link.send(0x051B, body)
    r = link.recv_reply(0x051C)
    if r is None:
        print("  ✘ 无回复（某些固件需要密码流程，可跳过此项）")
    else:
        data = r[8:24]  # Data[16]
        b4 = data[4]
        key_lock = (b4 & 0x01) != 0
        menu_lock = (b4 & 0x02) != 0
        set_key = (b4 >> 2) & 0x0F
        print(f"  设置字节[4] = 0x{b4:02X}")
        print(f"  KEY_LOCK  = {'开 ← 键盘锁! 长按 # (>400ms) 解锁' if key_lock else '关'}")
        print(f"  MENU_LOCK = {'开 ← F+数字 全失效!' if menu_lock else '关'}")
        print(f"  SET_KEY   = {set_key}")

    link.close()
    print("\n完成。")


if __name__ == "__main__":
    main()
