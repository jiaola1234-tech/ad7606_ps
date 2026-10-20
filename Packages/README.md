# Packages 目录说明

本目录保存嵌入式开发工具的包仓库配置，当前包含 `.repos.xml`。

## 主要内容

- `.repos.xml`：列出 CMSIS Pack 仓库来源，包括 Keil Pack 索引和 GNU ARM Eclipse/XCDL 包索引。

## 用途

STM32CubeIDE、Eclipse 或相关嵌入式工具在管理芯片支持包、CMSIS 包和外设库时可能读取这些仓库配置。它不是项目业务源码，通常只在配置开发环境或恢复工作区时使用。
