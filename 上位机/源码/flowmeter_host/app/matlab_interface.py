from __future__ import annotations

import ctypes
import os
import queue
import re
import shutil
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import numpy as np


LogCallback = Callable[[str], None]
ResultCallback = Callable[[float, int | None], None]


@dataclass
class MatlabTask:
    data_nx8: np.ndarray
    sample_rate_hz: int


class MatlabAlgorithmWorker:
    """Runs either the MATLAB ana*.m function or a Python MUSIC implementation."""

    def __init__(self, log: LogCallback | None = None, result: ResultCallback | None = None):
        self.log = log or (lambda _message: None)
        self.result = result or (lambda _velocity, _hwnd: None)
        self.backend = "Python"
        self.algorithm_path: Path | None = None
        self.function_name: str | None = None
        self.function_arg_count = 1
        self._engine = None
        self._engine_unavailable = False
        self._com_app = None
        self._task_queue: queue.Queue[MatlabTask] = queue.Queue(maxsize=1)
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._last_hwnd: int | None = None
        self._runtime_dir: Path | None = None

    @property
    def loaded(self) -> bool:
        return self.algorithm_path is not None

    def set_backend(self, backend: str) -> None:
        backend = backend.strip()
        self.backend = "MATLAB" if backend.upper() == "MATLAB" else "Python"
        if self.backend == "Python":
            self._close_matlab_sessions()
        self.log(f"算法后端已切换为 {self.backend}")

    def load_algorithm(self, path: str | os.PathLike[str]) -> None:
        source = Path(path).resolve()
        if not source.exists():
            raise FileNotFoundError(source)
        text = source.read_text(encoding="utf-8", errors="ignore")
        match = re.search(
            r"^\s*function\s+(?:\[[^\]]+\]|\w+)?\s*=?\s*(\w+)\s*\(([^)]*)\)",
            text,
            re.MULTILINE,
        )
        function_name = match.group(1) if match else source.stem
        if match:
            args = [arg.strip() for arg in match.group(2).split(",") if arg.strip()]
            self.function_arg_count = len(args)
        else:
            self.function_arg_count = 1

        runtime_dir = Path(tempfile.gettempdir()) / "flowmeter_host_matlab"
        runtime_dir.mkdir(parents=True, exist_ok=True)
        runtime_file = runtime_dir / f"{function_name}.m"
        shutil.copyfile(source, runtime_file)
        self.algorithm_path = runtime_file
        self.function_name = function_name
        self._runtime_dir = runtime_dir
        self.log(f"MATLAB 算法已加载: {source.name} -> {function_name}()")
        self.start()

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        self._thread = None
        self._close_matlab_sessions()

    def _close_matlab_sessions(self) -> None:
        if self._engine is not None:
            try:
                self._engine.quit()
            except Exception:
                pass
            self._engine = None
        if self._com_app is not None:
            try:
                self._com_app.Execute("quit force;")
            except Exception:
                pass
            self._com_app = None

    def submit(self, data_nx8: np.ndarray, sample_rate_hz: int) -> None:
        if data_nx8.size == 0:
            return
        task = MatlabTask(np.asarray(data_nx8, dtype=float).copy(), int(sample_rate_hz))
        try:
            while True:
                self._task_queue.get_nowait()
        except queue.Empty:
            pass
        try:
            self._task_queue.put_nowait(task)
        except queue.Full:
            pass

    def _run(self) -> None:
        self._co_initialize()
        while not self._stop_event.is_set():
            try:
                task = self._task_queue.get(timeout=0.2)
            except queue.Empty:
                continue
            try:
                velocity = self._calculate(task)
                hwnd = self._find_matlab_window() if self.backend == "MATLAB" else None
                self.result(float(velocity), hwnd)
            except Exception as exc:
                self.log(f"{self.backend} 算法运行失败: {exc}")

    def _calculate(self, task: MatlabTask) -> float:
        if self.backend == "Python":
            return python_music_velocity(task.data_nx8, task.sample_rate_hz)

        engine = self._ensure_engine()
        if engine is not None and self.function_name:
            return self._calculate_with_engine(engine, task)

        com_app = self._ensure_com_app()
        if com_app is not None and self.function_name:
            return self._calculate_with_com(com_app, task)

        self.log("MATLAB 不可用，临时使用 Python 算法")
        return python_music_velocity(task.data_nx8, task.sample_rate_hz)

    def _ensure_engine(self):
        if self._engine is not None:
            return self._engine
        if self._engine_unavailable:
            return None
        try:
            import matlab.engine  # type: ignore
        except Exception as exc:
            self.log(f"未检测到可用 MATLAB Engine，尝试 MATLAB COM 自动化: {exc}")
            self._engine_unavailable = True
            return None
        self.log("正在启动 MATLAB Engine...")
        self._engine = matlab.engine.start_matlab()
        if self._runtime_dir is not None:
            self._engine.addpath(str(self._runtime_dir), nargout=0)
        self.log("MATLAB Engine 已启动")
        return self._engine

    def _ensure_com_app(self):
        if os.name != "nt":
            return None
        if self._com_app is not None:
            return self._com_app
        try:
            import pythoncom  # type: ignore
            import win32com.client  # type: ignore

            pythoncom.CoInitialize()
            self.log("正在启动 MATLAB COM 自动化...")
            self._com_app = win32com.client.Dispatch("Matlab.Application")
            if self._runtime_dir is not None:
                self._com_app.Execute(f"addpath('{matlab_quote(str(self._runtime_dir))}');")
            self.log("MATLAB COM 已启动")
            return self._com_app
        except Exception as exc:
            self.log(f"MATLAB COM 启动失败: {exc}")
            return None

    def _calculate_with_engine(self, engine, task: MatlabTask) -> float:
        import matlab  # type: ignore

        matlab_data = matlab.double(task.data_nx8.T.tolist())
        if self.function_arg_count >= 2:
            value = engine.feval(self.function_name, matlab_data, float(task.sample_rate_hz), nargout=1)
        else:
            value = engine.feval(self.function_name, matlab_data, nargout=1)
        engine.eval("drawnow;", nargout=0)
        return float(np.asarray(value).reshape(-1)[-1])

    def _calculate_with_com(self, com_app, task: MatlabTask) -> float:
        data_8xn = task.data_nx8.T.astype(float).tolist()
        self._put_com_variable(com_app, "DATA", data_8xn)
        if self.function_arg_count >= 2:
            command = f"u_opt = {self.function_name}(DATA, {float(task.sample_rate_hz)}); drawnow;"
        else:
            command = f"u_opt = {self.function_name}(DATA); drawnow;"
        output = com_app.Execute(command)
        if output:
            self.log(str(output).strip())
        return float(np.asarray(self._get_com_variable(com_app, "u_opt")).reshape(-1)[-1])

    def _put_com_variable(self, com_app, name: str, value) -> None:
        zeros = np.zeros((len(value), len(value[0]) if value else 0), dtype=float).tolist()
        try:
            com_app.PutFullMatrix(name, "base", value, zeros)
            return
        except Exception:
            pass
        com_app.PutWorkspaceData(name, "base", np.asarray(value, dtype=float).tolist())
        com_app.Execute(f"{name} = double(cell2mat({name}));")

    def _get_com_variable(self, com_app, name: str):
        try:
            return com_app.GetWorkspaceData(name, "base")
        except Exception:
            pass
        return com_app.GetVariable(name, "base")

    def _fallback_velocity(self, data_nx8: np.ndarray, sample_rate_hz: int) -> float:
        return python_music_velocity(data_nx8, sample_rate_hz)

    def _find_matlab_window(self) -> int | None:
        if os.name != "nt":
            return None
        titles = []
        if self.function_name:
            titles.append(self.function_name.lower())
        titles.extend(["ana95", "实时流速", "music"])
        hwnd = find_window_by_titles(titles)
        if hwnd:
            self._last_hwnd = hwnd
        return self._last_hwnd

    def _co_initialize(self) -> None:
        if os.name != "nt":
            return
        try:
            import pythoncom  # type: ignore

            pythoncom.CoInitialize()
        except Exception:
            pass


