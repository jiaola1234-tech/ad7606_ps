from __future__ import annotations

import numpy as np
import pyqtgraph as pg
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QHBoxLayout, QLabel, QVBoxLayout, QWidget

from ..data_buffer import BufferSnapshot
from ..settings import CHANNEL_COUNT


MAX_PLOT_POINTS = 5000


class PlotWidget(QWidget):
    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.colors = ["#d62728", "#1f77b4", "#2ca02c", "#ff7f0e", "#9467bd", "#17becf", "#8c564b", "#7f7f7f"]

        self.velocity_label = QLabel("最新流速 -- m/s")
        self.velocity_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self.velocity_label.setObjectName("VelocityLabel")

        header = QHBoxLayout()
        header.addWidget(QLabel("八路实时信号"))
        header.addStretch(1)
        header.addWidget(self.velocity_label)

        self.grid = pg.GraphicsLayoutWidget()
        self.grid.setBackground("w")
        self.plots: list[pg.PlotItem] = []
        self.curves: list[pg.PlotDataItem] = []
        for idx in range(CHANNEL_COUNT):
            row = idx // 2
            col = idx % 2
            plot = self.grid.addPlot(row=row, col=col, title=f"CH{idx + 1}")
            plot.showGrid(x=True, y=True, alpha=0.22)
            plot.setMenuEnabled(False)
            plot.setMouseEnabled(x=False, y=False)
            plot.setLabel("left", "V")
            if row == 3:
                plot.setLabel("bottom", "Time", units="s")
            else:
                plot.getAxis("bottom").setStyle(showValues=False)
            curve = plot.plot(pen=pg.mkPen(self.colors[idx], width=1.15))
            self.plots.append(plot)
            self.curves.append(curve)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(6)
        layout.addLayout(header)
        layout.addWidget(self.grid, 1)

    def set_velocity(self, value: float | None) -> None:
        if value is None or np.isnan(value):
            self.velocity_label.setText("最新流速 -- m/s")
        else:
            self.velocity_label.setText(f"最新流速 {value:.3f} m/s")

    def update_data(self, snapshot: BufferSnapshot) -> None:
        if snapshot.timestamps.size == 0:
            for curve in self.curves:
                curve.setData([], [])
            return

        step = max(1, snapshot.timestamps.size // MAX_PLOT_POINTS)
        x = snapshot.timestamps[::step]
        channels = snapshot.channels[::step]
        for idx, curve in enumerate(self.curves):
            y = channels[:, idx]
            curve.setData(x, y)
            if y.size:
                y_min = float(np.nanmin(y))
                y_max = float(np.nanmax(y))
                if np.isfinite(y_min) and np.isfinite(y_max):
                    if y_min == y_max:
                        y_min -= 0.05
                        y_max += 0.05
                    padding = max((y_max - y_min) * 0.08, 0.01)
                    self.plots[idx].setYRange(y_min - padding, y_max + padding, padding=0)

    def set_x_range(self, start: float, end: float) -> None:
        if end <= start:
            return
        for plot in self.plots:
            plot.setXRange(start, end, padding=0)
