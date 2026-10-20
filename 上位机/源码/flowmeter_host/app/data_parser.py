from dataclasses import dataclass
from typing import Iterable

import numpy as np

from .settings import CHANNEL_COUNT, DEFAULT_SAMPLE_RATE_HZ


AD7606_SCALE_FACTOR = 5.0 / 32768.0


@dataclass(frozen=True)
class ParsedSample:
    timestamp: float
    values: np.ndarray


class DataParser:
    """Parser for text mock data and AD7606 TCP binary frames."""

    def __init__(self, sample_rate_hz: int = DEFAULT_SAMPLE_RATE_HZ):
        self.sample_rate_hz = max(1, int(sample_rate_hz))
        self._text_remainder = ""
        self._binary_remainder = b""
        self._next_timestamp = 0.0

    def set_sample_rate(self, sample_rate_hz: int) -> None:
        self.sample_rate_hz = max(1, int(sample_rate_hz))

    def parse_bytes(self, payload: bytes) -> list[ParsedSample]:
        if self._looks_like_text(payload):
            return self._parse_text_bytes(payload)
        return self._parse_ad7606_binary(payload)

    def _looks_like_text(self, payload: bytes) -> bool:
        if not payload:
            return True
        return all(byte in (9, 10, 13) or 32 <= byte <= 126 for byte in payload)

    def _parse_text_bytes(self, payload: bytes) -> list[ParsedSample]:
        text = self._text_remainder + payload.decode("utf-8", errors="strict")
        lines = text.splitlines(keepends=True)
        self._text_remainder = ""
        if lines and not lines[-1].endswith(("\n", "\r")):
            self._text_remainder = lines.pop()

        samples: list[ParsedSample] = []
        for line in lines:
            line = line.strip()
            if not line:
                continue
            samples.append(self.parse_line(line))
        return samples

    def _parse_ad7606_binary(self, payload: bytes) -> list[ParsedSample]:
        data = self._binary_remainder + payload
        frame_bytes = CHANNEL_COUNT * np.dtype("<i2").itemsize
        usable = (len(data) // frame_bytes) * frame_bytes
        self._binary_remainder = data[usable:]
        if usable == 0:
            return []

        values = np.frombuffer(data[:usable], dtype="<i2").reshape(-1, CHANNEL_COUNT).astype(float)
        values *= AD7606_SCALE_FACTOR
        step = 1.0 / self.sample_rate_hz
        timestamps = self._next_timestamp + np.arange(values.shape[0], dtype=float) * step
        self._next_timestamp = float(timestamps[-1] + step)
        return [ParsedSample(float(ts), row) for ts, row in zip(timestamps, values)]

    def parse_line(self, line: str) -> ParsedSample:
        parts = [item.strip() for item in line.split(",")]
        if len(parts) != CHANNEL_COUNT + 1:
            raise ValueError(f"expected {CHANNEL_COUNT + 1} fields, got {len(parts)}: {line!r}")
        try:
            numbers = [float(item) for item in parts]
        except ValueError as exc:
            raise ValueError(f"non-numeric data: {line!r}") from exc
        return ParsedSample(numbers[0], np.asarray(numbers[1:], dtype=float))


def parse_lines(lines: Iterable[str]) -> list[ParsedSample]:
    parser = DataParser()
    return [parser.parse_line(line) for line in lines]
