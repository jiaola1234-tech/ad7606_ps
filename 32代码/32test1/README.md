# 32test1 工作区说明

本目录是 STM32CubeIDE/Eclipse 工作区，里面放了两个 STM32F103C8T6 工程和 IDE 元数据。

## 主要内容

- `32test1/`：较完整的 STM32F103C8T6 测试工程，包含 ADC、USART、GPIO、启动文件和 HAL 驱动。
- `chaunkou/`：串口测试工程，适合单独验证串口收发、时钟和基础外设初始化。
- `.metadata/`：IDE 工作区配置、索引、日志和历史记录。

## 维护建议

开发时优先修改具体工程目录下的 `Core/Src`、`Core/Inc` 和 `.ioc` 配置文件。不要直接修改 `.metadata` 中的缓存和历史文件。
