"""In-memory stand-in for the firmware, speaking the real wire protocol.

It behaves like the C command handlers closely enough to test the host library
without hardware: GPIO levels, a table of fake I2C devices, and SPI loopback.
"""
from __future__ import annotations

import struct

from ioprobe.protocol import Cmd, FrameParser, Status, encode_response

GPIO_PINS = (11, 13, 31, 32)


class FakeTransport:
    def __init__(self, i2c_devices: dict[int, bytearray] | None = None) -> None:
        self._parser = FrameParser()
        self._out = bytearray()
        self.requests: list[tuple[int, int, bytes]] = []  # (cmd, seq, payload) as received
        self.gpio = {p: 0 for p in GPIO_PINS}
        self.gpio_cfg: dict[int, tuple[int, int]] = {}
        self.i2c_devices = i2c_devices if i2c_devices is not None else {}
        self.spi_cfg = None
        self.chunk = None  # if set, read() returns at most this many bytes per call
        self.mute = False  # swallow requests without answering
        self.prefix = b""  # bytes emitted before the next response (garbage / stale frames)
        self.closed = False

    # Transport interface
    def write(self, data: bytes) -> None:
        for f in self._parser.feed(data):
            self.requests.append((f.cmd, f.seq, f.payload))
            if self.mute:
                continue
            status, resp = self._handle(f.cmd, f.payload)
            self._out += self.prefix + encode_response(f.cmd, f.seq, status, resp if status == Status.OK else b"")
            self.prefix = b""

    def read(self, timeout: float) -> bytes:
        n = len(self._out) if self.chunk is None else min(self.chunk, len(self._out))
        data = bytes(self._out[:n])
        del self._out[:n]
        return data

    def close(self) -> None:
        self.closed = True

    # Firmware behaviour
    def _handle(self, cmd: int, p: bytes) -> tuple[int, bytes]:
        if cmd == Cmd.PING:
            return Status.OK, p
        if cmd == Cmd.GET_INFO:
            return Status.OK, bytes([1, 0, 4, 0]) + struct.pack("<H", 4096) + bytes([len(GPIO_PINS), *GPIO_PINS])
        if cmd == Cmd.RESET:
            return Status.OK, b""
        if cmd in (Cmd.GPIO_CONFIG, Cmd.GPIO_WRITE, Cmd.GPIO_READ):
            if not p or p[0] not in self.gpio:
                return Status.BAD_ARG, b""
            if cmd == Cmd.GPIO_CONFIG:
                self.gpio_cfg[p[0]] = (p[1], p[2])
                if p[1] != 0:
                    self.gpio[p[0]] = p[3]
                return Status.OK, b""
            if cmd == Cmd.GPIO_WRITE:
                self.gpio[p[0]] = p[1]
                return Status.OK, b""
            return Status.OK, bytes([self.gpio[p[0]]])
        if cmd == Cmd.GPIO_READ_ALL:
            return Status.OK, b"".join(bytes([k, v]) for k, v in self.gpio.items())
        if cmd == Cmd.I2C_CONFIG:
            return (Status.OK if p[1] <= 2 else Status.BAD_ARG), b""
        if cmd == Cmd.I2C_SCAN:
            return Status.OK, bytes(sorted(a for a in self.i2c_devices if 0x08 <= a <= 0x77))
        if cmd in (Cmd.I2C_WRITE, Cmd.I2C_READ, Cmd.I2C_WRITE_READ):
            regs = self.i2c_devices.get(p[1])
            if regs is None:
                return Status.NACK, b""
            if cmd == Cmd.I2C_WRITE:
                reg, data = p[2], p[3:]
                regs[reg:reg + len(data)] = data
                return Status.OK, b""
            n = p[2] | (p[3] << 8)
            reg = p[4] if cmd == Cmd.I2C_WRITE_READ else 0
            return Status.OK, bytes(regs[reg:reg + n])
        if cmd == Cmd.SPI_CONFIG:
            self.spi_cfg = struct.unpack("<BBBI", p)
            return Status.OK, b""
        if cmd == Cmd.SPI_TRANSFER:
            return Status.OK, p[3:]  # MOSI looped back to MISO
        if cmd == Cmd.SPI_CS:
            return Status.OK, b""
        return Status.UNKNOWN_CMD, b""
