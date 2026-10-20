# Vivado 硬件平台工程

本目录是从原工程复制出的可移植副本，原工程未被修改。

## 工程信息

- Vivado 版本：2022.2
- 工程文件：`ad7606_lwip.xpr`
- FPGA 器件：`xc7z020clg400-2`（Zynq-7000，ARM Cortex-A9）
- Block Design：`design_1`
- 自定义 AD7606 IP：`repo/ad7606_sample_1.0`
- 当前设计已启用 TTC0，接口方式为 EMIO。

## 目录内容

- `ad7606_lwip.srcs`：Block Design、约束等工程源码。
- `ad7606_lwip.gen`：Block Design wrapper 和各 IP 的生成文件。
- `repo`：工程所需的自定义 IP 仓库，包含 AD7606 采样 IP。
- `vitis hls`：原工程中的本地 HLS IP 仓库。
- `ad7606_lwip.runs/impl_1/design_1_wrapper.bit`：当前实现生成的 bitstream。
- `build_outputs`：当前 bitstream 的便于查找副本。
- `existing_exports`：历史 XSA，仅供对照；其中旧 XSA 不一定包含当前 TTC0 配置。
- `check_project.tcl`：检查工程、IP 和 Block Design。
- `regenerate_and_export_xsa.tcl`：重新综合、实现、生成 bitstream 并导出 XSA。

## 在 Vivado 中打开

1. 使用 Vivado 2022.2 打开 `ad7606_lwip.xpr`。
2. 在 Sources 中打开 `design_1.bd`。
3. 如 Vivado 提示生成输出文件，选择 Generate Output Products。
4. 依次运行 Synthesis、Implementation、Generate Bitstream。
5. 选择 File > Export > Export Hardware，勾选 Include bitstream，导出新的 XSA。

也可以在 Vivado Tcl Shell 中执行：

```tcl
cd C:/Users/DELL/Desktop/finalversion/vivado
source regenerate_and_export_xsa.tcl
```

生成的新平台位于 `hardware_platform/ad7606_lwip_ttc.xsa`。该脚本会重新运行综合和实现，耗时取决于电脑性能。

## TTC0 注意事项

当前 `design_1.bd` 中：

- `PCW_TTC0_PERIPHERAL_ENABLE = 1`
- `PCW_TTC0_TTC0_IO = EMIO`

因此后续创建 Vitis ARM 平台时，应使用本目录重新导出的 XSA，不要把 `existing_exports` 中的旧 XSA 当作最终平台。

## 增益调理板模拟 CAN 引脚

当前主板应用没有使用 Zynq PS 的 CAN0/CAN1 外设；增益切档采用 GPIO 模拟协议。PS MIO9 被 `vitis/0804_1_/src/can.c` 配置为输出，连接到增益小板 STM32 的 PA11 接收输入。该链路使用 50 us/bit 的单线波形，帧格式为 `A5 20 A3 level ~level CRC8`，不是标准 CAN 帧。

重新导出硬件平台时应保留 MIO9 为可用 GPIO，并确认没有把 MIO9 分配给其他外设。修改模拟 CAN 引脚或时序时，需要同时更新 Zynq 主板的 `can.c` 和 STM32 接收端的 `main.c`。
