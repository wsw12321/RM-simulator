# RMUC Tactics Lab 发布包 · 快速开始

**非官方 RMUC 2027 战术沙盒。尚未实现完整 2027 规则，不能据此评估真实比赛胜率。**

这个包已经包含编译好的 C++ 引擎，**不需要编译器**，解压后即可运行。
构建来源（提交、平台、编译器）记录在 `BUILD_INFO.json`。

## 系统要求

| 包 | 适用系统 |
|---|---|
| `linux-x86_64`（`.tar.gz`） | 64 位 x86 Linux（静态链接，不挑发行版和 glibc 版本），也可在 WSL 里用 |
| `macos-universal`（`.tar.gz`） | macOS 11 或更高，Apple Silicon 和 Intel 都可以 |
| `windows-x86_64`（`.zip`） | 64 位 Windows 10 / 11（静态链接，不需要安装运行库） |

另外需要：

- **Python 3.10 或更高版本**：只用标准库，不需要 `pip install`。
  Ubuntu 22.04 及以后自带；macOS 自带的 Python 是 3.9，请用 `brew install python` 或从 python.org 安装；
  Windows 请从 python.org 安装（安装器自带 `py` 启动器，`start.bat` 会优先用它）。
- 任意现代浏览器。

## 启动

Linux / macOS：

```bash
tar -xzf rmuc-tactics-lab-*.tar.gz
cd rmuc-tactics-lab-*/
bash start.sh
```

Windows：右键 zip →“全部解压缩”，进入解压出的文件夹，双击 `start.bat`
（或在命令提示符里进入该文件夹后运行 `start.bat`）。

脚本会检查 Python 版本和平台，启动只监听本机的服务，并自动打开浏览器；
没有桌面环境时请手动打开 `http://127.0.0.1:8765`。按 Ctrl+C 停止（Windows 上随后可能询问
“终止批处理操作吗”，输入 Y）。

- 换端口：`bash start.sh --port 8766`（Windows：`start.bat --port 8766`）
- 不自动打开浏览器：`--no-browser`
- 指定 Python：`PYTHON=python3.12 bash start.sh`（Windows：`set PYTHON="C:\Python312\python.exe"` 后再运行 `start.bat`）

主要页面：

| 地址 | 内容 |
|---|---|
| `/` | 主页（原双机战斗沙盒，可跳转到其他模式） |
| `/lab.html` | 2027 规则全流程实验对局回放 |
| `/roster.html` | 可配置多机器人战术场景 |
| `/tactical.html` | 双机交火与目标争夺 |
| `/combat.html` | 过滤观测的双机策略对战 |
| `/layered.html`、`/arena.html` | 分层移动实验、分层地图校准工作台 |

## 命令行工具

不启动服务也能直接用（Windows 把 `python3` 换成 `py`；输出重定向到文件时先 `set PYTHONUTF8=1`）：

```bash
python3 match_lab/cli.py run                 # 跑一局规则实验，输出中文摘要
python3 match_lab/cli.py sweep --seeds 20    # 同一配置 20 个种子的胜负统计（合成模型）
python3 scenario_harness/cli.py run scenario_harness/scenarios/timeout_control_priority.json
python3 integrated_scenario/cli.py run integrated_scenario/scenarios/capture_credit_timeout.json
```

## 修改策略后重新构建

包里带有完整源码。各模式在运行时会把源码重新哈希，与二进制中的构建 ID 比对，
所以**修改任何 C++ / 相关源码后都必须重新构建**，否则会提示 “binary is stale”。
重新构建需要支持 C++20 的编译器（GCC 13+ 或 Clang；Windows 用 MinGW-w64 GCC，例如 MSYS2 UCRT64）：

```bash
bash tools/build.sh              # Linux / macOS
py tools/native_build.py         # Windows（g++ 需在 PATH 中）
```

这会用本机编译器替换 `build/` 里的预编译程序，并运行全部原生测试。详见 `README.md`。

## 校验文件

`SHA256SUMS` 覆盖包内每个文件（包括预编译程序）：

```bash
sha256sum -c SHA256SUMS          # Linux
shasum -a 256 -c SHA256SUMS      # macOS
```

Windows 可以核对下载的 zip 本身：`certutil -hashfile <zip 文件名> SHA256`，与同名 `.sha256` 文件里的值比较。

## 常见问题

- **macOS 提示“无法验证开发者”**：预编译程序没有签名。`start.sh` 会自动移除 `build/` 的下载隔离标记；
  若直接运行 `python3 server.py`，请先执行 `xattr -dr com.apple.quarantine build`。
- **Windows 提示“Windows 已保护你的电脑”或“无法验证发布者”**：程序没有签名。确认 zip 来自本仓库的
  Release 后选择“仍要运行”；也可以在解压前右键 zip →“属性”→ 勾选“解除锁定”。个别杀毒软件可能误报未签名程序。
- **Windows 窗口显示 “Python 3.10 or newer was not found”**：没有找到 Python，请从 python.org 安装后重新运行 `start.bat`。
- **端口被占用**：`bash start.sh --port 8766`（Windows：`start.bat --port 8766`）。
- **提示 binary is stale**：源码与预编译程序不一致。重新构建，或重新解压原始包。
- **规则手册 PDF**：为控制体积，发布包不含 `docs/rules/` 下的官方规则手册原件，请在源码仓库中查看。
