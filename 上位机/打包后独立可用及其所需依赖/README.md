# 上位机独立运行版目录说明

本目录保存已经打包好的流量计上位机程序，面向不安装 Python 的 Windows 电脑直接运行。

## 主要内容

- `SonarFlowMeterHost.exe`：打包后的独立可执行程序。
- `ana96.m`：随包保留的 MATLAB 算法源文件，可查看或手动选择替换。
- `SHA256.txt`：EXE 的 SHA256 完整性校验值。
- `使用说明.txt`：原有独立运行版使用说明。

## 使用方式

双击 `SonarFlowMeterHost.exe` 运行。软件可以在没有 MATLAB 的情况下使用 Python 后端完成采集、显示和 CSV 导出；若要调用真实 MATLAB 算法和图窗，目标电脑需要安装并授权 MATLAB。
