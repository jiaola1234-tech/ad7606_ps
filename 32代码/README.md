# 32代码 目录说明

本目录保存 STM32 相关工程，当前主要是基于 STM32F103C8T6 的测试代码，用于验证 MCU 侧串口、ADC、GPIO、启动文件和 HAL 驱动配置。

## 主要内容

- `32test1/`：STM32CubeIDE 工作区，包含两个工程：
  - `32test1/32test1/`：带 ADC、USART、GPIO 等配置的测试工程。
  - `32test1/chaunkou/`：串口相关测试工程。
  - `32test1/.metadata/`：STM32CubeIDE/Eclipse 工作区元数据和历史记录，不是业务源码。

## 使用方式

用 STM32CubeIDE 打开 `32test1` 工作区后，可以导入或直接打开其中的 `32test1`、`chaunkou` 工程。实际代码主要在各工程的 `Core/Src`、`Core/Inc` 和 `Drivers` 目录中。

## 注意事项

- `Debug/` 目录是编译输出目录，可用于查看生成的目标文件和 ELF，但不要把它当成源码修改入口。
- `.metadata/` 目录由 IDE 维护，除非修复工作区配置问题，否则不建议手动编辑。
