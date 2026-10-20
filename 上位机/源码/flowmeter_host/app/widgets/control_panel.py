from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QFrame,
    QGridLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from ..network_receiver import ReceiverConfig
from ..settings import (
    DEFAULT_DISPLAY_WINDOW_SECONDS,
    DEFAULT_NETWORK_IP,
    DEFAULT_NETWORK_MODE,
    DEFAULT_NETWORK_PORT,
    DEFAULT_SAMPLE_RATE_HZ,
)


class ControlPanel(QWidget):
    connect_requested = Signal(object)
    disconnect_requested = Signal()
    sample_rate_changed = Signal(int)
    display_window_changed = Signal(float)
    auto_scroll_changed = Signal(bool)
    pause_changed = Signal(bool)
    clear_requested = Signal()
    latest_requested = Signal()
    matlab_file_selected = Signal(str)
    algorithm_backend_changed = Signal(str)
    record_start_requested = Signal(float)
    record_stop_requested = Signal()

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.mode_combo = QComboBox()
        self.mode_combo.addItems(["UDP", "TCP Client", "TCP Server"])
        self.mode_combo.setCurrentText(DEFAULT_NETWORK_MODE)
        self.ip_edit = QLineEdit(DEFAULT_NETWORK_IP)
        self.port_spin = QSpinBox()
        self.port_spin.setRange(1, 65535)
        self.port_spin.setValue(DEFAULT_NETWORK_PORT)
        self.connect_button = QPushButton("连接")
        self.disconnect_button = QPushButton("断开")
        self.disconnect_button.setEnabled(False)

        self.sample_rate_combo = QComboBox()
        self.sample_rate_combo.addItems(["100", "200", "500", "1000", "5000", "50000"])
        self.sample_rate_combo.setCurrentText(str(DEFAULT_SAMPLE_RATE_HZ))
        self.sample_rate_combo.setEditable(True)

        self.window_spin = QDoubleSpinBox()
        self.window_spin.setRange(0.02, 300.0)
        self.window_spin.setDecimals(3)
        self.window_spin.setSingleStep(0.1)
        self.window_spin.setValue(float(DEFAULT_DISPLAY_WINDOW_SECONDS))
        self.window_spin.setSuffix(" 秒")

        self.auto_scroll_check = QCheckBox("自动滚动")
        self.auto_scroll_check.setChecked(True)
        self.pause_button = QPushButton("暂停显示")
        self.pause_button.setCheckable(True)
        self.clear_button = QPushButton("清空数据")
        self.latest_button = QPushButton("回到最新")

        self.algorithm_backend_combo = QComboBox()
        self.algorithm_backend_combo.addItems(["Python", "MATLAB"])
        self.algorithm_backend_combo.setCurrentText("Python")
        self.matlab_button = QPushButton("选择 MATLAB 算法")
        self.matlab_path_label = QLabel("未选择")
        self.matlab_path_label.setWordWrap(True)

        self.record_duration = QDoubleSpinBox()
        self.record_duration.setRange(0.0, 24 * 3600.0)
        self.record_duration.setDecimals(1)
        self.record_duration.setSingleStep(1.0)
        self.record_duration.setValue(60.0)
        self.record_duration.setSuffix(" 秒")
        self.record_start_button = QPushButton("开始保存")
        self.record_stop_button = QPushButton("结束保存")
        self.record_stop_button.setEnabled(False)
        self.record_status_label = QLabel("未保存")

        form = QFormLayout()
        form.addRow("协议", self.mode_combo)
        form.addRow("IP", self.ip_edit)
        form.addRow("端口", self.port_spin)
        form.addRow("采样率 Hz", self.sample_rate_combo)
        form.addRow("显示长度", self.window_spin)

        buttons = QGridLayout()
        buttons.addWidget(self.connect_button, 0, 0)
        buttons.addWidget(self.disconnect_button, 0, 1)
        buttons.addWidget(self.pause_button, 1, 0)
        buttons.addWidget(self.clear_button, 1, 1)
        buttons.addWidget(self.latest_button, 2, 0, 1, 2)

        algorithm_form = QFormLayout()
        algorithm_form.addRow("算法后端", self.algorithm_backend_combo)
        algorithm_form.addRow("", self.matlab_button)
        algorithm_form.addRow("MATLAB 文件", self.matlab_path_label)

        record_form = QFormLayout()
        record_form.addRow("保存时长", self.record_duration)
        record_buttons = QGridLayout()
        record_buttons.addWidget(self.record_start_button, 0, 0)
        record_buttons.addWidget(self.record_stop_button, 0, 1)
        record_form.addRow("", record_buttons)
        record_form.addRow("状态", self.record_status_label)

        layout = QVBoxLayout(self)
        title = QLabel("流量计上位机")
        title.setObjectName("PanelTitle")
        layout.addWidget(title)
        layout.addWidget(self._section("连接参数"))
        layout.addLayout(form)
        layout.addWidget(self._section("显示控制"))
        layout.addWidget(self.auto_scroll_check)
        layout.addLayout(buttons)
        layout.addWidget(self._line())
        layout.addWidget(self._section("算法联动"))
        layout.addLayout(algorithm_form)
        layout.addWidget(self._line())
        layout.addWidget(self._section("数据保存"))
        layout.addLayout(record_form)
        layout.addStretch(1)

        self.connect_button.clicked.connect(self._emit_connect)
        self.disconnect_button.clicked.connect(self.disconnect_requested)
        self.sample_rate_combo.currentTextChanged.connect(self._emit_sample_rate)
        self.window_spin.valueChanged.connect(self._emit_window)
        self.auto_scroll_check.toggled.connect(self.auto_scroll_changed)
        self.pause_button.toggled.connect(self._emit_pause)
        self.clear_button.clicked.connect(self.clear_requested)
        self.latest_button.clicked.connect(self.latest_requested)
        self.matlab_button.clicked.connect(self._choose_matlab)
        self.algorithm_backend_combo.currentTextChanged.connect(self.algorithm_backend_changed)
        self.record_start_button.clicked.connect(self._emit_record_start)
        self.record_stop_button.clicked.connect(self.record_stop_requested)

    def set_connected(self, connected: bool) -> None:
        self.connect_button.setEnabled(not connected)
        self.disconnect_button.setEnabled(connected)

    def set_auto_scroll(self, enabled: bool) -> None:
        self.auto_scroll_check.blockSignals(True)
        self.auto_scroll_check.setChecked(enabled)
        self.auto_scroll_check.blockSignals(False)

    def set_recording(self, recording: bool, text: str) -> None:
        self.record_start_button.setEnabled(not recording)
        self.record_stop_button.setEnabled(recording)
        self.record_duration.setEnabled(not recording)
        self.record_status_label.setText(text)

    def _line(self) -> QFrame:
        line = QFrame()
        line.setFrameShape(QFrame.HLine)
        line.setFrameShadow(QFrame.Sunken)
        return line

    def _section(self, text: str) -> QLabel:
        label = QLabel(text)
        label.setObjectName("SectionLabel")
        return label

    def _emit_connect(self) -> None:
        self.connect_requested.emit(
            ReceiverConfig(self.mode_combo.currentText(), self.ip_edit.text().strip(), self.port_spin.value())
        )

    def _emit_sample_rate(self, text: str) -> None:
        try:
            value = int(float(text))
        except ValueError:
            return
        if value > 0:
            self.sample_rate_changed.emit(value)

    def _emit_window(self) -> None:
        self.display_window_changed.emit(float(self.window_spin.value()))

    def _emit_pause(self, paused: bool) -> None:
        self.pause_button.setText("继续显示" if paused else "暂停显示")
        self.pause_changed.emit(paused)

    def _choose_matlab(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "选择 MATLAB 算法文件", "", "MATLAB Files (*.m)")
        if path:
            self.matlab_path_label.setText(path)
            self.matlab_file_selected.emit(path)

    def _emit_record_start(self) -> None:
        self.record_start_requested.emit(float(self.record_duration.value()))
