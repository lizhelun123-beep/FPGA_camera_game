# 当前板端硬件规范源

包含一套当前 BD、顶层 wrapper、XDC、实际顶层 RTL 和本地IP仓库；不保留生成的IP副本、DCP、旧实验工程或 Vivado 运行缓存。

当前运行固件使用 `../firmware/version14/inputs/hardware.xsa` 内冻结的历史 live 硬件，XSA SHA256 `03ae58ac8483359101e5286931effb0d84544f4b9b6c215572ff88653011e0c8`，BIT SHA256 `a5a88800af559c2e95ad67453deab7d4ec5bdfeaa3eccddc7279f0f2f64f3e55`。当前可编辑 MIPI/显示源含后续接口/诊断改动，不宣称从当前源重建会得到同一 BIT 或已通过时序；当前采样路线的历史停用结论没有被本次打包重新准入。

`open_project.tcl` 仅用相对路径在 `build/source_project/` 建立/打开可编辑工程（Vivado2023.1，xc7z020clg400-2），未在本次执行，不运行综合/实现或硬件操作。它不带原 XPR 中指向多轮历史诊断输出的约束/DCP引用，不用于复现这些诊断结果。不要为运行现有版本14而重建 PL；应用构建优先使用随包冻结平台。

在 Vivado GUI Tcl 控制台可以明确运行：`source ./color_test/open_project.tcl`。本次仅保存工程文件，没有调用 Vivado。
