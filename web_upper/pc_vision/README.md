# 网页上位机

当前 build=`20261029-web-recovery-quit-5`，version14 / wire-v3。启动脚本和源码保留当前工作区行为；目录从原 `pc_vision(1)/pc_vision` 整理为 `web_upper/pc_vision`。

`shared_runtime.py` 向上查找根 `pc_vision` 和共用 C 源，不使用另一份配置或求解器。Three.js 0.180.0 实际用到的模块及 LICENSE 放在 `web/vendor/three`；后台原 `/node_modules/three/` URL 映射到此目录，浏览器导入地址和业务逻辑不变。无需复制 node_modules 或运行 npm 安装。

在根目录用 `start_game.ps1 -PythonPath <虚拟环境Python> -SerialPort COM9` 启动；完整命令、-Preview/-Check 和测试见 [根说明](../../README.md)。运行中完整场景保持、原时序、350ms准入、错误返回菜单和结束程序均保留。
