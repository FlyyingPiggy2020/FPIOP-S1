"""Finding and talking to the probe over its USB CDC serial port."""
from __future__ import annotations

from typing import Protocol

import serial
from serial.tools import list_ports

USB_VID = 0x34B7
USB_PID = 0x5301
USB_PRODUCT = "IO Probe"

# Since firmware 0.2.0 the probe is a composite device with two CDC ports
# (plus a CMSIS-DAP interface that is not a serial port).
CTRL_INTERFACE = "IO Probe Control"
UART_INTERFACE = "IO Probe Target UART"
CTRL_INTF_NUM = 1
UART_INTF_NUM = 3


class Transport(Protocol):
    def write(self, data: bytes) -> None: ...

    def read(self, timeout: float) -> bytes:
        """Return whatever bytes arrive within timeout (possibly b"")."""
        ...

    def close(self) -> None: ...


def _role(port) -> str | None:
    """'ctrl' or 'uart' for one of the probe's serial ports, None if it can't be told."""
    if port.interface == CTRL_INTERFACE:
        return "ctrl"
    if port.interface == UART_INTERFACE:
        return "uart"
    loc = port.location or ""
    if ":" in loc:  # Linux/Windows style "1-2.3:1.<interface>"
        intf = loc.rsplit(".", 1)[-1]
        if intf.isdigit():
            return {CTRL_INTF_NUM: "ctrl", UART_INTF_NUM: "uart"}.get(int(intf))
    return None


def _probe_ports() -> dict[str, dict[str, str]]:
    """{usb serial number: {'ctrl': path, 'uart': path}} for every connected probe."""
    groups: dict[str, list] = {}
    for p in list_ports.comports():
        if (p.vid, p.pid) == (USB_VID, USB_PID) or (p.product or "").startswith(USB_PRODUCT):
            groups.setdefault(p.serial_number or p.device, []).append(p)
    result = {}
    for serial_no, ports in groups.items():
        ports.sort(key=lambda p: p.device)
        roles = {}
        for p in ports:
            role = _role(p)
            if role:
                roles[role] = p.device
        # macOS often reports neither interface name nor number: the ports then sort in interface
        # order (the device name ends in the interface number), control first.
        unassigned = [p.device for p in ports if p.device not in roles.values()]
        for role in ("ctrl", "uart"):
            if role not in roles and unassigned:
                roles[role] = unassigned.pop(0)
        result[serial_no] = roles
    return result


def list_probes() -> list[str]:
    """Control-port paths of every connected IO Probe."""
    return sorted(r["ctrl"] for r in _probe_ports().values() if "ctrl" in r)


def _find(role: str) -> str:
    probes = [r for r in _probe_ports().values() if role in r]
    if not probes:
        raise FileNotFoundError("no IO Probe found (USB VID:PID 34B7:5301). Is the board's USB-C port connected?")
    if len(probes) > 1:
        raise RuntimeError(f"several IO Probes found, pick one with port=...: {', '.join(r[role] for r in probes)}")
    return probes[0][role]


def find_probe() -> str:
    """Serial port of the IO Probe control protocol."""
    return _find("ctrl")


def find_target_uart() -> str:
    """Serial port bridged to the target board's UART (J3-8 TX / J3-10 RX)."""
    return _find("uart")


class SerialTransport:
    def __init__(self, port: str) -> None:
        # Baud rate is ignored by USB CDC; the link runs at USB speed.
        self._ser = serial.Serial(port, timeout=0)
        self.port = port

    def write(self, data: bytes) -> None:
        self._ser.write(data)

    def read(self, timeout: float) -> bytes:
        self._ser.timeout = timeout
        first = self._ser.read(1)
        if not first:
            return b""
        return first + self._ser.read(self._ser.in_waiting)

    def close(self) -> None:
        self._ser.close()
