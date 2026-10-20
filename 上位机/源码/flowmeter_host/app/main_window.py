from __future__ import annotations

import queue
import sys
import time
from pathlib import Path

import numpy as np
from PySide6.QtCore import QTimer, Qt, Signal
from PySide6.QtGui import QCloseEvent
from PySide6.QtWidgets import (
    QApplication,
    QDockWidget,
    QFileDialog,
    QFrame,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPlainTextEdit,
    QSlider,
    QSplitter,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)

from .csv_exporter import export_snapshot
from .data_buffer import DataBuffer
from .data_parser import DataParser
from .matlab_interface import MatlabAlgorithmWorker
from .network_receiver import NetworkReceiver, ReceiverConfig
from .settings import AppConfig, PLOT_REFRESH_MS, QUEUE_DRAIN_LIMIT
from .theme import APP_STYLE
from .widgets.control_panel import ControlPanel
from .widgets.plot_widget import PlotWidget


class MainWindow(QMainWindow):
    log_signal = Signal(str)
    matlab_result_signal = Signal(float, object)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("Flowmeter Host - VOFA+ Style")
        self.resize(1800, 1000)

        self.config = AppConfig()
        self.raw_queue: queue.Queue[bytes] = queue.Queue()
        self.parser = DataParser()
        self.buffer = DataBuffer(self.config.capacity)
        self.receiver = NetworkReceiver(self.raw_queue, self._thread_log)
        self.matlab = MatlabAlgorithmWorker(self._thread_log, self._thread_matlab_result)

        self.paused = False
        self.auto_scroll = True
        self.display_window_seconds = float(self.config.display_window_seconds)
        self.visible_start = 0.0
        self.visible_end = self.display_window_seconds
        self.latest_velocity: float | None = None
        self._last_matlab_submit = 0.0
        self._embedded_hwnd: int | None = None
        self._recording = False
        self._record_duration_seconds = 0.0
        self._record_start_timestamp: float | None = None
        self._record_path: str | None = None
        self._record_timestamps: list[np.ndarray] = []
        self._record_channels: list[np.ndarray] = []
        self._record_flow_velocity: list[np.ndarray] = []

        self.control = ControlPanel()
        self.plot = PlotWidget()
        self.log_view = QPlainTextEdit()
        self.log_view.setReadOnly(True)
        self.timeline = QSlider(Qt.Horizontal)
        self.timeline.setRange(0, 1000)
        self.timeline.setValue(1000)
        self.range_label = QLabel("可视范围: --")
        self.matlab_frame = QFrame()
        self.matlab_frame.setObjectName("MatlabFrame")
        self.matlab_frame.setFrameShape(QFrame.StyledPanel)
        self.matlab_frame.setMinimumWidth(700)
        self.matlab_frame.setLayout(QVBoxLayout())
        self.matlab_hint = QLabel("MATLAB 算法窗口将在这里嵌入")
        self.matlab_hint.setAlignment(Qt.AlignCenter)
        self.matlab_frame.layout().addWidget(self.matlab_hint)

        self._build_layout()
        self._connect_signals()
        self._load_default_algorithm()
        self._refresh_plot()

        self.timer = QTimer(self)
        self.timer.setInterval(PLOT_REFRESH_MS)
        self.timer.timeout.connect(self._tick)
        self.timer.start()
        self._log("程序已启动")

    def _build_layout(self) -> None:
        left = QDockWidget("控制", self)
        left.setWidget(self.control)
        left.setMinimumWidth(280)
        self.addDockWidget(Qt.LeftDockWidgetArea, left)

        center = QWidget()
        center_layout = QVBoxLayout(center)
        splitter = QSplitter(Qt.Horizontal)
        splitter.addWidget(self.plot)
        splitter.addWidget(self.matlab_frame)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([760, 820])
        center_layout.addWidget(splitter, 1)

        timeline_row = QHBoxLayout()
        timeline_row.addWidget(QLabel("时间轴"))
        timeline_row.addWidget(self.timeline, 1)
        timeline_row.addWidget(self.range_label)
        center_layout.addLayout(timeline_row)
        self.setCentralWidget(center)

        bottom = QDockWidget("日志", self)
        bottom.setWidget(self.log_view)
        bottom.setMinimumHeight(140)
        self.addDockWidget(Qt.BottomDockWidgetArea, bottom)

        self.status = QStatusBar()
        self.setStatusBar(self.status)
        self.connection_status = QLabel("未连接")
        self.sample_status = QLabel(f"采样率 {self.config.sample_rate_hz} Hz")
        self.count_status = QLabel("点数 0")
        self.velocity_status = QLabel("流速 --")
        for widget in (self.connection_status, self.sample_status, self.count_status, self.velocity_status):
            self.status.addPermanentWidget(widget)

    def _connect_signals(self) -> None:
        self.control.connect_requested.connect(self._connect_network)
        self.control.disconnect_requested.connect(self._disconnect_network)
        self.control.sample_rate_changed.connect(self._set_sample_rate)
        self.control.display_window_changed.connect(self._set_display_window)
        self.control.auto_scroll_changed.connect(self._set_auto_scroll)
        self.control.pause_changed.connect(self._set_paused)
        self.control.clear_requested.connect(self._clear_data)
        self.control.latest_requested.connect(self._go_latest)
        self.control.matlab_file_selected.connect(self._load_matlab)
        self.control.algorithm_backend_changed.connect(self.matlab.set_backend)
        self.control.record_start_requested.connect(self._start_recording)
        self.control.record_stop_requested.connect(self._stop_recording)
        self.timeline.sliderPressed.connect(self._timeline_pressed)
        self.timeline.valueChanged.connect(self._timeline_changed)
        self.log_signal.connect(self._log)
        self.matlab_result_signal.connect(self._on_matlab_result)

    def _load_default_algorithm(self) -> None:
        default_candidates = [
            Path(__file__).resolve().parents[2] / "ana96.m",
            Path(__file__).resolve().parents[1] / "ana96.m",
        ]
        for candidate in default_candidates:
            if candidate.exists():
                try:
                    self.matlab.load_algorithm(candidate)
                    self.control.matlab_path_label.setText(str(candidate))
                    self._log(f"已自动载入默认算法: {candidate.name}")
                except Exception as exc:
                    self._log(f"默认算法载入失败: {exc}")
                return

    def _thread_log(self, message: str) -> None:
        self.log_signal.emit(message)

    def _thread_matlab_result(self, velocity: float, hwnd: int | None) -> None:
        self.matlab_result_signal.emit(velocity, hwnd)

    def _connect_network(self, config: ReceiverConfig) -> None:
        if self.matlab.loaded:
            self.matlab.start()
            self._log(f"采集开始，{self.matlab.backend} 算法后台线程已就绪")
        else:
            self._log("未选择 MATLAB 算法文件；Python 算法仍可运行")
        self.receiver.start(config)
        self.control.set_connected(True)
        self.connection_status.setText("连接中")

    def _disconnect_network(self) -> None:
        self.receiver.stop()
        self.control.set_connected(False)
        self.connection_status.setText("未连接")
        self._log("已断开连接")

    def _set_sample_rate(self, rate_hz: int) -> None:
        self.config.sample_rate_hz = rate_hz
        self.parser.set_sample_rate(rate_hz)
        self.buffer.resize(self.config.capacity)
        self.receiver.set_sample_rate(rate_hz)
        self.sample_status.setText(f"采样率 {rate_hz} Hz")
        self._log(f"采样率已设置为 {rate_hz} Hz")

    def _set_display_window(self, seconds: float) -> None:
        self.display_window_seconds = max(0.02, float(seconds))
        self._go_latest()
        self._refresh_plot()
        self._log(f"显示窗口长度已设置为 {self.display_window_seconds:g} 秒")

    def _set_auto_scroll(self, enabled: bool) -> None:
        self.auto_scroll = enabled
        if enabled:
            self._go_latest()

    def _set_paused(self, paused: bool) -> None:
        self.paused = paused
        self._log("显示已暂停" if paused else "显示已继续")

    def _clear_data(self) -> None:
        self.buffer.clear()
        self.plot.update_data(self.buffer.snapshot())
        self._log("当前缓存数据已清空")

    def _go_latest(self) -> None:
        self.auto_scroll = True
        self.control.set_auto_scroll(True)
        latest = self.buffer.latest_time()
        if latest is not None:
            self.visible_end = latest
            self.visible_start = latest - self.display_window_seconds
        else:
            self.visible_start = 0.0
            self.visible_end = self.display_window_seconds
        self.timeline.blockSignals(True)
        self.timeline.setValue(self.timeline.maximum())
        self.timeline.blockSignals(False)

    def _load_matlab(self, path: str) -> None:
        try:
            self.matlab.load_algorithm(path)
        except Exception as exc:
            self._log(f"MATLAB 算法加载失败: {exc}")

    def _start_recording(self, duration_seconds: float) -> None:
        if self._recording:
            return
        path, _ = QFileDialog.getSaveFileName(self, "保存 CSV", "flowmeter_data.csv", "CSV Files (*.csv)")
        if not path:
            return
        self._recording = True
        self._record_duration_seconds = max(0.0, float(duration_seconds))
        self._record_start_timestamp = None
        self._record_path = path
        self._record_timestamps = []
        self._record_channels = []
        self._record_flow_velocity = []
        text = "正在保存: 手动结束" if self._record_duration_seconds <= 0 else f"正在保存: 0.0/{self._record_duration_seconds:g} 秒"
        self.control.set_recording(True, text)
        self._log(f"CSV 保存已开始: {path}")

    def _stop_recording(self) -> None:
        if not self._recording:
            return
        path = self._record_path
        timestamps = np.concatenate(self._record_timestamps) if self._record_timestamps else np.empty(0, dtype=float)
        channels = (
            np.vstack(self._record_channels)
            if self._record_channels
            else np.empty((0, self.buffer.channels.shape[1]), dtype=float)
        )
        flow_velocity = (
            np.concatenate(self._record_flow_velocity)
            if self._record_flow_velocity
            else np.empty(0, dtype=float)
        )
        self._recording = False
        self._record_start_timestamp = None
        self._record_path = None
        self._record_timestamps = []
        self._record_channels = []
        self._record_flow_velocity = []
        self.control.set_recording(False, f"已保存 {timestamps.size} 行")
        if not path:
            return
        try:
            from .data_buffer import BufferSnapshot

            export_snapshot(BufferSnapshot(timestamps, channels, flow_velocity), path)
            self._log(f"CSV 保存完成: {path}，{timestamps.size} 行")
        except Exception as exc:
            self.control.set_recording(False, "保存失败")
            self._log(f"CSV 保存失败: {exc}")

    def _timeline_pressed(self) -> None:
        self.auto_scroll = False
        self.control.set_auto_scroll(False)

    def _timeline_changed(self, value: int) -> None:
        if self.auto_scroll:
            return
        earliest = self.buffer.earliest_time()
        latest = self.buffer.latest_time()
        if earliest is None or latest is None:
            return
        span = max(0.0, latest - earliest - self.display_window_seconds)
        ratio = value / max(1, self.timeline.maximum())
        self.visible_start = earliest + ratio * span
        self.visible_end = self.visible_start + self.display_window_seconds
        self._refresh_plot()

    def _tick(self) -> None:
        self._drain_queue()
        if not self.paused:
            if self.auto_scroll:
                latest = self.buffer.latest_time()
                if latest is not None:
                    self.visible_end = latest
                    self.visible_start = latest - self.display_window_seconds
                    self.timeline.blockSignals(True)
                    self.timeline.setValue(self.timeline.maximum())
                    self.timeline.blockSignals(False)
            self._refresh_plot()
        self._update_status()
        self._maybe_submit_matlab()

    def _drain_queue(self) -> None:
        drained = 0
        while drained < QUEUE_DRAIN_LIMIT:
            try:
                payload = self.raw_queue.get_nowait()
            except queue.Empty:
                break
            drained += 1
            try:
                samples = self.parser.parse_bytes(payload)
                for sample in samples:
                    self.buffer.append(sample.timestamp, sample.values)
                self._append_record_samples(samples)
            except Exception as exc:
                self._log(f"数据解析错误: {exc}")
        if drained and self.connection_status.text() != "已连接":
            self.connection_status.setText("已连接")

    def _append_record_samples(self, samples) -> None:
        if not self._recording or not samples:
            return
        if self._record_start_timestamp is None:
            self._record_start_timestamp = float(samples[0].timestamp)
        timestamps = np.asarray([sample.timestamp for sample in samples], dtype=float)
        channels = np.vstack([sample.values for sample in samples]).astype(float, copy=False)
        elapsed = timestamps - self._record_start_timestamp
        if self._record_duration_seconds > 0:
            keep = elapsed <= self._record_duration_seconds
            if not np.any(keep):
                self._stop_recording()
                return
            timestamps = timestamps[keep]
            channels = channels[keep]
            elapsed = elapsed[keep]

        self._record_timestamps.append(timestamps.copy())
        self._record_channels.append(channels.copy())
        velocity = float(self.latest_velocity) if self.latest_velocity is not None else np.nan
        self._record_flow_velocity.append(np.full(timestamps.size, velocity, dtype=float))

        last_elapsed = float(elapsed[-1])
        if self._record_duration_seconds > 0:
            shown = min(last_elapsed, self._record_duration_seconds)
            self.control.set_recording(True, f"正在保存: {shown:.1f}/{self._record_duration_seconds:g} 秒")
            if last_elapsed >= self._record_duration_seconds:
                self._stop_recording()
        else:
            self.control.set_recording(True, f"正在保存: {last_elapsed:.1f} 秒")

    def _refresh_plot(self) -> None:
        if self.visible_end <= self.visible_start:
            self.visible_start = 0.0
            self.visible_end = self.display_window_seconds
        snapshot = self.buffer.range(self.visible_start, self.visible_end)
        self.plot.update_data(snapshot)
        self.plot.set_x_range(self.visible_start, self.visible_end)
        self.range_label.setText(f"可视范围: {self.visible_start:.3f} - {self.visible_end:.3f} s")

    def _maybe_submit_matlab(self) -> None:
        now = time.monotonic()
        if now - self._last_matlab_submit < 2.0:
            return
        analysis_seconds = 2.0
        snapshot = self.buffer.latest_seconds(analysis_seconds)
        min_samples = max(8, int(self.config.sample_rate_hz * analysis_seconds))
        if snapshot.timestamps.size < min_samples:
            return
        self._last_matlab_submit = now
        self.matlab.start()
        self.matlab.submit(snapshot.channels, self.config.sample_rate_hz)

    def _on_matlab_result(self, velocity: float, hwnd: int | None) -> None:
        self.latest_velocity = velocity
        self.buffer.set_flow_for_latest(velocity, max(1, self.config.sample_rate_hz // 10))
        self.plot.set_velocity(velocity)
        self.velocity_status.setText(f"流速 {velocity:.3f} m/s" if np.isfinite(velocity) else "流速 --")
        if hwnd:
            self._embed_window(hwnd)

    def _embed_window(self, hwnd: int) -> None:
        if sys.platform != "win32" or self._embedded_hwnd == hwnd:
            return
        try:
            import ctypes
            from PySide6.QtGui import QWindow
            from PySide6.QtWidgets import QWidget

            user32 = ctypes.windll.user32
            user32.SetParent(int(hwnd), int(self.matlab_frame.winId()))
            style = user32.GetWindowLongW(int(hwnd), -16)
            style = style & ~0x00C00000  # WS_CAPTION
            user32.SetWindowLongW(int(hwnd), -16, style)
            qwindow = QWindow.fromWinId(int(hwnd))
            container = QWidget.createWindowContainer(qwindow, self.matlab_frame)
            layout = self.matlab_frame.layout()
            while layout.count():
                item = layout.takeAt(0)
                widget = item.widget()
                if widget:
                    widget.setParent(None)
            layout.addWidget(container)
            self._embedded_hwnd = hwnd
            self._log("MATLAB 图窗已嵌入上位机界面")
        except Exception as exc:
            self._log(f"MATLAB 图窗嵌入失败，将保持独立窗口显示: {exc}")

    def _update_status(self) -> None:
        self.count_status.setText(f"点数 {self.buffer.total_received}")

    def _log(self, message: str) -> None:
        self.log_view.appendPlainText(f"[{time.strftime('%H:%M:%S')}] {message}")

    def closeEvent(self, event: QCloseEvent) -> None:
        self.timer.stop()
        if self._recording:
            self._stop_recording()
        self.receiver.stop()
        self.matlab.stop()
        event.accept()


def main() -> None:
    app = QApplication(sys.argv)
    app.setStyleSheet(APP_STYLE)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())
