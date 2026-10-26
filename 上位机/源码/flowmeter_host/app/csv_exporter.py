from __future__ import annotations

import csv
import queue
import threading
import time
from pathlib import Path
from typing import Callable

import pandas as pd

from .data_buffer import BufferSnapshot
from .settings import CHANNEL_COUNT


CSV_COLUMNS = ["timestamp", *[f"ch{idx + 1}" for idx in range(CHANNEL_COUNT)], "flow_velocity"]


CsvFinishedCallback = Callable[[str, int, Exception | None], None]


class CsvRecordingWriter:
    """Write recording chunks on a worker thread while acquisition continues.

    The GUI only enqueues numpy arrays.  Disk I/O and row conversion happen in
    this worker, so finishing a recording does not concatenate a large in-memory
    recording or call pandas from the GUI thread.
    """

    def __init__(self, path: str | Path, on_finished: CsvFinishedCallback | None = None):
        self.path = Path(path)
        self._on_finished = on_finished or (lambda _path, _count, _error: None)
        self._queue: queue.Queue[tuple[object, object, object] | None] = queue.Queue()
        self._thread: threading.Thread | None = None
        self._finish_requested = False
        self._lock = threading.Lock()

    @property
    def running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def start(self) -> None:
        with self._lock:
            if self._thread is not None:
                raise RuntimeError("CSV writer has already started")
            self._thread = threading.Thread(target=self._run, name="csv-writer", daemon=True)
            self._thread.start()

    def append(self, timestamps, channels, flow_velocity) -> None:
        with self._lock:
            if self._thread is None or self._finish_requested:
                raise RuntimeError("CSV writer is not accepting samples")
            # The producer owns these arrays after this call.  Copies prevent a
            # future caller mutation from changing rows that are waiting to be written.
            self._queue.put((timestamps.copy(), channels.copy(), flow_velocity.copy()))

    def finish(self) -> None:
        with self._lock:
            if self._thread is None or self._finish_requested:
                return
            self._finish_requested = True
            self._queue.put(None)

    def wait(self, timeout: float | None = None) -> bool:
        thread = self._thread
        if thread is None:
            return True
        thread.join(timeout)
        return not thread.is_alive()

    def _run(self) -> None:
        count = 0
        error: Exception | None = None
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            with self.path.open("w", newline="", encoding="utf-8-sig") as handle:
                writer = csv.writer(handle)
                writer.writerow(CSV_COLUMNS)
                handle.flush()
                last_flush = time.monotonic()
                while True:
                    chunk = self._queue.get()
                    if chunk is None:
                        break
                    timestamps, channels, flow_velocity = chunk
                    rows = zip(timestamps, channels, flow_velocity)
                    writer.writerows(
                        [float(timestamp), *[float(value) for value in values], float(velocity)]
                        for timestamp, values, velocity in rows
                    )
                    count += len(timestamps)
                    # Keep the file observable during a long recording without
                    # forcing a disk flush for every small network packet.
                    if time.monotonic() - last_flush >= 0.25:
                        handle.flush()
                        last_flush = time.monotonic()
        except Exception as exc:  # Report disk/full/permission errors to the GUI.
            error = exc
        finally:
            self._on_finished(str(self.path), count, error)


def export_snapshot(snapshot: BufferSnapshot, path: str | Path) -> None:
    columns = {"timestamp": snapshot.timestamps}
    for idx in range(CHANNEL_COUNT):
        columns[f"ch{idx + 1}"] = snapshot.channels[:, idx] if snapshot.channels.size else []
    columns["flow_velocity"] = snapshot.flow_velocity
    df = pd.DataFrame(columns)
    df.to_csv(path, index=False, encoding="utf-8-sig")
