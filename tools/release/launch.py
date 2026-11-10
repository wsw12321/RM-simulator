"""RMUC Tactics Lab release launcher, shared by start.sh and start.bat.

Checks the Python version and the package platform, clears the macOS download
quarantine on the package's own build/ directory, opens the browser once the
local-only server answers, then runs server.py in this process.

  bash start.sh [--port 8766] [--no-browser]     (Linux / macOS / WSL)
  start.bat [--port 8766] [--no-browser]         (Windows)
"""
import sys

if sys.version_info < (3, 10):  # keep this check runnable on old interpreters
    sys.exit('需要 Python 3.10 或更高版本，当前是 %d.%d。' % sys.version_info[:2]
             + '可用 PYTHON 环境变量指定其他解释器，例如 PYTHON=python3.12。')

import argparse  # noqa: E402
import json  # noqa: E402
import os  # noqa: E402
import platform  # noqa: E402
import runpy  # noqa: E402
import subprocess  # noqa: E402
import threading  # noqa: E402
import time  # noqa: E402
import urllib.request  # noqa: E402
import webbrowser  # noqa: E402
from pathlib import Path  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def current_platform():
    system = {'Darwin': 'macos'}.get(platform.system(), platform.system().lower())
    machine = {'amd64': 'x86_64', 'aarch64': 'arm64'}.get(platform.machine().lower(), platform.machine().lower())
    return system, machine


def check_platform():
    """Prebuilt binaries only run where they were built; a source checkout has no BUILD_INFO.json."""
    info = ROOT / 'BUILD_INFO.json'
    if not info.is_file():
        return
    want = json.loads(info.read_text(encoding='utf-8'))['platform']
    system, machine = current_platform()
    if want == f'{system}-{machine}' or (want == 'macos-universal' and system == 'macos'):
        return
    if want.startswith('windows-') or system == 'windows':
        hint = '请下载与系统对应的包：Windows 用 windows-x86_64，WSL 里用 linux-x86_64。'
    else:
        hint = '请下载对应平台的包，或按 README.md 从源码构建。'
    sys.exit(f'这个发布包是为 {want} 构建的，当前系统是 {system}-{machine}。{hint}')


def clear_quarantine():
    """A browser download quarantines every extracted file, and Gatekeeper then blocks the
    unsigned helper programs the server starts. Only this package's build/ is touched."""
    if platform.system() != 'Darwin' or not (ROOT / 'BUILD_INFO.json').is_file():
        return
    marked = [p for p in (ROOT / 'build').iterdir()
              if subprocess.run(['xattr', '-p', 'com.apple.quarantine', str(p)], capture_output=True).returncode == 0]
    if marked:
        print('macOS：移除 build/ 中预编译程序的下载隔离标记（com.apple.quarantine），否则系统会拦截这些未签名的程序。')
        if subprocess.run(['xattr', '-dr', 'com.apple.quarantine', str(ROOT / 'build')]).returncode:
            print('警告：移除隔离标记失败，可手动运行 xattr -dr com.apple.quarantine build', file=sys.stderr)


def open_browser_when_ready(url):
    if platform.system() == 'Linux' and not (os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY')):
        return  # no desktop session (server, plain WSL): use the printed address
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # never route loopback through a proxy

    def wait_and_open():
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                with opener.open(url + 'api/meta', timeout=0.5):
                    webbrowser.open(url)
                    return
            except OSError:
                time.sleep(0.2)

    threading.Thread(target=wait_and_open, daemon=True).start()


def main():
    parser = argparse.ArgumentParser(description='启动 RMUC Tactics Lab 本地服务（只监听 127.0.0.1）')
    parser.add_argument('--port', type=int, default=8765, help='端口（默认 8765）')
    parser.add_argument('--no-browser', action='store_true', help='不自动打开浏览器')
    args = parser.parse_args()
    check_platform()
    clear_quarantine()
    url = f'http://127.0.0.1:{args.port}/'
    if not args.no_browser:
        open_browser_when_ready(url)
    print(f'启动本地服务：{url} （只监听本机；按 Ctrl+C 停止）', flush=True)
    os.chdir(ROOT)
    sys.argv = [str(ROOT / 'server.py'), '--port', str(args.port)]
    sys.path.insert(0, str(ROOT))
    runpy.run_path(str(ROOT / 'server.py'), run_name='__main__')


if __name__ == '__main__':
    main()
