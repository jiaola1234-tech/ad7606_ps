# tools 目录说明

本目录保存上位机开发和联调辅助工具。

## 主要内容

- `mock_sender.py`：模拟下位机发送采样数据，支持 UDP、TCP client、TCP server 等模式。

## 使用方式

在 `flowmeter_host` 目录下运行模拟 UDP 数据：

```powershell
python tools\mock_sender.py --mode udp --ip 127.0.0.1 --port 9000 --rate 200
```

它主要用于没有真实下位机时验证上位机接收、解析、曲线刷新和 CSV 导出。
