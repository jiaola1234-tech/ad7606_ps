# 上位机独立运行版目录说明

本目录保存已经打包好的流量计上位机程序，面向不安装 Python 的 Windows 电脑直接运行。

## 主要内容

- `SonarFlowMeterHost.exe`：打包后的独立可执行程序。
- `ana96.m`：随包保留的 MATLAB 算法源文件，可查看或手动选择替换。
- `SHA256.txt`：EXE 的 SHA256 完整性校验值。
- `使用说明.txt`：原有独立运行版使用说明。

## 使用方式

双击 `SonarFlowMeterHost.exe` 运行。界面默认选择 MATLAB 后端，目标电脑安装并授权 MATLAB 后会显示 MATLAB 算法图窗；选择 Python 后端，或 MATLAB 不可用时自动回退 Python，右侧仍会显示内置的 MUSIC 谱、速度能量积分和流速历史图。

如果旧快捷方式仍提示 `DLL load failed while importing QtCore`，请使用目录 `SonarFlowMeterHost_fixed_20261023` 中的 `启动上位机.bat`。这是带 Qt DLL 搜索路径的修复版，不要继续打开旧桌面快捷方式。

算法图需要先累计约 2 秒数据；数据不足时右侧图区域会保持空白，达到分析长度后每约 2 秒刷新一次。

连接板端时选择 `TCP Client`，默认地址为 `192.168.1.10:5001`。板端启动或网口链路稍慢时，上位机会每 1 秒自动重试；运行中断线也会自动重连。
