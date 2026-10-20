# vivado 目录说明

本目录是流量计项目的 Vivado 硬件工程，用于生成 Zynq-7000 硬件平台、bitstream 和供 Vitis 使用的 XSA 文件。

## 主要内容

- `ad7606_lwip.xpr`：Vivado 工程文件。
- `ad7606_lwip.srcs/`：Block Design、约束和工程源文件。
- `repo/`：本工程使用的自定义 IP 仓库，包含 AD7606 采样 IP、DVI/时钟等 IP。
- `vitis hls/`：HLS 导出的 `real_converter` IP 相关文件。
- `build_outputs/`：便于查找的 bitstream 输出副本。
- `existing_exports/`：历史导出的 XSA 文件，仅供对照。
- `0806.xsa`：当前目录下的一份硬件平台导出文件。
- `check_project.tcl`：工程/IP/Block Design 检查脚本。
- `regenerate_and_export_xsa.tcl`：重新综合、实现、生成 bitstream 并导出 XSA 的脚本。
- `README_硬件平台生成说明.md`：更详细的硬件平台生成说明。

## 使用方式

使用 Vivado 2022.2 打开 `ad7606_lwip.xpr`。如需重新给 Vitis 导出硬件平台，可在 Vivado Tcl Shell 中运行 `regenerate_and_export_xsa.tcl`，或按 `README_硬件平台生成说明.md` 中的步骤手动操作。

## 注意事项

`.Xil`、`ad7606_lwip.cache`、`ad7606_lwip.gen`、`ad7606_lwip.runs`、`ad7606_lwip.hw`、`ad7606_lwip.sim` 多数是 Vivado 自动生成的缓存、仿真、综合和实现输出。日常修改优先看 `ad7606_lwip.srcs`、`repo` 和 Tcl 脚本。
