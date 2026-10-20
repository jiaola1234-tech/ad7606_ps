from __future__ import annotations

import numpy as np
import pyqtgraph as pg
from PySide6.QtCore import QRectF, Qt
from PySide6.QtWidgets import QGridLayout, QWidget

from ..matlab_interface import AnalysisResult


def _matlab_jet_lut(size: int = 256) -> np.ndarray:
    x = np.linspace(0.0, 1.0, size)
    red = np.clip(1.5 - np.abs(4.0 * x - 3.0), 0.0, 1.0)
    green = np.clip(1.5 - np.abs(4.0 * x - 2.0), 0.0, 1.0)
    blue = np.clip(1.5 - np.abs(4.0 * x - 1.0), 0.0, 1.0)
    return (np.column_stack([red, green, blue]) * 255).astype(np.ubyte)


class AnalysisPanel(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        pg.setConfigOption("foreground", "k")
        self.setStyleSheet("background: white;")

        layout = QGridLayout(self)
        layout.setContentsMargins(30, 24, 30, 24)
        layout.setHorizontalSpacing(28)
        layout.setVerticalSpacing(30)
        layout.setRowStretch(0, 48)
        layout.setRowStretch(1, 15)
        layout.setRowStretch(2, 17)
        layout.setColumnStretch(0, 26)
        layout.setColumnStretch(1, 26)
        layout.setColumnStretch(2, 24)

        self.ridge_plot = pg.PlotWidget(title="主脊线图：高斯平滑后 k-f 谱")
        self.original_plot = pg.PlotWidget(title="原始 MUSIC")
        self.subtract_plot = pg.PlotWidget(title="对称相减")
        self.energy_plot = pg.PlotWidget(title="能量积分")
        self.history_plot = pg.PlotWidget(title="流速随时间变化")
        self.sources_plot = pg.PlotWidget(title="信号源数")

        layout.addWidget(self.ridge_plot, 0, 0, 1, 3)
        layout.addWidget(self.original_plot, 1, 0)
        layout.addWidget(self.subtract_plot, 1, 1)
        layout.addWidget(self.energy_plot, 1, 2)
        layout.addWidget(self.history_plot, 2, 0, 1, 2)
        layout.addWidget(self.sources_plot, 2, 2)

        self.original_image = pg.ImageItem()
        self.subtract_image = pg.ImageItem()
        self.ridge_image = pg.ImageItem()
        self.original_plot.addItem(self.original_image)
        self.subtract_plot.addItem(self.subtract_image)
        self.ridge_plot.addItem(self.ridge_image)

        ridge_pen = pg.mkPen("#ff0000", width=2, style=Qt.DashLine)
        self.original_ridge_line = self.original_plot.plot(pen=ridge_pen)
        self.subtract_ridge_line = self.subtract_plot.plot(pen=ridge_pen)
        self.ridge_ridge_line = self.ridge_plot.plot(pen=ridge_pen)
        self.energy_curve = self.energy_plot.plot(pen=pg.mkPen("#0000ff", width=2))
        self.energy_peak = self.energy_plot.plot(
            pen=None,
            symbol="o",
            symbolBrush=None,
            symbolPen=pg.mkPen("#ff0000", width=2),
            symbolSize=9,
        )
        self.history_curve = self.history_plot.plot(
            pen=pg.mkPen("#0000ff", width=2),
            symbol="o",
            symbolBrush=None,
            symbolPen=pg.mkPen("#0000ff", width=2),
            symbolSize=6,
        )
        self.sources_curve = self.sources_plot.plot(
            pen=pg.mkPen("#00cc00", width=2),
            symbol="o",
            symbolBrush=None,
            symbolPen=pg.mkPen("#00cc00", width=2),
            symbolSize=6,
        )
        self._lut = _matlab_jet_lut()

        for plot in (self.original_plot, self.subtract_plot, self.ridge_plot):
            plot.setLabel("left", "f (Hz)")
            plot.setLabel("bottom", "k (rad/m)")
        self.energy_plot.setLabel("left", "累计能量")
        self.energy_plot.setLabel("bottom", "u (m/s)")
        self.history_plot.setLabel("left", "u (m/s)")
        self.history_plot.setLabel("bottom", "t (s)")
        self.sources_plot.setLabel("left", "N")
        self.sources_plot.setLabel("bottom", "t (s)")

        for plot in (
            self.original_plot,
            self.subtract_plot,
            self.ridge_plot,
            self.energy_plot,
            self.history_plot,
            self.sources_plot,
        ):
            plot.setBackground("w")
            plot.showGrid(x=True, y=True, alpha=0.20)
            plot.setDownsampling(auto=True)
            for axis_name in ("left", "bottom"):
                axis = plot.getAxis(axis_name)
                axis.setPen(pg.mkPen("#222222"))
                axis.setTextPen(pg.mkPen("#222222"))

        self.ridge_plot.setMinimumHeight(360)
        self.original_plot.setMinimumHeight(125)
        self.subtract_plot.setMinimumHeight(125)
        self.energy_plot.setMinimumHeight(125)
        self.history_plot.setMinimumHeight(135)
        self.sources_plot.setMinimumHeight(135)
        self._set_placeholder()

    def _set_placeholder(self) -> None:
        self.original_image.clear()
        self.subtract_image.clear()
        self.ridge_image.clear()
        self.original_image.setVisible(False)
        self.subtract_image.setVisible(False)
        self.ridge_image.setVisible(False)
        self.energy_curve.setData([], [])
        self.energy_peak.setData([], [])
        self.history_curve.setData([], [])
        self.sources_curve.setData([], [])
        self.original_ridge_line.setData([], [])
        self.subtract_ridge_line.setData([], [])
        self.ridge_ridge_line.setData([], [])

    def update_result(self, result: AnalysisResult) -> None:
        if result.p_original.size:
            self._update_heatmap(self.original_image, self.original_plot, result.k_axis, result.f_axis, result.p_original)
        if result.p_after_subtract.size:
            self._update_heatmap(
                self.subtract_image,
                self.subtract_plot,
                result.k_axis,
                result.f_axis,
                result.p_after_subtract,
            )
        if result.p_use.size:
            self._update_heatmap(self.ridge_image, self.ridge_plot, result.k_axis, result.f_axis, result.p_use)
        if result.energy.size:
            self.energy_curve.setData(result.u_trials, result.energy)
            self.energy_peak.setData([result.velocity], [np.interp(result.velocity, result.u_trials, result.energy)])
            self.energy_plot.setXRange(0.0, 8.0, padding=0)
        if result.time_points.size and result.velocity_points.size:
            self.history_curve.setData(result.time_points, result.velocity_points)
            self.history_plot.setXRange(float(np.nanmin(result.time_points)), float(np.nanmax(result.time_points)), padding=0.08)
        if result.time_points.size and result.estimated_sources_history.size:
            self.sources_curve.setData(result.time_points, result.estimated_sources_history)
            self.sources_plot.setYRange(0, 21, padding=0)
            self.sources_plot.setXRange(float(np.nanmin(result.time_points)), float(np.nanmax(result.time_points)), padding=0.08)
        if result.f_axis.size and np.isfinite(result.velocity) and result.velocity > 0:
            ridge_k = (2.0 * np.pi * result.f_axis) / result.velocity
            for curve in (self.original_ridge_line, self.subtract_ridge_line, self.ridge_ridge_line):
                curve.setData(ridge_k, result.f_axis)

        self.ridge_plot.setTitle(f"主脊线图：高斯平滑后 k-f 谱 (u={result.velocity:.2f} m/s, t={result.current_time:.1f} s)")
        self.original_plot.setTitle(f"原始 MUSIC (Sources={result.estimated_sources}, t={result.current_time:.1f} s)")
        self.subtract_plot.setTitle(f"对称相减后 (t={result.current_time:.1f} s)")
        self.energy_plot.setTitle(f"能量积分, u_opt={result.velocity:.2f} m/s")

    def _update_heatmap(
        self,
        image: pg.ImageItem,
        plot: pg.PlotItem,
        x_axis: np.ndarray,
        y_axis: np.ndarray,
        values: np.ndarray,
    ) -> None:
        image.setVisible(True)
        image.setImage(values.T, autoLevels=True)
        image.setLookupTable(self._lut)
        if x_axis.size >= 2 and y_axis.size >= 2:
            image.setRect(
                QRectF(
                    float(x_axis[0]),
                    float(y_axis[0]),
                    float(x_axis[-1] - x_axis[0]),
                    float(y_axis[-1] - y_axis[0]),
                )
            )
            plot.setXRange(float(x_axis[0]), float(x_axis[-1]), padding=0)
            plot.setYRange(float(y_axis[0]), float(y_axis[-1]), padding=0)
