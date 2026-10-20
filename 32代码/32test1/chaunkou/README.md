# STM32 串口测试工程 chaunkou

本目录是 STM32F103C8T6 的串口测试工程，用于验证 UART、GPIO、时钟、中断和基础 HAL 初始化流程。

## 主要内容

- `chaunkou.ioc`：CubeMX 工程配置。
- `Core/Inc/`：工程头文件和 HAL 配置。
- `Core/Src/`：主程序、MSP 初始化、中断处理和系统初始化源码。
- `Core/Startup/`：芯片启动文件。
- `Drivers/`：CMSIS 与 STM32F1 HAL 驱动库。
- `STM32F103C8TX_FLASH.ld`：Flash 链接脚本。
- `Debug/`：编译输出目录。

## 使用方式

用 STM32CubeIDE 打开工程后，重点查看 `Core/Src/main.c` 中的串口初始化和收发逻辑。该工程适合在连接 XCOM 等串口助手时做基础通信验证。
