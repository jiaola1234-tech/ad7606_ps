from dataclasses import dataclass


CHANNEL_COUNT = 8
DEFAULT_NETWORK_MODE = "TCP Client"
DEFAULT_NETWORK_IP = "192.168.1.10"
DEFAULT_NETWORK_PORT = 5001
DEFAULT_SAMPLE_RATE_HZ = 50000
DEFAULT_BUFFER_SECONDS = 60
DEFAULT_DISPLAY_WINDOW_SECONDS = 1.0
PLOT_REFRESH_MS = 33
QUEUE_DRAIN_LIMIT = 5000


@dataclass
class NetworkConfig:
    mode: str = DEFAULT_NETWORK_MODE
    ip: str = DEFAULT_NETWORK_IP
    port: int = DEFAULT_NETWORK_PORT


@dataclass
class AppConfig:
    sample_rate_hz: int = DEFAULT_SAMPLE_RATE_HZ
    buffer_seconds: int = DEFAULT_BUFFER_SECONDS
    display_window_seconds: float = DEFAULT_DISPLAY_WINDOW_SECONDS

    @property
    def capacity(self) -> int:
        return max(1, int(self.sample_rate_hz * self.buffer_seconds))
