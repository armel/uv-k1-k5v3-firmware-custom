#!/usr/bin/env bash
# ============================================================
#  打开烧写网页（Linux / UOS 版）
#  对标 Windows 版：点击打开烧写网页.bat
#
#  功能（与 .bat 完全一致）：
#    1. 检查 C++ 服务器是否已编译，未编译则提示
#    2. 检查服务器是否已在运行，在运行则直接打开页面
#    3. 后台启动 K5Web 服务器（NTP 时间代理 :8765 + HTTP 静态文件 :8080）
#    4. 最多等待 30 秒就绪，超时回退 file:// 直接打开 index.html
#    5. 自动打开浏览器
#
#  适配：Ubuntu / Debian / Deepin / UOS / 统信 / Fedora / Arch / openSUSE 等
#  用法：双击运行，或终端执行  bash 打开烧写网页.sh
# ============================================================

cd "$(dirname "$0")" || exit 1

HTTP_PORT=8080
NTP_PORT=8765
PAGE_URL="http://127.0.0.1:${HTTP_PORT}/index.html"
SERVER_BIN="./k5web_server_linux"

# ---------- 1. 检查服务器二进制 ----------
if [ ! -x "$SERVER_BIN" ]; then
    echo "[ERROR] 未找到 $SERVER_BIN"
    echo
    echo "请先编译 C++ 服务器："
    echo "  运行: bash compile_k5web_server_linux.sh"
    echo "  （Debian/Ubuntu/UOS 需先: sudo apt install build-essential）"
    echo
    echo "如改用 Python 版："
    echo "  终端1: python3 time_proxy.py"
    echo "  终端2: python3 -m http.server $HTTP_PORT"
    exit 1
fi

# ---------- HTTP 探测：curl 优先，其次 wget，最后 bash 内置 /dev/tcp ----------
http_ok() {
    local url="$1"
    if command -v curl >/dev/null 2>&1; then
        curl -s -o /dev/null --max-time 2 "$url" >/dev/null 2>&1
        return $?
    fi
    if command -v wget >/dev/null 2>&1; then
        wget -q -O /dev/null --timeout=2 "$url" >/dev/null 2>&1
        return $?
    fi
    # 兜底：仅探测 TCP 连通性
    local host rest h p
    rest="${url#http://}"
    host="${rest%%/*}"
    h="${host%%:*}"; p="${host##*:}"
    if timeout 2 bash -c "exec 3<>/dev/tcp/$h/$p" 2>/dev/null; then
        return 0
    fi
    return 1
}

# ---------- 打开浏览器：xdg-open 优先，兼容各桌面环境 ----------
open_page() {
    local url="$1"
    if command -v xdg-open >/dev/null 2>&1; then
        nohup xdg-open "$url" >/dev/null 2>&1 &
        return 0
    fi
    for opener in gio kde-open exo-open gnome-open; do
        if command -v "$opener" >/dev/null 2>&1; then
            nohup "$opener" "$url" >/dev/null 2>&1 &
            return 0
        fi
    done
    echo "[WARN] 未找到可用的浏览器打开命令，请手动在浏览器中打开："
    echo "       $url"
    return 1
}

# ---------- 2. 服务器已在运行？ ----------
if http_ok "http://127.0.0.1:${HTTP_PORT}/"; then
    echo "[INFO] K5Web 服务器已在运行"
    open_page "$PAGE_URL"
    echo
    echo "============================================================"
    echo "  NTP 时间代理: http://127.0.0.1:${NTP_PORT}/time"
    echo "  网页工具:     $PAGE_URL"
    echo "============================================================"
    echo
    echo "说明："
    echo "  1. 关闭本终端不影响服务器运行"
    echo "  2. 停止服务器: pkill -f k5web_server_linux"
    echo
    exit 0
fi

# ---------- 3. 后台启动服务器 ----------
echo "正在启动 K5Web 服务器（NTP 代理 + HTTP 文件服务）..."
nohup "$SERVER_BIN" > k5web_server.log 2>&1 &

# ---------- 4. 等待就绪（最多 30 秒） ----------
echo "等待服务器就绪..."
RETRIES=30
while [ "$RETRIES" -gt 0 ]; do
    if http_ok "http://127.0.0.1:${HTTP_PORT}/"; then
        break
    fi
    RETRIES=$((RETRIES - 1))
    sleep 1
done

if [ "$RETRIES" -le 0 ]; then
    echo "[WARN] 服务器启动超时，回退 file:// 直接打开页面"
    echo "       排障可查看日志: $(pwd)/k5web_server.log"
    PAGE_URL="file://$(pwd)/index.html"
else
    echo "[INFO] 服务器就绪"
fi

# ---------- 5. 打开页面 ----------
open_page "$PAGE_URL"

echo
echo "============================================================"
echo "  NTP 时间代理: http://127.0.0.1:${NTP_PORT}/time"
echo "  网页工具:     $PAGE_URL"
echo "  服务器日志:   $(pwd)/k5web_server.log"
echo "============================================================"
echo
echo "说明："
echo "  1. 关闭本终端不影响服务器运行"
echo "  2. 停止服务器: pkill -f k5web_server_linux"
echo
