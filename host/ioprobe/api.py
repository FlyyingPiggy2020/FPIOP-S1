"""High-level Python API for the IO Probe.

    from ioprobe import Probe

    with Probe.open() as probe:
        print(probe.info())
        print(probe.i2c.scan())
        probe.gpio.config(13, "out", initial=1)
        rx = probe.spi.transfer(b"\\x9f\\x00\\x00\\x00", cs=0)
"""
from __future__ import annotations

import struct
import time
from dataclasses import dataclass

from .device import SerialTransport, Transport, find_probe
from .protocol import (MAX_PAYLOAD, MAX_RESP_DATA, SPI_CS_NONE, SPI_FLAG_KEEP_CS, Cmd, FrameParser, Status,
                       encode_request)

DEFAULT_TIMEOUT = 2.0


class ProbeError(Exception):
    """The probe answered with a non-OK status."""

    def __init__(self, cmd: int, status: int) -> None:
        self.cmd = Cmd(cmd) if cmd in Cmd._value2member_map_ else cmd
        self.status = Status(status) if status in Status._value2member_map_ else status
        name = self.status.name if isinstance(self.status, Status) else f"0x{status:02X}"
        cmd_name = self.cmd.name if isinstance(self.cmd, Cmd) else f"0x{cmd:02X}"
        super().__init__(f"{cmd_name} failed: {name}")


class ProbeTimeout(Exception):
    """No (valid) response arrived in time."""


@dataclass(frozen=True)
class ProbeInfo:
    protocol: int
    firmware: str
    max_payload: int
    gpio_pins: tuple[int, ...]  # J3 header numbers usable as GPIO


class Probe:
    def __init__(self, transport: Transport, timeout: float = DEFAULT_TIMEOUT) -> None:
        self._t = transport
        self._parser = FrameParser()
        self._seq = 0
        self.timeout = timeout
        self.gpio = Gpio(self)
        self.i2c = I2c(self)
        self.spi = Spi(self)

    @classmethod
    def open(cls, port: str | None = None, timeout: float = DEFAULT_TIMEOUT) -> "Probe":
        return cls(SerialTransport(port or find_probe()), timeout)

    def close(self) -> None:
        self._t.close()

    def __enter__(self) -> "Probe":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def call(self, cmd: int, payload: bytes = b"") -> bytes:
        """Send one request, wait for its response, return the data bytes (status stripped)."""
        self._seq = (self._seq + 1) & 0xFF
        self._t.write(encode_request(cmd, self._seq, payload))
        deadline = time.monotonic() + self.timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProbeTimeout(f"no response to {Cmd(cmd).name if cmd in Cmd._value2member_map_ else cmd}")
            for frame in self._parser.feed(self._t.read(min(remaining, 0.1))):
                # Stale replies (e.g. from an earlier timed-out call) are skipped by seq.
                if frame.is_response and frame.seq == self._seq and frame.cmd == cmd:
                    if frame.status != Status.OK:
                        raise ProbeError(cmd, frame.status)
                    return frame.data

    # --- system -----------------------------------------------------------
    def ping(self, data: bytes = b"") -> bytes:
        if len(data) > MAX_RESP_DATA:
            raise ValueError(f"at most {MAX_RESP_DATA} bytes can be echoed")
        return self.call(Cmd.PING, data)

    def info(self) -> ProbeInfo:
        d = self.call(Cmd.GET_INFO)
        n = d[6]
        return ProbeInfo(protocol=d[0], firmware=f"{d[1]}.{d[2]}.{d[3]}",
                         max_payload=d[4] | (d[5] << 8), gpio_pins=tuple(d[7:7 + n]))

    def reset(self) -> None:
        """Reboot the probe. The USB port disappears for ~1 s; reopen afterwards."""
        self.call(Cmd.RESET)


class Gpio:
    MODES = {"in": 0, "out": 1, "od": 2}
    PULLS = {"none": 0, "up": 1, "down": 2}

    def __init__(self, probe: Probe) -> None:
        self._p = probe

    def config(self, pin: int, mode: str = "in", pull: str = "none", initial: int = 0) -> None:
        """mode: "in" | "out" (push-pull) | "od" (open-drain); pull: "none" | "up" | "down"."""
        if mode not in self.MODES or pull not in self.PULLS:
            raise ValueError(f"mode must be one of {list(self.MODES)}, pull one of {list(self.PULLS)}")
        self._p.call(Cmd.GPIO_CONFIG, bytes([pin, self.MODES[mode], self.PULLS[pull], 1 if initial else 0]))

    def write(self, pin: int, level: int) -> None:
        self._p.call(Cmd.GPIO_WRITE, bytes([pin, 1 if level else 0]))

    def read(self, pin: int) -> int:
        """Actual pin level, also while the pin is driven as an output."""
        return self._p.call(Cmd.GPIO_READ, bytes([pin]))[0]

    def read_all(self) -> dict[int, int]:
        d = self._p.call(Cmd.GPIO_READ_ALL)
        return {d[i]: d[i + 1] for i in range(0, len(d), 2)}


