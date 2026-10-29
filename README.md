# 摄像头光路游戏 · 2026-10-29

AX7Z020B / Zynq-7020 + OV5640，1280×720。板端运行 Cortex-A9/core0 Standalone，检测实物并在 HDMI 叠加模型和光路；电脑端使用 Python 共享运行层，选择 Godot 或 Three.js 网页呈现。线上协议为二进制 v3，配套 version14 固件。

这是从当前工作区整理的独立工程快照，包含尚未提交 Git 的最新源码。网页版本 `20261029-web-recovery-quit-5`，Python 运行层 `20261028-binary-v3-350-6`。唯一手工游戏配置为 `pc_vision/game_config.json`；电脑场景准入350ms，校时质量上限250ms，板端 B 支持间隔350ms。网页和 Godot 共用同一串口运行层、C 核心和配置，一次只启动一个上位机。

| 目录 | 内容 |
| --- | --- |
| `color_test/software/vision_ps_baremetal/` | 当前板端应用、共用 C 核心、配置生成器和相关测试 |
| `color_test/repo/ip/`、`color_test/color_test.srcs/`、`color_test/rtl/` | 一套当前可编辑 PL/BD/IP/XDC 规范源 |
| `firmware/version14/` | 当前 BOOT/ELF、冻结 XSA/BIT/FSBL/BSP/已编译配置和原校验证据 |
| `pc_vision/` | 当前 Python 串口运行层、Godot 工程与资源 |
| `web_upper/pc_vision/` | 当前网页上位机及随附 Three.js 实际使用模块 |
| `tools/` | 整包核对和冻结平台应用构建入口 |

## 启动网页版

需要 Python 3.10+，以及主机 C 编译器。Windows 已验证工具路线为 VS2022 Professional 的 C/C++ 工具；也可使用 PATH 中的 GCC。网页不需要 OpenCV、NumPy、Godot、npm 或联网加载 Three.js。

在工程根目录执行：

```powershell
python -m venv .venv
& ./.venv/Scripts/python.exe -m pip install -r requirements.txt
& ./web_upper/pc_vision/start_game.ps1 -PythonPath "$PWD/.venv/Scripts/python.exe" -Check
& ./web_upper/pc_vision/start_game.ps1 -PythonPath "$PWD/.venv/Scripts/python.exe" -SerialPort COM9
```

浏览器打开 `http://127.0.0.1:8765/`。端口按实际设备替换，串口115200/8N1。只看离线布局可将 `-SerialPort COM9` 换成 `-Preview`，它不打开串口或计分。页面顶部和错误页均可“结束程序”，后端关闭电脑串口/C子进程；也可在终端 Ctrl+C。启动脚本不下载或复位板卡。

运行中手部遮挡/等待识别时保留最后一份完整场景，新有效场景原子替换。保留场景的原采集时间不刷新，过期数据不计分。每关开始后180秒；配置内后12关为已保存布局，未保证可解。

## 启动 Godot / Python 上位机

安装同一 Python 依赖后，另行安装 Godot 4（原工程验证版本4.7.2），关闭网页后台，然后：

```powershell
& ./.venv/Scripts/python.exe -B -m pc_vision.godot_bridge --check
# 让 python 命令指向上述虚拟环境，或先执行 ./.venv/Scripts/Activate.ps1
& ./pc_vision/start_game.ps1 -SerialPort COM9 -GodotPath 'D:/Tools/Godot/Godot_console.exe'
```

Godot 资源首次由启动脚本导入。也可直接使用虚拟环境 Python：

```powershell
& ./.venv/Scripts/python.exe -m pc_vision.godot_bridge --serial COM9 --godot 'D:/Tools/Godot/Godot_console.exe'
```

## 板端与离线核对

当前可交付固件为 `firmware/version14/output/BOOT.BIN`（2278608字节），SHA256：
`4abcf689a6fc933c946851915aceaaca7850e7e457a930c66f098f5229b5fe88`。

```powershell
python -B tools/verify_package.py
python -B tools/build_board.py --check
```

重编译应用见 [板端说明](color_test/software/vision_ps_baremetal/README.md)。原有历史 Vitis 工作区、旧固件和重复 PYNQ 工程没有加入本包；冻结平台足以支持当前应用编译。现有 BOOT 未重新生成。当前 PL 规范源与冻结旧硬件的来源边界见 [硬件说明](color_test/README.md)，不能据此声称重建 PL 与当前 BOOT 位级相同。

## 离线开发验证

```powershell
python -B -m unittest pc_vision.test_board_bridge pc_vision.test_board_wire pc_vision.test_serial_handshake -v
Push-Location web_upper
python -B -m unittest pc_vision.test_web_game -v
Pop-Location
Push-Location web_upper/pc_vision/web
node --check main.js
node --check portals.js
node --check presentation.js
node --test presentation.test.js
Pop-Location
```

这些是主机/离线检查，不能代替摄像头交互或端到端帧率测量。用户此前确认 version14 使用成功；本次仅做工程整理和离线核对，没有打开真实 UART/JTAG、操作板卡、写 SD、重编译 ARM 或运行 Vivado。

## GitHub 上传

将本目录的内容作为一个仓库上传即可。上一级两个 ZIP 分别是同内容合并包与网页独立备份，不必重复上传到仓库。未包含 `.git` 历史、个人环境、日志、串口记录、临时输出或旧发布包。所有交付文件的 SHA256 见 `SHA256SUMS`；仅用于核对本次快照，后续正常修改源码后需要自行更新清单。

第三方许可保留在原目录，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。本次未替用户选择项目整体开源许可证。
