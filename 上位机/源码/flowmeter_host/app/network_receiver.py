from __future__ import annotations

import queue
import socket
import threading
from dataclasses import dataclass
from typing import Callable


LogCallback = Callable[[str], None]


@dataclass
class ReceiverConfig:
    mode: str
    ip: str
    port: int


class NetworkReceiver:
    """Socket worker that pushes raw packets or stream chunks to a thread-safe queue."""

    def __init__(self, output_queue: queue.Queue[bytes], log: LogCallback | None = None):
        self.output_queue = output_queue
        self.log = log or (lambda _message: None)
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._sock: socket.socket | None = None
        self._client_sock: socket.socket | None = None
        self._lock = threading.Lock()

    @property
    def running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def start(self, config: ReceiverConfig) -> None:
        if self.running:
            return
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._run, args=(config,), daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        self._close_sockets()
        if self._thread and self._thread.is_alive() and threading.current_thread() is not self._thread:
            self._thread.join(timeout=2.0)
        self._thread = None

    def _close_sockets(self) -> None:
        with self._lock:
            for sock in (self._client_sock, self._sock):
                if sock is not None:
                    try:
                        sock.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                    try:
                        sock.close()
                    except OSError:
                        pass
            self._client_sock = None
            self._sock = None

    def set_sample_rate(self, rate_hz: int) -> None:
        # TODO: Replace this text command with the real lower-computer protocol.
        command = f"SET_SAMPLE_RATE,{rate_hz}\n".encode("ascii")
        with self._lock:
            sock = self._client_sock or self._sock
            if sock is None:
                return
            try:
                sock.sendall(command)
            except OSError as exc:
                self.log(f"采样率命令发送失败: {exc}")

    def _run(self, config: ReceiverConfig) -> None:
        mode = config.mode.upper()
        try:
            if mode == "UDP":
                self._run_udp(config)
            elif mode == "TCP CLIENT":
                self._run_tcp_client(config)
            elif mode == "TCP SERVER":
                self._run_tcp_server(config)
            else:
                self.log(f"未知协议模式: {config.mode}")
        except OSError as exc:
            if not self._stop_event.is_set():
                self.log(f"网络错误: {exc}")
        finally:
            self._close_sockets()
            self.log("网络接收线程已退出")

    def _set_socket(self, sock: socket.socket) -> None:
        sock.settimeout(0.5)
        with self._lock:
            self._sock = sock

    def _run_udp(self, config: ReceiverConfig) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._set_socket(sock)
        sock.bind((config.ip, config.port))
        self.log(f"UDP 监听 {config.ip}:{config.port}")
        while not self._stop_event.is_set():
            try:
                data, _addr = sock.recvfrom(65535)
            except socket.timeout:
                continue
            if data:
                self.output_queue.put(data)

    def _run_tcp_client(self, config: ReceiverConfig) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._set_socket(sock)
        self.log(f"TCP Client 连接 {config.ip}:{config.port}")
        sock.connect((config.ip, config.port))
        with self._lock:
            self._client_sock = sock
        self.log("TCP Client 已连接")
        self._recv_stream(sock)

    def _run_tcp_server(self, config: ReceiverConfig) -> None:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._set_socket(server)
        server.bind((config.ip, config.port))
        server.listen(1)
        self.log(f"TCP Server 监听 {config.ip}:{config.port}")
        while not self._stop_event.is_set():
            try:
                client, addr = server.accept()
            except socket.timeout:
                continue
            self.log(f"TCP 客户端已接入: {addr[0]}:{addr[1]}")
            client.settimeout(0.5)
            with self._lock:
                self._client_sock = client
            try:
                self._recv_stream(client)
            finally:
                with self._lock:
                    self._client_sock = None
                try:
                    client.close()
                except OSError:
                    pass

    def _recv_stream(self, sock: socket.socket) -> None:
        while not self._stop_event.is_set():
            try:
                chunk = sock.recv(65535)
            except socket.timeout:
                continue
            if not chunk:
                self.log("TCP 连接已断开")
                break
            self.output_queue.put(chunk)
