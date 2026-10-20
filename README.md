# finalversion 总目录说明

这是电磁流量计/声呐流量计项目的最终交付目录，集中保存硬件平台、Vivado/Vitis 工程、STM32 测试代码、上位机软件、屏幕工程和使用说明。

## 目录概览

- `硬件平台/`：独立保留的硬件平台文件。
- `vivado/`：Vivado 工程、IP、Block Design、bitstream 和 XSA 导出脚本。
- `vitis/`：Vitis 工程副本，包含板端裸机应用、平台工程、FSBL/BSP 和系统工程。
- `ws/`：另一套 Vitis/SDK 工作区，当前较完整的板端源码在 `0806_/src/`。
- `32代码/`：STM32F103C8T6 测试工程。
- `上位机/`：PC 上位机源码和打包后的独立运行版。
- `屏幕/`：MCGSPro 触摸屏工程。
- `电磁流量计/`：用户使用说明书 Word 文件。
- `XCOM V2.6/`：串口调试工具。
- `Packages/`：STM32CubeIDE / CMSIS Pack 相关资源。

## 当前约定

- 测量页原始值由板端发送，屏幕侧按 `0.001 * X` 显示。
- 当前生效系数：
  - `4WUB0116`：当前生效 K
  - `4WUB0117`：当前生效 B
- 手动输入系数：
  - `4WUB0160`：手动 K 原始值
  - `4WUB0161`：手动 B 原始值
  - `4WUB0162`：应用命令
- 脉冲参数：
  - `4WUB0150`：脉冲体积，单位 mL
  - `4WUB0151`：脉冲个数
  - `4WUB0152`：脉宽，单位 ms

## 阅读顺序

1. 先看 `电磁流量计/` 下的使用说明。
2. 再看 `上位机/README.md` 了解 PC 软件。
3. 板端开发优先看 `vivado/README.md`、`vitis/README.md` 和 `ws/README.md`。
4. 修改屏幕页面时看 `屏幕/README.md` 和 `MCGSPro_手动定点校准页代码修改说明.md`。

## 备注

- `vitis/` 和 `ws/` 里有较多 IDE 自动生成文件，修改时优先定位到源码或 Tcl 脚本。
- `.metadata`、`Debug`、`.Xil`、`.run_venv` 之类目录通常是工具生成物，不建议手工编辑。

## Git 仓库说明

本目录已纳入 Git 版本管理（`main` 分支）。仓库只跟踪**可读的源文件与必要的硬件交付物**，具体排除规则见根目录 `.gitignore`。

**被排除的内容（原始文件仍保留在本机，仅不进 Git）**：

| 类别 | 说明 |
| --- | --- |
| Python 虚拟环境 | `.pack_venv/`、`.run_venv/` 等，可由 `requirements.txt` 重建 |
| 打包产物 | PyInstaller 的 `build/`、`dist/`、`*.pkg` |
| Vivado 自动生成目录 | `.cache/`、`.gen/`、`.hw/`、`.runs/`、`.sim/`、`.tmp/`、`*.dcp` |
| Vitis/Eclipse 元数据 | `.metadata/`、`RemoteSystemsTempFiles/`、`*.launch` |
| 压缩备份包 | `*.rar`、`*.zip`、`*.7z` |
| 编辑器/系统文件 | `.vs/`、`.vscode/`、`Thumbs.db` 等 |

**保留在 Git 中的硬件交付物**：Vivado 工程文件（`*.xpr`）、XSA 硬件平台导出、bitstream、`xdc` 引脚约束、Tcl 生成/导出脚本、板端 C 源码、上位机 Python 源码与打包后独立运行版。

> 说明：完整备份包（`上位机.rar` 等）与两份 Python 虚拟环境因单文件超过 GitHub 100 MB 硬限制而未纳入，需归档请另行走网盘或Release 附件。
