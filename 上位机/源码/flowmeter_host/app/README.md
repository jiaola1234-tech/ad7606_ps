# app 目录说明

本目录是上位机程序的主要业务代码包，负责 GUI、网络接收、数据解析、缓存、算法调用和导出。

## 模块说明

- `main_window.py`：主窗口和主要交互流程。
- `network_receiver.py`：UDP/TCP 数据接收线程。
- `data_parser.py`：把接收到的文本数据解析成采样帧。
- `data_buffer.py`：保存实时数据和历史时间轴。
- `matlab_interface.py`：MATLAB Engine 调用、算法降级和 MATLAB 图窗嵌入逻辑。
- `csv_exporter.py`：CSV 导出。
- `ip_configurator.py`：本机 IP 配置辅助功能。
- `settings.py`：默认配置。
- `theme.py`：界面样式。
- `widgets/`：主界面中复用的曲线、控制和分析面板。

## 修改入口

改界面布局优先看 `main_window.py` 和 `widgets/`；改通信协议优先看 `network_receiver.py` 与 `data_parser.py`；改算法调用优先看 `matlab_interface.py`。