def python_music_velocity(data_nx8: np.ndarray, sample_rate_hz: int) -> float:
    data = np.asarray(data_nx8, dtype=float)
    if data.ndim != 2 or data.shape[1] < 2:
        return float("nan")

    channels = min(8, data.shape[1])
    data = data[:, :channels]
    sample_rate_hz = max(1, int(sample_rate_hz))
    window_len = min(data.shape[0], max(8, int(round(sample_rate_hz * 2.0))))
    if window_len < 8:
        return float("nan")
    data = data[-window_len:]

    sensor_spacing = 0.0605
    analysis_band = (0.0, 50.0)
    num_k = 800
    u_trials = np.linspace(0.5, 8.0, 400)
    k_valid_range = (0.0, 83.0)
    num_sources = min(7, channels - 1)

    nfft = 1 << int(np.ceil(np.log2(window_len)))
    window = np.hamming(window_len)
    spectrum = np.fft.rfft((data - np.mean(data, axis=0)) * window[:, None], n=nfft, axis=0)
    f_axis_full = np.fft.rfftfreq(nfft, d=1.0 / sample_rate_hz)
    mask = (f_axis_full >= analysis_band[0]) & (f_axis_full <= analysis_band[1])
    f_axis = f_axis_full[mask]
    spectrum = spectrum[mask].T
    if f_axis.size == 0:
        return float("nan")

    positions = np.arange(channels, dtype=float) * sensor_spacing
    k_axis = np.linspace(-2.0 * np.pi / sensor_spacing, 2.0 * np.pi / sensor_spacing, num_k)
    steering = np.exp(1j * np.outer(positions, k_axis))
    steering /= np.maximum(np.linalg.norm(steering, axis=0, keepdims=True), np.finfo(float).eps)

    p_music = np.empty((f_axis.size, num_k), dtype=float)
    eye = np.eye(channels)
    for fi in range(f_axis.size):
        x = spectrum[:, fi : fi + 1]
        r = x @ x.conj().T
        r = 0.5 * (r + r.conj().T)
        trace = float(np.real(np.trace(r)))
        r = r + max(trace, np.finfo(float).eps) * 1e-3 * eye
        eigvals, eigvecs = np.linalg.eigh(r)
        order = np.argsort(eigvals)[::-1]
        eigvecs = eigvecs[:, order]
        noise = eigvecs[:, num_sources:]
        denom = np.sum(np.abs(noise.conj().T @ steering) ** 2, axis=0)
        p_music[fi] = 1.0 / np.maximum(np.real(denom), np.finfo(float).eps)

    p_use = _smooth2d(10.0 * np.log10(np.abs(p_music) + np.finfo(float).eps))
    energy = np.zeros(u_trials.size, dtype=float)
    for ui, velocity in enumerate(u_trials):
        k_line = (2.0 * np.pi * f_axis) / velocity
        valid = (
            (k_line >= k_valid_range[0])
            & (k_line <= k_valid_range[1])
            & (k_line >= k_axis[0])
            & (k_line <= k_axis[-1])
        )
        if np.any(valid):
            vals = [
                np.interp(k_line[fi], k_axis, p_use[fi])
                for fi in np.flatnonzero(valid)
            ]
            energy[ui] = float(np.mean(vals))

    if not np.any(np.isfinite(energy)):
        return float("nan")
    energy = _moving_average(energy, 3)
    selected = int(np.nanargmax(energy))
    u_opt = float(u_trials[selected])
    lo = max(u_opt * 0.95, u_trials[0])
    hi = min(u_opt * 1.05, u_trials[-1])
    fine = np.linspace(lo, hi, 50)
    fine_energy = np.interp(fine, u_trials, energy)
    return float(fine[int(np.nanargmax(fine_energy))])


