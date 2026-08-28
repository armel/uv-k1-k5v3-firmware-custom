#!/usr/bin/env python3
"""
本地 NTP -> HTTP 时间代理
用法：python time_proxy.py
      然后网页通过 http://127.0.0.1:8765/time 获取 JSON 时间

优先查询阿里云 NTP (ntp.aliyun.com)，失败时依次尝试其他公共 NTP，
最后回退到本机系统时间。返回 JSON 中的 unixtime 为北京时间 1970-epoch 秒数。
"""

import http.server
import json
import socket
import struct
import sys
import time
import urllib.request
from datetime import datetime, timezone, timedelta

NTP_SERVERS = [
    "ntp.aliyun.com",
    "ntp.tencent.com",
    "cn.pool.ntp.org",
    "time.asia.apple.com",
    "pool.ntp.org",
]
NTP_PORT = 123
NTP_EPOCH = 2208988800  # 1900-01-01 到 1970-01-01 的秒数
HTTP_PORT = 8765

# NTP (UDP 123) 被网络拦截时的 HTTP 兜底时间源：读响应 Date 头（GMT/UTC）
HTTP_TIME_SOURCES = [
    ("https://httpbin.org/get", "httpbin"),
    ("https://mirrors.tuna.tsinghua.edu.cn/", "清华镜像"),
    ("https://www.baidu.com/", "百度"),
]


def query_ntp(server, timeout=2.0):
    """查询单个 NTP 服务器，返回 (unixtime_utc, server_name)。"""
    addr = (server, NTP_PORT)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    try:
        # NTP v3 请求：LI=0, VN=3, Mode=3 (client)；Transmit Timestamp 填当前时间
        pkt = bytearray(48)
        pkt[0] = 0x1B
        t0 = time.time() + NTP_EPOCH
        pkt[40:44] = struct.pack(">I", int(t0))
        pkt[44:48] = struct.pack(">I", int((t0 % 1) * (1 << 32)))
        sock.sendto(pkt, addr)
        data, _ = sock.recvfrom(1024)
        if len(data) < 48:
            raise ValueError("NTP response too short")
        recv_ts = struct.unpack(">I", data[32:36])[0] + struct.unpack(">I", data[36:40])[0] / (1 << 32)
        unix_utc = recv_ts - NTP_EPOCH
        return unix_utc, server
    finally:
        sock.close()


def get_ntp_time():
    """依次尝试 NTP 服务器，返回第一个成功的 (unixtime_utc, source)。"""
    last_err = None
    for server in NTP_SERVERS:
        try:
            return query_ntp(server)
        except Exception as e:
            last_err = e
            print(f"NTP 源失败：{server} -> {e}", file=sys.stderr)
    raise last_err or RuntimeError("所有 NTP 服务器均不可用")


def get_http_time():
    """NTP 被拦截时的兜底：请求 HTTP 服务并解析响应 Date 头（GMT）。
    返回 (unixtime_utc, source)，全部失败抛异常。"""
    last_err = None
    for url, name in HTTP_TIME_SOURCES:
        try:
            req = urllib.request.Request(url, method="GET",
                                         headers={"User-Agent": "k5web-time-proxy/1.0",
                                                  "Cache-Control": "no-cache"})
            with urllib.request.urlopen(req, timeout=5) as resp:
                date_hdr = resp.headers.get("Date")
            if not date_hdr:
                raise ValueError("响应无 Date 头")
            from email.utils import parsedate_to_datetime
            unix_utc = parsedate_to_datetime(date_hdr).timestamp()
            return unix_utc, f"{name} (HTTP Date)"
        except Exception as e:
            last_err = e
            print(f"HTTP 时间源失败：{name} -> {e}", file=sys.stderr)
    raise last_err or RuntimeError("所有 HTTP 时间源均不可用")


class TimeHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        # 关闭默认访问日志，保持终端干净
        pass

    def _send_json(self, status, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self._send_json(204, {})

    def do_GET(self):
        if self.path not in ("/time", "/time/"):
            self._send_json(404, {"error": "not found"})
            return
        try:
            unix_utc, source = get_ntp_time()
        except Exception:
            # NTP (UDP 123) 被网络拦截时，退到 HTTP Date 头；仍失败才用本机时间
            print("NTP 全部失败，尝试 HTTP 时间源...", file=sys.stderr)
            try:
                unix_utc, source = get_http_time()
            except Exception as e2:
                unix_utc = time.time()
                source = "本机系统时间（NTP/HTTP 均不可用）"
                print(f"HTTP 时间源也失败，回退本机时间：{e2}", file=sys.stderr)

        # 北京时间 = UTC + 8h
        unix_beijing = unix_utc + 8 * 3600
        dt = datetime.fromtimestamp(unix_utc, tz=timezone.utc) + timedelta(hours=8)
        self._send_json(200, {
            "unixtime": int(unix_beijing),
            "unixtime_utc": int(unix_utc),
            "datetime_beijing": dt.strftime("%Y-%m-%d %H:%M:%S"),
            "source": source,
        })


def main():
    server = http.server.HTTPServer(("127.0.0.1", HTTP_PORT), TimeHandler)
    print(f"NTP 代理已启动：http://127.0.0.1:{HTTP_PORT}/time")
    print("按 Ctrl+C 停止")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n已停止")


if __name__ == "__main__":
    main()