class I2c:
    SPEEDS = {100_000: 0, 400_000: 1, 1_000_000: 2}

    def __init__(self, probe: Probe) -> None:
        self._p = probe

    def config(self, speed: int = 100_000, bus: int = 0) -> None:
        if speed not in self.SPEEDS:
            raise ValueError(f"speed must be one of {list(self.SPEEDS)}")
        self._p.call(Cmd.I2C_CONFIG, bytes([bus, self.SPEEDS[speed]]))

    def scan(self, bus: int = 0) -> list[int]:
        """7-bit addresses (0x08-0x77) that ACK."""
        return list(self._p.call(Cmd.I2C_SCAN, bytes([bus])))

    def write(self, addr: int, data: bytes, bus: int = 0) -> None:
        if not data:
            raise ValueError("data must not be empty (use scan() to probe an address)")
        self._p.call(Cmd.I2C_WRITE, bytes([bus, _addr7(addr)]) + bytes(data))

    def read(self, addr: int, n: int, bus: int = 0) -> bytes:
        return self._p.call(Cmd.I2C_READ, struct.pack("<BBH", bus, _addr7(addr), _rlen(n)))

    def write_read(self, addr: int, data: bytes, n: int, bus: int = 0) -> bytes:
        """Write data (e.g. a register address), repeated start, read n bytes."""
        if not data:
            raise ValueError("data must not be empty")
        return self._p.call(Cmd.I2C_WRITE_READ, struct.pack("<BBH", bus, _addr7(addr), _rlen(n)) + bytes(data))

    def read_reg(self, addr: int, reg: int, n: int = 1, bus: int = 0) -> bytes:
        """Read n bytes starting at an 8-bit register address."""
        return self.write_read(addr, bytes([reg]), n, bus)

    def write_reg(self, addr: int, reg: int, data: bytes, bus: int = 0) -> None:
        self.write(addr, bytes([reg]) + bytes(data), bus)


class Spi:
    def __init__(self, probe: Probe) -> None:
        self._p = probe

    def config(self, mode: int = 0, freq: int = 1_000_000, lsb_first: bool = False, bus: int = 0) -> None:
        if mode not in (0, 1, 2, 3) or freq <= 0:
            raise ValueError("mode must be 0-3 and freq > 0")
        self._p.call(Cmd.SPI_CONFIG, struct.pack("<BBBI", bus, mode, 1 if lsb_first else 0, freq))

    def transfer(self, data: bytes, cs: int | None = 0, keep_cs: bool = False, bus: int = 0) -> bytes:
        """Full-duplex: clock out data, return the same number of bytes read on MISO.

        cs: 0 or 1 to assert that chip select around the transfer, None to leave CS alone.
        keep_cs: leave CS asserted afterwards, to continue the transaction with another call.
        """
        if not data:
            raise ValueError("data must not be empty")
        if len(data) > MAX_PAYLOAD - 3:
            raise ValueError(f"at most {MAX_PAYLOAD - 3} bytes per transfer")
        flags = SPI_FLAG_KEEP_CS if keep_cs else 0
        return self._p.call(Cmd.SPI_TRANSFER, bytes([bus, SPI_CS_NONE if cs is None else cs, flags]) + bytes(data))

    def cs(self, cs: int, asserted: bool, bus: int = 0) -> None:
        self._p.call(Cmd.SPI_CS, bytes([bus, cs, 1 if asserted else 0]))


def _addr7(addr: int) -> int:
    if not 0 <= addr <= 0x7F:
        raise ValueError(f"I2C address must be 7-bit (0x00-0x7F), got 0x{addr:X}")
    return addr


def _rlen(n: int) -> int:
    if not 1 <= n <= MAX_RESP_DATA:
        raise ValueError(f"read length must be 1-{MAX_RESP_DATA}")
    return n
