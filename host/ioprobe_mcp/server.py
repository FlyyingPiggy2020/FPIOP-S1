"""MCP server exposing the IO Probe to AI agents (I2C / SPI / GPIO on HPM5301EVKLite J3).

Every tool returns a dict with "ok". On success it carries the result fields; on failure
"error" holds the probe status name (e.g. "NACK", "TIMEOUT", "BAD_ARG", "NOT_CONNECTED")
and "detail" a human-readable message. "elapsed_ms" is the host-side round-trip time.
"""
from __future__ import annotations

import threading
import time
from typing import Any, Callable, Literal

from mcp.server.mcpserver import MCPServer

from ioprobe import Probe, ProbeError, ProbeTimeout

INSTRUCTIONS = """\
Hardware probe on an HPM5301EVKLite board. All IO is 3.3 V; the target must share GND with the board.
Pins are J3 header numbers. Fixed wiring:
- I2C master bus 0: SDA = J3-3, SCL = J3-5 (targets need pull-ups, ~4.7k to 3.3 V).
- SPI master bus 0: MOSI = J3-19, MISO = J3-21, SCLK = J3-23, CS0 = J3-24, CS1 = J3-26 (CS active low).
- GPIO: J3 13, 27, 28, 32.
- Also on the same USB device (not driven through these tools): a CMSIS-DAP SWD debugger for the target
  (SWCLK = J3-11, SWDIO = J3-29, nRESET = J3-31; use OpenOCD / pyOCD / probe-rs) and a serial port bridged to
  the target UART (TX = J3-8, RX = J3-10). gpio_read reports the real pin level even when the pin is an output.
Data bytes are hex strings like "9f 00 ab". I2C addresses are 7-bit integers (0x50 = 80).
A response carries at most 4095 data bytes. Errors come back as ok=false with error="NACK" etc.;
NACK on an I2C address means no device answered there.
"""

ProbeFactory = Callable[[], Probe]


class ProbeSession:
    """One shared probe connection: opened lazily, reopened after a timeout or unplug."""

    def __init__(self, factory: ProbeFactory) -> None:
        self._factory = factory
        self._probe: Probe | None = None
        self._lock = threading.Lock()

    def run(self, fn: Callable[[Probe], dict[str, Any] | None]) -> dict[str, Any]:
        start = time.perf_counter()
        with self._lock:
            try:
                if self._probe is None:
                    self._probe = self._factory()
                result = fn(self._probe) or {}
                out = {"ok": True, **result}
            except ProbeError as e:
                out = {"ok": False, "error": getattr(e.status, "name", str(e.status)), "detail": str(e)}
            except ValueError as e:
                out = {"ok": False, "error": "BAD_ARG", "detail": str(e)}
            except (ProbeTimeout, OSError, RuntimeError) as e:
                self._drop()
                code = "TIMEOUT" if isinstance(e, ProbeTimeout) else "NOT_CONNECTED"
                out = {"ok": False, "error": code, "detail": str(e)}
        out["elapsed_ms"] = round((time.perf_counter() - start) * 1000, 2)
        return out

    def _drop(self) -> None:
        if self._probe is not None:
            try:
                self._probe.close()
            except Exception:
                pass
            self._probe = None

    def close(self) -> None:
        with self._lock:
            self._drop()


def _hex(data: str) -> bytes:
    s = "".join(data.lower().replace("0x", "").replace(",", " ").split())
    if len(s) % 2:
        raise ValueError(f"odd number of hex digits in {data!r}")
    try:
        return bytes.fromhex(s)
    except ValueError:
        raise ValueError(f"not a hex byte string: {data!r}") from None


def _fmt(data: bytes) -> str:
    return data.hex(" ")


