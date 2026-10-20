# Flowmeter Host

一个类似 VOFA+ 的流量计上位机示例项目，支持网口采集、8 路实时曲线、历史时间轴、CSV 导出，以及 MATLAB 算法后台调用和 MATLAB 图窗嵌入。

## 目录结构

```text
flowmeter_host/
├── main.py
├── requirements.txt
├── README.md
├── app/
│   ├── main_window.py
│   ├── network_receiver.py
│   ├── data_parser.py
│   ├── data_buffer.py
│   ├── matlab_interface.py
│   ├── csv_exporter.py
│   ├── settings.py
│   └── widgets/
│       ├── plot_widget.py
│       └── control_panel.py
├── tests/
│   └── smoke_test.py
└── tools/
    └── mock_sender.py
```

## 安装依赖

```powershell
cd flowmeter_host
python -m venv .venv
.\.venv\Scripts\activate
python -m pip install -r requirements.txt
```

MATLAB Engine 是可选依赖。需要真实调用 MATLAB 时，在 MATLAB 安装目录下执行：

```powershell
cd "MATLAB安装目录\extern\engines\python"
python -m pip install .
```

没有安装 MATLAB Engine 时，软件会自动降级为 Python 模拟流速计算，GUI、采集、显示和 CSV 仍可运行。

## 运行主程序

```powershell
cd flowmeter_host
python main.py
```

默认监听参数建议：

- 协议：UDP
- IP：127.0.0.1
- 端口：9000
- 采样率：按 mock sender 的 `--rate` 设置匹配即可

## 运行模拟发送器

打开另一个终端：

```powershell
cd flowmeter_host
python tools\mock_sender.py --mode udp --ip 127.0.0.1 --port 9000 --rate 200
```

TCP Server 测试方式：上位机选择 `TCP Server` 并点击连接，然后运行：

```powershell
python tools\mock_sender.py --mode tcp-client --ip 127.0.0.1 --port 9000 --rate 200
```

TCP Client 测试方式：先运行 mock TCP server，然后上位机选择 `TCP Client` 连接：

```powershell
python tools\mock_sender.py --mode tcp-server --ip 127.0.0.1 --port 9000 --rate 200
```

## 数据格式

每一行一帧文本数据：

```text
timestamp,ch1,ch2,ch3,ch4,ch5,ch6,ch7,ch8
```

示例：

```text
123.456,1.1,2.2,3.3,4.4,5.5,6.6,7.7,8.8
```

解析逻辑在 `app/data_parser.py`，后续要改成二进制协议时只需要替换该模块。

## MATLAB 算法

界面中点击“选择 MATLAB 算法”选择 `.m` 文件。当前已兼容你提供的 `ana96.m` 这类文件：即使文件名是 `ana96.m`、函数名是 `ana95(DATA)`，Python 侧会复制到临时目录并按函数名调用。

如果工作区根目录存在 `ana96.m`，程序启动时会自动载入它；开始采集后，后台算法线程会定时把最近数据送入 MATLAB。算法弹出的 MUSIC 波数频率谱和流速折线图窗口会被尝试嵌入到主界面下方的固定区域。

当前调用约定：

```matlab
function u_opt = ana95(DATA)
% DATA 为 8 x N 矩阵
% u_opt 为当前流速标量
end
```

原需求里的格式也容易扩展：

```matlab
function flow_velocity = calc_flow(data)
% data 为 N x 8 矩阵
% flow_velocity 为 N x 1 向量
end
```

如需切换到 `calc_flow(N x 8)`，修改 `app/matlab_interface.py` 中 `_calculate()` 的转置和返回值处理即可。

## MATLAB 图窗嵌入

算法第一次运行时会弹出 MATLAB Figure。上位机会在 Windows 下按窗口标题查找包含 `ANA95`、`实时流速` 或 `MUSIC` 的窗口，并尝试嵌入主界面下方固定区域。

如果嵌入失败，算法窗口会保持独立显示，主程序不会崩溃。

## CSV 导出

CSV 列：

```text
timestamp,ch1,ch2,ch3,ch4,ch5,ch6,ch7,ch8,flow_velocity
```

可以选择保存当前可视范围，也可以手动输入开始/结束秒数。尚未计算出流速的点会留空。

## 常见问题

- GUI 没数据：确认协议、IP、端口和 mock sender 一致。
- MATLAB 算法不运行：确认 MATLAB Engine 已安装，或查看日志中的降级提示。
- `ana96.m` 调用失败：确认 MATLAB 当前许可证和 Signal Processing Toolbox 等算法依赖可用。
- 高采样率卡顿：增大算法调用间隔或降低 mock sender 速率；曲线刷新已固定为约 30 FPS，网络接收和 GUI 已分线程。
- 真实下位机采样率命令：`app/network_receiver.py` 已预留 `set_sample_rate(rate_hz)`，当前发送文本命令 `SET_SAMPLE_RATE,<rate>`，需要按真实协议替换。

## 测试

基础 smoke test：

```powershell
cd flowmeter_host
python tests\smoke_test.py
```

GUI 初始化 smoke test：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
python -c "import sys; from PySide6.QtCore import QTimer; from PySide6.QtWidgets import QApplication; from app.theme import APP_STYLE; from app.main_window import MainWindow; app=QApplication(sys.argv); app.setStyleSheet(APP_STYLE); w=MainWindow(); QTimer.singleShot(300, app.quit); app.exec(); w.close(); print('gui init smoke passed')"
```

UDP 接收链路 smoke test：

```powershell
python -c "import queue, socket, time; from app.network_receiver import NetworkReceiver, ReceiverConfig; from app.data_parser import DataParser; q=queue.Queue(); logs=[]; r=NetworkReceiver(q, logs.append); r.start(ReceiverConfig('UDP','127.0.0.1',19000)); time.sleep(0.2); s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); [s.sendto(f'{i/10:.3f},1,2,3,4,5,6,7,8\n'.encode(),('127.0.0.1',19000)) for i in range(5)]; time.sleep(0.5); r.stop(); p=DataParser(); count=0; import queue as qq
while True:
    try: count += len(p.parse_bytes(q.get_nowait()))
    except qq.Empty: break
assert count == 5, count
print('udp receive smoke passed:', count, 'samples')"
```