def _moving_average(values: np.ndarray, width: int) -> np.ndarray:
    if width <= 1:
        return values
    kernel = np.ones(width, dtype=float) / width
    return np.convolve(values, kernel, mode="same")


def _smooth2d(values: np.ndarray) -> np.ndarray:
    padded = np.pad(values, ((1, 1), (1, 1)), mode="edge")
    return (
        padded[:-2, :-2]
        + padded[:-2, 1:-1]
        + padded[:-2, 2:]
        + padded[1:-1, :-2]
        + 2.0 * padded[1:-1, 1:-1]
        + padded[1:-1, 2:]
        + padded[2:, :-2]
        + padded[2:, 1:-1]
        + padded[2:, 2:]
    ) / 10.0


def find_window_by_titles(title_fragments: list[str]) -> int | None:
    user32 = ctypes.windll.user32
    matches: list[int] = []

    def callback(hwnd, _lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        length = user32.GetWindowTextLengthW(hwnd)
        if length <= 0:
            return True
        buffer = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buffer, length + 1)
        title = buffer.value.lower()
        if any(fragment and fragment in title for fragment in title_fragments):
            matches.append(hwnd)
            return False
        return True

    enum_proc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)(callback)
    user32.EnumWindows(enum_proc, 0)
    time.sleep(0.05)
    return matches[0] if matches else None


def matlab_quote(path: str) -> str:
    return path.replace("'", "''")
