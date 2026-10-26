from __future__ import annotations

import tempfile
import sys
import threading
import socket
import time
from pathlib import Path

import numpy as np
import pandas as pd

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from app.csv_exporter import CsvRecordingWriter, export_snapshot
from app.data_buffer import DataBuffer
from app.data_parser import AD7606_SCALE_FACTOR, DataParser
from app.matlab_interface import MatlabAlgorithmWorker, python_music_analysis, python_music_velocity
from app.network_receiver import NetworkReceiver, ReceiverConfig
from app.settings import DEFAULT_DISPLAY_WINDOW_SECONDS, DEFAULT_NETWORK_IP, DEFAULT_NETWORK_MODE, DEFAULT_NETWORK_PORT


def test_parser() -> None:
    sample = DataParser().parse_line("1.250,1,2,3,4,5,6,7,8")
    assert sample.timestamp == 1.25
    assert sample.values.shape == (8,)
    assert sample.values[-1] == 8


def test_ad7606_binary_parser() -> None:
    parser = DataParser(sample_rate_hz=1000)
    raw = np.arange(16, dtype="<i2").tobytes()
    assert parser.parse_bytes(raw[:7]) == []
    samples = parser.parse_bytes(raw[7:])
    assert len(samples) == 2
    assert samples[0].timestamp == 0.0
    assert samples[1].timestamp == 0.001
    np.testing.assert_allclose(samples[0].values, np.arange(8) * AD7606_SCALE_FACTOR)
    np.testing.assert_allclose(samples[1].values, np.arange(8, 16) * AD7606_SCALE_FACTOR)


def test_default_network_config() -> None:
    assert DEFAULT_NETWORK_MODE == "TCP Client"
    assert DEFAULT_NETWORK_IP == "192.168.1.10"
    assert DEFAULT_NETWORK_PORT == 5001
    assert DEFAULT_DISPLAY_WINDOW_SECONDS == 1.0


def test_buffer_and_range() -> None:
    buffer = DataBuffer(capacity=5)
    for idx in range(8):
        buffer.append(float(idx), np.full(8, idx, dtype=float))
    assert buffer.size == 5
    snap = buffer.range(4.0, 6.0)
    assert snap.timestamps.tolist() == [4.0, 5.0, 6.0]
    assert snap.channels.shape == (3, 8)


def test_csv_export() -> None:
    buffer = DataBuffer(capacity=10)
    for idx in range(3):
        buffer.append(float(idx), np.full(8, idx, dtype=float), flow_velocity=idx * 0.1)
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "out.csv"
        export_snapshot(buffer.snapshot(), path)
        df = pd.read_csv(path)
    assert list(df.columns) == [
        "timestamp",
        "ch1",
        "ch2",
        "ch3",
        "ch4",
        "ch5",
        "ch6",
        "ch7",
        "ch8",
        "flow_velocity",
    ]
    assert len(df) == 3


def test_csv_recording_writer() -> None:
    finished = threading.Event()
    result = {}
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "stream.csv"

        def on_finished(csv_path: str, count: int, error: Exception | None) -> None:
            result.update(path=csv_path, count=count, error=error)
            finished.set()

        writer = CsvRecordingWriter(path, on_finished)
        writer.start()
        writer.append(
            np.asarray([0.0, 0.01]),
            np.arange(16, dtype=float).reshape(2, 8),
            np.asarray([1.0, 1.1]),
        )
        writer.finish()
        assert finished.wait(2.0)
        assert result["error"] is None
        assert result["count"] == 2
        df = pd.read_csv(path)
        assert len(df) == 2
        assert df.loc[1, "flow_velocity"] == 1.1


def test_tcp_client_retries_until_server_is_ready() -> None:
    output = __import__("queue").Queue()
    logs = []
    receiver = NetworkReceiver(output, logs.append)
    port = 19231
    receiver.start(ReceiverConfig("TCP Client", "127.0.0.1", port))
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        time.sleep(1.2)
        server.bind(("127.0.0.1", port))
        server.listen(1)
        server.settimeout(2.0)
        client, _ = server.accept()
        try:
            client.sendall(b"0.000,1,2,3,4,5,6,7,8\n")
            deadline = time.time() + 2.0
            while time.time() < deadline and output.empty():
                time.sleep(0.02)
            assert not output.empty()
            assert any("连接失败" in message for message in logs)
            assert any("TCP Client 已连接" in message for message in logs)
        finally:
            client.close()
    finally:
        receiver.stop()
        server.close()


def test_matlab_fallback() -> None:
    worker = MatlabAlgorithmWorker()
    data = np.random.default_rng(7).normal(size=(256, 8))
    velocity = worker._fallback_velocity(data, 1000)
    assert np.isfinite(velocity)
    assert 0 <= velocity <= 10
    assert worker.backend == "Python"


def test_python_music_algorithm() -> None:
    rng = np.random.default_rng(11)
    fs = 1000
    t = np.arange(fs * 2) / fs
    data = np.column_stack([np.sin(2 * np.pi * 20 * t + idx * 0.2) for idx in range(8)])
    data += 0.02 * rng.normal(size=data.shape)
    velocity = python_music_velocity(data, fs)
    assert np.isfinite(velocity)
    assert 0.5 <= velocity <= 8.0


def test_python_music_plot_data() -> None:
    rng = np.random.default_rng(13)
    fs = 1000
    t = np.arange(fs * 2) / fs
    data = np.column_stack([np.sin(2 * np.pi * 20 * t + idx * 0.2) for idx in range(8)])
    velocity, plot_data = python_music_analysis(data, fs)
    assert np.isfinite(velocity)
    assert plot_data is not None
    assert plot_data.spectrum_db.shape == (103, 800)
    assert plot_data.frequencies.size == 103
    assert plot_data.energy.size == 400


def test_real_algorithm_file_discovery() -> None:
    algorithm = PROJECT_ROOT.parent / "ana96.m"
    if not algorithm.exists():
        return
    worker = MatlabAlgorithmWorker()
    worker.load_algorithm(algorithm)
    try:
        assert worker.function_name == "ana95"
        assert worker.algorithm_path is not None
        assert worker.algorithm_path.name == "ana95.m"
    finally:
        worker.stop()


def main() -> None:
    test_parser()
    test_ad7606_binary_parser()
    test_default_network_config()
    test_buffer_and_range()
    test_csv_export()
    test_csv_recording_writer()
    test_tcp_client_retries_until_server_is_ready()
    test_matlab_fallback()
    test_python_music_algorithm()
    test_python_music_plot_data()
    test_real_algorithm_file_discovery()
    print("smoke tests passed: parser, ring buffer, CSV export, MATLAB fallback, algorithm discovery")


if __name__ == "__main__":
    main()
