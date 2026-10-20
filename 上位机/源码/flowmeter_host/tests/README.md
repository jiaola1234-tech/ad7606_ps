# tests 目录说明

本目录保存上位机工程的测试代码。

## 主要内容

- `smoke_test.py`：基础冒烟测试，用于确认核心模块可以导入、解析和基本运行。

## 使用方式

在 `flowmeter_host` 目录下运行：

```powershell
python tests\smoke_test.py
```

测试通过只代表基础链路正常，不等同于真实硬件通信和 MATLAB 环境全部可用。
