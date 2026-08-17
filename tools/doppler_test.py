#!/usr/bin/env python3
"""
Doppler 数据层逻辑单元测试（PC 端）

复现 App/app/doppler.c 中的时间换算与查表算法，与 Python 标准库
(datetime) 对照验证，确保移植到固件前逻辑正确。

用法: python tools/doppler_test.py
"""

import datetime
import struct
import sys

# ---- 复刻 App/app/doppler.c 的 C 算法（必须与固件代码保持同步） ----

def is_leap_year(year2000):
    """DOPPLER_IsLeapYear: year2000 = 2000 + 年数"""
    y = 2000 + year2000
    return (y % 4 == 0 and y % 100 != 0) or y % 400 == 0

DAYS_IN_MONTH = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]

def unix_time(t):
    """DOPPLER_UnixTime: t = [year2000, month, day, hour, minute, second]
    返回 2000-01-01 00:00:00 起算的秒数"""
    year, month = t[0], t[1]
    seconds = 0
    for y in range(year):
        seconds += (366 if is_leap_year(y) else 365) * 86400
    for m in range(1, month):
        seconds += DAYS_IN_MONTH[m - 1] * 86400
        if m == 2 and is_leap_year(year):
            seconds += 86400
    if t[2] > 0:
        seconds += (t[2] - 1) * 86400
    seconds += t[3] * 3600 + t[4] * 60 + t[5]
    return seconds

# ---- 查表逻辑（与 DOPPLER_GetEntry 一致） ----
DOPPLER_MAX_ENTRIES = 1920

def get_entry(start_unix, sum_time, table, unix_now):
    """返回 (index, entry) 或 None"""
    diff = unix_now - start_unix
    if diff < 0:
        return None
    entry_count = (sum_time + 1) >> 1
    index = diff >> 1
    if index >= entry_count or index >= DOPPLER_MAX_ENTRIES:
        return None
    return index, table[index]

# ---- 参考实现：Python datetime ----
EPOCH = datetime.datetime(2000, 1, 1, 0, 0, 0)

def unix_time_ref(t):
    dt = datetime.datetime(2000 + t[0], t[1], t[2], t[3], t[4], t[5])
    return int((dt - EPOCH).total_seconds())

# ---- 测试 ----
def test_unix_time():
    cases = [
        # (年2000, 月, 日, 时, 分, 秒)
        (0, 1, 1, 0, 0, 0),    # 2000-01-01 00:00:00 = 0
        (0, 1, 1, 0, 0, 1),    # +1s
        (0, 2, 29, 0, 0, 0),   # 2000 闰年 2/29
        (1, 3, 1, 0, 0, 0),    # 2001-03-01（含 2000 闰年）
        (4, 2, 29, 12, 30, 45),# 2004 闰年
        (24, 12, 31, 23, 59, 59), # 2024 年底
        (26, 8, 17, 10, 0, 0), # 2026-08-17（今天）
        (99, 12, 31, 23, 59, 59), # 2099 年底（非闰年）
    ]
    for t in cases:
        got = unix_time(t)
        ref = unix_time_ref(t)
        assert got == ref, f"FAIL {t}: got={got} ref={ref}"
        print(f"  OK  {t} -> {got} s")

def test_get_entry():
    start = unix_time([26, 8, 17, 12, 0, 0])   # 2026-08-17 12:00:00
    sum_time = 600                              # 10 分钟过境
    # 每 2 秒一条：uplink/downlink 模拟多普勒频偏
    table = []
    for i in range((sum_time + 1) >> 1):
        table.append((43850000 + i * 100, 43750000 - i * 100))

    # 1) 未开始
    assert get_entry(start, sum_time, table, start - 1) is None
    # 2) 正好开始
    assert get_entry(start, sum_time, table, start) == (0, table[0])
    # 3) 第 1 秒（同一条）
    assert get_entry(start, sum_time, table, start + 1) == (0, table[0])
    # 4) 第 2 秒（下一条）
    assert get_entry(start, sum_time, table, start + 2) == (1, table[1])
    # 5) 最后一条（第 599 秒 -> index 299）
    assert get_entry(start, sum_time, table, start + 599) == (299, table[299])
    # 6) 过境结束
    assert get_entry(start, sum_time, table, start + 600) is None
    # 7) 奇数 sum_time 的边界（表长 = (601+1)>>1 = 301 条）
    odd_table = [(43850000 + i * 100, 43750000 - i * 100) for i in range((601 + 1) >> 1)]
    assert get_entry(start, 601, odd_table, start + 601) == (300, odd_table[300])
    # 8) 超长过境截断（超过 MAX_ENTRIES）
    long_sum = DOPPLER_MAX_ENTRIES * 2 + 10
    long_table = [(43850000, 43750000)] * DOPPLER_MAX_ENTRIES
    assert get_entry(start, long_sum, long_table, start + DOPPLER_MAX_ENTRIES * 2) is None
    print("  OK  查表边界全部通过（未开始/进行中/结束/奇数时长/超长截断）")

def main():
    print("== DOPPLER_UnixTime 时间换算对照 ==")
    test_unix_time()
    print("== DOPPLER_GetEntry 查表边界 ==")
    test_get_entry()
    print("\n全部通过 ✅")

if __name__ == "__main__":
    sys.exit(main())
