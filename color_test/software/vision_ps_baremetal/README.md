# 当前 Standalone 板端与共用 C 核心

当前主入口：`src/standalone/game_beam_standalone.c`。两lane、1280×720 RAW10/BGGR，Cortex-A9/core0 Standalone。五类真实物块，逻辑光源来自上位机关卡命令；二进制v3，诊断版本14。B支持间隔350ms、恒亮显示保持、原始采集标记及安全三缓冲/退休/HALTED规则保留。

目录中 `game_linux_capture/display` 是当前 Standalone 实际复用的 DMA 所有者实现，不是另一个需要部署的 Linux 工程。

## 移动后的应用构建

从仓库根目录，使用 Vitis2023.1 自带 Cortex-A9 hard-float 工具链：

```powershell
python -B tools/build_board.py --check
python -B tools/build_board.py --vitis-root 'D:/Xilinx/Vitis/2023.1' --tag my_board_1 --boot
```

新输出为 `build/board/my_board_1/`，已存在则拒绝覆盖。默认复用 `firmware/version14/inputs/` 的冻结 XSA/BIT/FSBL/BSP 和已编译 beam 配置，沿原构建脚本的19个编译单元与参数编译同一源码；不重新生成 PL/平台，也不连接设备。后续明确调整 JSON 时可加 `--regenerate-config`，调用同一个 `generate_game_config.py` 生成 beam C。PC 准入350ms是独立参数，冻结 beam 配置不是第二份手工配置。

原 `tools/build_game_beam.tcl`、`prepare_game_beam.py` 保留为构建来源参考，它们的旧 `-baseline live` 依赖历史工作区；本精简包请使用根 `tools/build_board.py`。其 `--check` 复用原 XSA、ELF、BOOT分区核对函数，直接核对随包冻结文件，避免复制整套历史平台。

本次整理只验证搬移后的离线校验与主机 C 检查，没有重新运行 ARM 编译。BOOT 分区证据 `firmware/version14/output/verification.json` 来自原版构建，含原工作区路径，保留证据原文。新增构建入口只在明确执行上述命令时运行编译；不下载、不复位、不写 SD。