def create_server(factory: ProbeFactory | None = None) -> MCPServer:
    session = ProbeSession(factory or Probe.open)
    mcp = MCPServer(name="ioprobe", instructions=INSTRUCTIONS, version="0.2.0")

    # --- system -----------------------------------------------------------
    @mcp.tool()
    def probe_info() -> dict:
        """Firmware version, protocol version, max payload and the J3 pins usable as GPIO."""
        def go(p: Probe):
            i = p.info()
            return {"firmware": i.firmware, "protocol": i.protocol, "max_payload": i.max_payload,
                    "gpio_pins": list(i.gpio_pins)}
        return session.run(go)

    @mcp.tool()
    def probe_reset() -> dict:
        """Reboot the probe (all pins return to their safe defaults). The next call reconnects."""
        def go(p: Probe):
            p.reset()
            session._drop()
            time.sleep(1.5)  # USB re-enumeration
        return session.run(go)

    # --- gpio -------------------------------------------------------------
    @mcp.tool()
    def gpio_config(pin: int, mode: Literal["in", "out", "od"] = "in",
                    pull: Literal["none", "up", "down"] = "none", initial: int = 0) -> dict:
        """Configure a J3 pin as GPIO. mode: "in", "out" (push-pull) or "od" (open-drain).
        initial is the output level applied when switching to an output mode."""
        return session.run(lambda p: p.gpio.config(pin, mode, pull, initial))

    @mcp.tool()
    def gpio_write(pin: int, level: int) -> dict:
        """Drive an output pin (configured with gpio_config) to 0 or 1."""
        return session.run(lambda p: p.gpio.write(pin, level))

    @mcp.tool()
    def gpio_read(pin: int | None = None) -> dict:
        """Read one J3 pin's level, or all probe GPIOs when pin is omitted."""
        def go(p: Probe):
            if pin is None:
                return {"levels": {str(k): v for k, v in p.gpio.read_all().items()}}
            return {"pin": pin, "level": p.gpio.read(pin)}
        return session.run(go)

    # --- i2c --------------------------------------------------------------
    @mcp.tool()
    def i2c_config(speed_hz: Literal[100000, 400000, 1000000] = 100000) -> dict:
        """Set the I2C bus clock (bus 0, J3-3 SDA / J3-5 SCL)."""
        return session.run(lambda p: p.i2c.config(speed_hz))

    @mcp.tool()
    def i2c_scan() -> dict:
        """List 7-bit addresses (0x08-0x77) that acknowledge on the I2C bus."""
        def go(p: Probe):
            addrs = p.i2c.scan()
            return {"addresses": addrs, "addresses_hex": [f"0x{a:02x}" for a in addrs]}
        return session.run(go)

    @mcp.tool()
    def i2c_write(addr: int, data: str) -> dict:
        """Write bytes to a device: START, addr+W, data..., STOP. data is hex, e.g. "6b 00"
        (register 0x6B, value 0x00)."""
        return session.run(lambda p: p.i2c.write(addr, _hex(data)))

    @mcp.tool()
    def i2c_read(addr: int, length: int) -> dict:
        """Read length bytes from a device: START, addr+R, data..., STOP."""
        return session.run(lambda p: {"data": _fmt(p.i2c.read(addr, length))})

    @mcp.tool()
    def i2c_write_read(addr: int, data: str, length: int) -> dict:
        """Write then read with a repeated START, the usual register read: data="75", length=1
        reads register 0x75."""
        return session.run(lambda p: {"data": _fmt(p.i2c.write_read(addr, _hex(data), length))})

    # --- spi --------------------------------------------------------------
    @mcp.tool()
    def spi_config(mode: int = 0, freq_hz: int = 1000000, lsb_first: bool = False) -> dict:
        """Set SPI mode (0-3, CPOL/CPHA), SCLK frequency and bit order for bus 0."""
        return session.run(lambda p: p.spi.config(mode, freq_hz, lsb_first))

    @mcp.tool()
    def spi_transfer(data: str, cs: int | None = 0, keep_cs: bool = False) -> dict:
        """Full-duplex transfer: clock out data (hex) and return the bytes read on MISO.
        cs: 0 (J3-24) or 1 (J3-26) is asserted around the transfer; null leaves CS untouched.
        keep_cs=true leaves CS asserted so the next spi_transfer continues the same transaction."""
        return session.run(lambda p: {"rx": _fmt(p.spi.transfer(_hex(data), cs, keep_cs))})

    @mcp.tool()
    def spi_cs(cs: int, asserted: bool) -> dict:
        """Drive a chip select by hand (asserted=true pulls it low)."""
        return session.run(lambda p: p.spi.cs(cs, asserted))

    mcp.probe_session = session  # for tests
    return mcp


def main() -> None:
    create_server().run("stdio")


if __name__ == "__main__":
    main()
