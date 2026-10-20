# 上位机源码目录说明

本目录保存 PC 上位机的可维护源码和 MATLAB 算法文件。

## 主要内容

- `ana96.m`：MATLAB 流速算法源文件。
- `flowmeter_host/`：Python/PySide6 上位机源码工程。
- `源码说明.txt`：原有源码目录说明，保留给纯文本阅读场景。

## 使用方式

在当前电脑可进入 `flowmeter_host` 后双击 `运行源码.bat`，或在命令行中运行 `python main.py`。在其他电脑上建议重新创建 Python 虚拟环境并安装 `requirements.txt`。

## 注意事项

`flowmeter_host/.run_venv` 是本机即用环境，路径可能绑定当前电脑，不保证复制到其他电脑后还能直接运行。跨电脑部署优先使用打包后的 EXE 或重新建环境。
