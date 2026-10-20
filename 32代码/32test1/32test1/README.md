# STM32 工程 32test1

本目录是 STM32F103C8T6 的 STM32CubeIDE 工程，用于验证 ADC、串口、GPIO 和系统启动配置。

## 主要内容

- `32test1.ioc`：CubeMX 外设配置文件。
- `Core/Inc/`：用户头文件和 HAL 配置，例如 `main.h`、`adc.h`、`usart.h`。
- `Core/Src/`：主要 C 源码，例如 `main.c`、`adc.c`、`usart.c`、中断和系统初始化代码。
- `Core/Startup/`：STM32F103C8T6 启动汇编文件。
- `Drivers/`：CMSIS 与 STM32F1 HAL 驱动库。
- `STM32F103C8TX_FLASH.ld`：Flash 链接脚本。
- `Debug/`：IDE 生成的编译输出。

## 使用方式

使用 STM32CubeIDE 打开该工程，修改 `.ioc` 或 `Core` 下的用户代码后重新生成/编译。调试和烧录配置在 `.launch` 文件中。
