#!/usr/bin/env bash
# RMUC Tactics Lab release launcher (Linux / macOS / WSL).
# Checks Python 3.10+ and the package platform, starts the local-only server
# and opens the browser once it answers.
#   bash start.sh                 # http://127.0.0.1:8765
#   bash start.sh --port 8766
#   bash start.sh --no-browser
# Must stay compatible with bash 3.2 (macOS /bin/bash).
set -euo pipefail
cd "$(dirname "$0")"

PYTHON="${PYTHON:-python3}"
port=8765
open_browser=1
args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --no-browser) open_browser=0 ;;
    --port)
      [ $# -ge 2 ] || { echo "--port 需要一个端口号" >&2; exit 2; }
      port="$2"; args+=(--port "$2"); shift ;;
    --port=*) port="${1#--port=}"; args+=("$1") ;;
    -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
    *) echo "未知参数：$1（可用：--port N、--no-browser）" >&2; exit 2 ;;
  esac
  shift
done

if ! command -v "$PYTHON" >/dev/null 2>&1; then
  echo "未找到 Python 3（需要 3.10 或更高版本，只用标准库，不需要 pip 安装任何包）。" >&2
  echo "  Ubuntu / Debian / WSL：sudo apt install python3" >&2
  echo "  macOS：brew install python，或从 https://www.python.org/downloads/ 安装" >&2
  exit 1
fi
if ! "$PYTHON" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'; then
  echo "需要 Python 3.10 或更高版本，当前 $PYTHON 是 $("$PYTHON" -V 2>&1)。" >&2
  echo "可以这样指定其他解释器：PYTHON=/path/to/python3.12 bash start.sh" >&2
  exit 1
fi

# The prebuilt binaries in build/ only run on the platform they were built for.
"$PYTHON" - <<'PY'
import json, platform, sys
want = json.load(open('BUILD_INFO.json', encoding='utf-8'))['platform']
system = {'Darwin': 'macos', 'Linux': 'linux'}.get(platform.system(), platform.system().lower())
machine = {'amd64': 'x86_64', 'aarch64': 'arm64'}.get(platform.machine().lower(), platform.machine().lower())
if want != f'{system}-{machine}' and not (want == 'macos-universal' and system == 'macos'):
    hint = '原生 Windows 不受支持，请在 WSL 中使用 linux-x86_64 包。' if system == 'windows' else '请下载对应平台的包，或按 README.md 从源码构建。'
    sys.exit(f'这个发布包是为 {want} 构建的，当前系统是 {system}-{machine}。{hint}')
PY

if [ "$(uname -s)" = Darwin ] && find build -xattrname com.apple.quarantine 2>/dev/null | grep -q .; then
  # A browser download marks every extracted file as quarantined, and Gatekeeper
  # then blocks the unsigned helper programs the server starts. Only this
  # package's own build/ directory is touched.
  echo "macOS：移除 build/ 中预编译程序的下载隔离标记（com.apple.quarantine），否则系统会拦截这些未签名的程序。"
  xattr -dr com.apple.quarantine build || echo "警告：移除隔离标记失败，可手动运行 xattr -dr com.apple.quarantine build" >&2
fi

url="http://127.0.0.1:${port}/"
if [ "$open_browser" = 1 ]; then
  "$PYTHON" - "$url" <<'PY' &
import os, platform, sys, time, urllib.request, webbrowser
url = sys.argv[1]
if platform.system() == 'Linux' and not (os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY')):
    sys.exit()  # no desktop session (server / plain WSL): use the printed address
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # never route loopback through a proxy
deadline = time.monotonic() + 20
while time.monotonic() < deadline:
    try:
        with opener.open(url + 'api/meta', timeout=0.5):
            break
    except OSError:
        time.sleep(0.2)
else:
    sys.exit()
webbrowser.open(url)
PY
fi

echo "启动本地服务：$url （只监听本机；按 Ctrl+C 停止）"
exec "$PYTHON" server.py ${args[@]+"${args[@]}"}
