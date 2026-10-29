# 电脑 Python 运行层与 Godot

当前运行层 `20261028-binary-v3-350-6`，配套板端 version14 / 二进制v3。配置只读取本目录 `game_config.json`。本包聚焦当前板端姿态驱动的上位机，旧 OpenCV/scene-v1 照片工具未纳入。

启动、依赖与离线验证见 [根说明](../README.md)。Godot 工程位于 `godot/`，串口桥接为 `godot_bridge.py`；网页通过适配器直接导入这些模块，均调用 `board_game_host.c` 与板端同一个 `game_session/ray_game`。

只读构建检查：`python -B -m pc_vision.godot_bridge --check`。

协议字段见 [SERIAL_PROTOCOL.md](SERIAL_PROTOCOL.md)。
