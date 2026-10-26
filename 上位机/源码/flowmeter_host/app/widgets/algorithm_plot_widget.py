from __future__ import annotations

import numpy as np
import pyqtgraph as pg
from PySide6.QtCore import QRectF
from PySide6.QtWidgets import QVBoxLayout, QWidget

from ..matlab_interface import AlgorithmPlotData


class AlgorithmPlotWidget(QWidget):
    """Native Python MUSIC plots shown when MATLAB figures are unavailable."""

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.setObjectName("AlgorithmPlotWidget")
        self._velocity_history: list[float] = []
        self._time_history: list[int] = []

        self.grid = pg.GraphicsLayoutWidget()
        self.grid.setBackground("w")
        self.spectrum_plot = self.grid.addPlot(row=0, col=0, title="Python MUSIC：k-f 谱")
        self.spectrum_plot.setLabel("bottom", "k", units="rad/m")
        self.spectrum_plot.setLabel("left", "频率", units="Hz")
        self.spectrum_plot.showGrid(x=True, y=True, alpha=0.2)
        self.spectrum_image = pg.ImageItem(axisOrder="row-major")
        self.spectrum_plot.addItem(self.spectrum_image)

        self.energy_plot = self.grid.addPlot(row=1, col=0, title="速度能量积分")
        self.energy_plot.setLabel("bottom", "速度", units="m/s")
        self.energy_plot.setLabel("left", "能量")
        self.energy_plot.showGrid(x=True, y=True, alpha=0.2)
        self.energy_curve = self.energy_plot.plot(pen=pg.mkPen("#1f77b4", width=1.5))
        self.energy_marker = self.energy_plot.plot(
            pen=None, symbol="o", symbolSize=9, symbolBrush="#d62728", symbolPen="#d62728"
        )

        self.history_plot = self.grid.addPlot(row=1, col=1, title="流速随分析次数变化")
        self.history_plot.setLabel("bottom", "分析次数")
        self.history_plot.setLabel("left", "流速", units="m/s")
        self.history_plot.showGrid(x=True, y=True, alpha=0.2)
        self.history_curve = self.history_plot.plot(pen=pg.mkPen("#2ca02c", width=1.5), symbol="o", symbolSize=4)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.addWidget(self.grid)

    def update_data(self, data: AlgorithmPlotData) -> None:
        frequencies = np.asarray(data.frequencies, dtype=float)
        k_axis = np.asarray(data.k_axis, dtype=float)
        spectrum = np.asarray(data.spectrum_db, dtype=float)
        if frequencies.size and k_axis.size and spectrum.size:
            finite = np.isfinite(spectrum)
            if np.any(finite):
                clean = np.nan_to_num(spectrum, nan=float(np.nanmin(spectrum[finite])))
                self.spectrum_image.setImage(clean, autoLevels=True)
                self.spectrum_image.setRect(
                    QRectF(float(k_axis[0]), float(frequencies[0]),
                           float(k_axis[-1] - k_axis[0]), float(frequencies[-1] - frequencies[0]))
                )
                self.spectrum_plot.setXRange(float(k_axis[0]), float(k_axis[-1]), padding=0)
                self.spectrum_plot.setYRange(float(frequencies[0]), float(frequencies[-1]), padding=0)

        velocities = np.asarray(data.velocities, dtype=float)
        energy = np.asarray(data.energy, dtype=float)
        if velocities.size and energy.size:
            self.energy_curve.setData(velocities, energy)
            velocity = float(data.velocity)
            if np.isfinite(velocity):
                energy_at_velocity = float(np.interp(velocity, velocities, energy))
                self.energy_marker.setData([velocity], [energy_at_velocity])
                self.energy_plot.setXRange(float(velocities[0]), float(velocities[-1]), padding=0)

        velocity = float(data.velocity)
        if np.isfinite(velocity):
            self._velocity_history.append(velocity)
            self._time_history.append(len(self._velocity_history))
            self._velocity_history = self._velocity_history[-120:]
            self._time_history = self._time_history[-120:]
            self.history_curve.setData(self._time_history, self._velocity_history)
            if self._time_history:
                self.history_plot.setXRange(self._time_history[0], self._time_history[-1] + 1, padding=0.02)
