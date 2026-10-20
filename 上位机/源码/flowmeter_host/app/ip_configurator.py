from __future__ import annotations

import ctypes
import subprocess
from dataclasses import dataclass


@dataclass(frozen=True)
class HostIpConfig:
    ip_address: str
    prefix_length: int
    netmask: str
    gateway: str
    dns: str


@dataclass(frozen=True)
class IpConfigResult:
    ok: bool
    message: str


def is_admin() -> bool:
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def configure_host_ipv4(config: HostIpConfig) -> IpConfigResult:
    if not is_admin():
        return IpConfigResult(
            False,
            "Administrator privileges are required to configure the host IPv4 address.",
        )

    script = f"""
$ErrorActionPreference = 'Stop'
$ip = '{config.ip_address}'
$prefix = {int(config.prefix_length)}
$netmask = '{config.netmask}'
$gateway = '{config.gateway}'
$dns = '{config.dns}'

$adapter = Get-NetAdapter -Physical |
    Where-Object {{
        $_.Status -eq 'Up' -and
        $_.InterfaceDescription -notmatch 'Wireless|Wi-Fi|WLAN|Bluetooth'
    }} |
    Select-Object -First 1

if (-not $adapter) {{
    $route = Get-NetRoute -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue |
        Sort-Object RouteMetric |
        Select-Object -First 1

    if ($route) {{
        $adapter = Get-NetAdapter -InterfaceIndex $route.InterfaceIndex -ErrorAction Stop
    }}
}}

if (-not $adapter) {{
    throw 'No enabled physical network adapter was found.'
}}

$ifIndex = $adapter.ifIndex
Set-NetIPInterface -InterfaceIndex $ifIndex -AddressFamily IPv4 -Dhcp Disabled

Get-NetIPAddress -InterfaceIndex $ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object {{ $_.IPAddress -ne $ip }} |
    Remove-NetIPAddress -Confirm:$false -ErrorAction SilentlyContinue

Get-NetRoute -InterfaceIndex $ifIndex -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue |
    Remove-NetRoute -Confirm:$false -ErrorAction SilentlyContinue

$existing = Get-NetIPAddress -InterfaceIndex $ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object {{ $_.IPAddress -eq $ip }} |
    Select-Object -First 1

if (-not $existing) {{
    New-NetIPAddress -InterfaceIndex $ifIndex -IPAddress $ip -PrefixLength $prefix -DefaultGateway $gateway | Out-Null
}}

$defaultRoute = Get-NetRoute -InterfaceIndex $ifIndex -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue |
    Select-Object -First 1

if (-not $defaultRoute) {{
    New-NetRoute -InterfaceIndex $ifIndex -DestinationPrefix '0.0.0.0/0' -NextHop $gateway | Out-Null
}}

Set-DnsClientServerAddress -InterfaceIndex $ifIndex -ServerAddresses $dns
"Host IPv4 configured on interface index $($ifIndex): $ip/$netmask, gateway $gateway, DNS $dns"
"""

    try:
        completed = subprocess.run(
            [
                "powershell.exe",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-Command",
                script,
            ],
            capture_output=True,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            encoding="utf-8",
            errors="replace",
            text=True,
            timeout=20,
        )
    except Exception as exc:
        return IpConfigResult(False, f"Host IPv4 configuration failed: {exc}")

    if completed.returncode != 0:
        return IpConfigResult(False, f"Host IPv4 configuration failed. Exit code: {completed.returncode}")
    output = (completed.stdout or "").strip()
    return IpConfigResult(True, output or "Host IPv4 configured.")
