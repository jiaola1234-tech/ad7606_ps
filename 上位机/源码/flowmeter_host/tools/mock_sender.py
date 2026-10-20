import argparse
import math
import socket
import time


def build_line(t: float, channels: int = 8) -> str:
    values = []
    for idx in range(channels):
        phase = idx * 0.35
        value = math.sin(2 * math.pi * 1.2 * t + phase) + 0.25 * math.sin(2 * math.pi * 4.0 * t + phase)
        values.append(value)
    return ",".join([f"{t:.6f}", *[f"{value:.6f}" for value in values]]) + "\n"


def send_udp(ip: str, port: int, rate: int) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    period = 1.0 / rate
    start = time.perf_counter()
    count = 0
    while True:
        t = time.perf_counter() - start
        sock.sendto(build_line(t).encode("utf-8"), (ip, port))
        count += 1
        next_time = start + count * period
        time.sleep(max(0.0, next_time - time.perf_counter()))


def send_tcp_client(ip: str, port: int, rate: int) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((ip, port))
    period = 1.0 / rate
    start = time.perf_counter()
    count = 0
    while True:
        t = time.perf_counter() - start
        sock.sendall(build_line(t).encode("utf-8"))
        count += 1
        next_time = start + count * period
        time.sleep(max(0.0, next_time - time.perf_counter()))


def run_tcp_server(ip: str, port: int, rate: int) -> None:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((ip, port))
    server.listen(1)
    print(f"mock TCP server listening on {ip}:{port}")
    client, addr = server.accept()
    print(f"client connected: {addr}")
    period = 1.0 / rate
    start = time.perf_counter()
    count = 0
    while True:
        t = time.perf_counter() - start
        client.sendall(build_line(t).encode("utf-8"))
        count += 1
        next_time = start + count * period
        time.sleep(max(0.0, next_time - time.perf_counter()))


def main() -> None:
    parser = argparse.ArgumentParser(description="Mock flowmeter data sender")
    parser.add_argument("--mode", choices=["udp", "tcp-client", "tcp-server"], default="udp")
    parser.add_argument("--ip", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--rate", type=int, default=200)
    args = parser.parse_args()

    if args.mode == "udp":
        send_udp(args.ip, args.port, args.rate)
    elif args.mode == "tcp-client":
        send_tcp_client(args.ip, args.port, args.rate)
    else:
        run_tcp_server(args.ip, args.port, args.rate)


if __name__ == "__main__":
    main()
