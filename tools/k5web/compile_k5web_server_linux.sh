#!/usr/bin/env bash
# 对标 Windows 版 compile_k5web_server.bat：
# 用 g++ 编译 Linux 版 K5Web 服务器（原生 C++，无需 Python 运行时）。
# 用法：bash compile_k5web_server_linux.sh
# 依赖：g++（Debian/Ubuntu/UOS: sudo apt install build-essential）

cd "$(dirname "$0")" || exit 1

CXX="${CXX:-g++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "[ERROR] $CXX 未找到，请先安装编译工具："
    echo "  Debian / Ubuntu / UOS: sudo apt install build-essential"
    echo "  Fedora:                 sudo dnf install gcc-c++"
    echo "  Arch:                   sudo pacman -S gcc"
    echo "  openSUSE:               sudo zypper install gcc-c++"
    exit 1
fi

"$CXX" -O2 -s -std=c++11 -Wall k5web_server_linux.cpp -o k5web_server_linux -pthread
if [ $? -ne 0 ]; then
    echo "[ERROR] 编译失败。"
    exit 1
fi

echo "[OK] k5web_server_linux 编译完成（静态网页服务 :8080 + NTP 时间代理 :8765）。"
echo "     运行: bash 打开烧写网页.sh"
