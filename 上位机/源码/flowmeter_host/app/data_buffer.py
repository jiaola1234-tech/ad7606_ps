from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .settings import CHANNEL_COUNT


@dataclass
class BufferSnapshot:
    timestamps: np.ndarray
    channels: np.ndarray
    flow_velocity: np.ndarray


class DataBuffer:
    """Fixed-size ring buffer with chronological snapshots and time slicing."""

    def __init__(self, capacity: int):
        self.capacity = max(1, int(capacity))
        self.timestamps = np.full(self.capacity, np.nan, dtype=float)
        self.channels = np.full((self.capacity, CHANNEL_COUNT), np.nan, dtype=float)
        self.flow_velocity = np.full(self.capacity, np.nan, dtype=float)
        self.start = 0
        self.size = 0
        self.total_received = 0

    def resize(self, capacity: int) -> None:
        old = self.snapshot()
        self.__init__(capacity)
        if old.timestamps.size:
            keep = min(self.capacity, old.timestamps.size)
            self.append_many(old.timestamps[-keep:], old.channels[-keep:], old.flow_velocity[-keep:])

    def append(self, timestamp: float, values: np.ndarray, flow_velocity: float = np.nan) -> None:
        if self.size < self.capacity:
            idx = (self.start + self.size) % self.capacity
            self.size += 1
        else:
            idx = self.start
            self.start = (self.start + 1) % self.capacity
        self.timestamps[idx] = timestamp
        self.channels[idx] = values
        self.flow_velocity[idx] = flow_velocity
        self.total_received += 1

    def append_many(
        self,
        timestamps: np.ndarray,
        channels: np.ndarray,
        flow_velocity: np.ndarray | None = None,
    ) -> None:
        if flow_velocity is None:
            flow_velocity = np.full(len(timestamps), np.nan, dtype=float)
        for ts, values, velocity in zip(timestamps, channels, flow_velocity):
            self.append(float(ts), np.asarray(values, dtype=float), float(velocity))

    def clear(self) -> None:
        total_received = self.total_received
        self.__init__(self.capacity)
        self.total_received = total_received

    def latest_time(self) -> float | None:
        if self.size == 0:
            return None
        idx = (self.start + self.size - 1) % self.capacity
        return float(self.timestamps[idx])

    def earliest_time(self) -> float | None:
        if self.size == 0:
            return None
        return float(self.timestamps[self.start])

    def set_flow_for_latest(self, velocity: float, count: int) -> None:
        count = min(max(1, count), self.size)
        for offset in range(count):
            idx = (self.start + self.size - 1 - offset) % self.capacity
            self.flow_velocity[idx] = velocity

    def snapshot(self) -> BufferSnapshot:
        if self.size == 0:
            return BufferSnapshot(np.empty(0), np.empty((0, CHANNEL_COUNT)), np.empty(0))
        indices = (self.start + np.arange(self.size)) % self.capacity
        return BufferSnapshot(
            self.timestamps[indices].copy(),
            self.channels[indices].copy(),
            self.flow_velocity[indices].copy(),
        )

    def range(self, start_time: float, end_time: float) -> BufferSnapshot:
        snap = self.snapshot()
        if snap.timestamps.size == 0:
            return snap
        mask = (snap.timestamps >= start_time) & (snap.timestamps <= end_time)
        return BufferSnapshot(snap.timestamps[mask], snap.channels[mask], snap.flow_velocity[mask])

    def latest_seconds(self, seconds: float) -> BufferSnapshot:
        latest = self.latest_time()
        if latest is None:
            return self.snapshot()
        return self.range(latest - seconds, latest)
