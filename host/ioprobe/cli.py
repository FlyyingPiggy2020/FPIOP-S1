"""Command-line front end.

Conventions: I2C addresses and data bytes are hex ("50", "0x50", "9f 00 ab" or "9f00ab");
GPIO pins are J3 header numbers in decimal. Add --json for machine-readable output.

    ioprobe info
    ioprobe gpio config 13 out --initial 1
    ioprobe gpio read
    ioprobe i2c scan
    ioprobe i2c wr 50 2 00          # write 00, read 2 bytes from 0x50
    ioprobe spi xfer 9f 00 00 00 --cs 0
"""
from __future__ import annotations

import argparse
import json
import sys

from .api import Probe, ProbeError, ProbeTimeout
from .device import list_probes


def hexbytes(tokens: list[str]) -> bytes:
    s = "".join(t.lower().removeprefix("0x") for t in tokens).replace("_", "")
    if len(s) % 2:
        raise argparse.ArgumentTypeError(f"odd number of hex digits: {s!r}")
    return bytes.fromhex(s)


def hexint(s: str) -> int:
    return int(s, 16)


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="ioprobe", description="Drive the HPM5301 IO Probe (I2C / SPI / GPIO).")
    ap.add_argument("--port", help="serial port (default: auto-detect)")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list", help="list connected probes")
    sub.add_parser("info", help="firmware version and GPIO pins")
    p = sub.add_parser("ping", help="round-trip check")
    p.add_argument("data", nargs="*", default=[], help="hex bytes to echo")
    sub.add_parser("reset", help="reboot the probe")
    p = sub.add_parser("swd-mode", help="get/set the DAPLink SWD backend (applies at next debugger connect)")
    p.add_argument("mode", nargs="?", choices=["spi", "gpio"])

    gpio = sub.add_parser("gpio", help="GPIO on J3 pins").add_subparsers(dest="op", required=True)
    p = gpio.add_parser("config")
    p.add_argument("pin", type=int)
    p.add_argument("mode", choices=["in", "out", "od"])
    p.add_argument("--pull", choices=["none", "up", "down"], default="none")
    p.add_argument("--initial", type=int, choices=[0, 1], default=0)
    p = gpio.add_parser("write")
    p.add_argument("pin", type=int)
    p.add_argument("level", type=int, choices=[0, 1])
    p = gpio.add_parser("read", help="read one pin, or all probe GPIOs")
    p.add_argument("pin", type=int, nargs="?")

    i2c = sub.add_parser("i2c", help="I2C master (bus 0 = J3-3 SDA / J3-5 SCL)").add_subparsers(dest="op", required=True)
    for name in ("speed", "scan", "write", "read", "wr"):
        p = i2c.add_parser(name)
        p.add_argument("--bus", type=int, default=0)
        if name == "speed":
            p.add_argument("hz", type=int, choices=[100_000, 400_000, 1_000_000])
        if name in ("write", "read", "wr"):
            p.add_argument("addr", type=hexint, help="7-bit address, hex")
        if name in ("read", "wr"):
            p.add_argument("n", type=int, help="bytes to read")
        if name in ("write", "wr"):
            p.add_argument("data", nargs="+", help="hex bytes to write")

    spi = sub.add_parser("spi", help="SPI master (MOSI 19, MISO 21, SCLK 23, CS0 24, CS1 26)")
    spi = spi.add_subparsers(dest="op", required=True)
    p = spi.add_parser("config")
    p.add_argument("--mode", type=int, choices=[0, 1, 2, 3], default=0)
    p.add_argument("--freq", type=int, default=1_000_000, help="SCLK in Hz")
    p.add_argument("--lsb", action="store_true", help="LSB first")
    p = spi.add_parser("xfer", help="full-duplex transfer")
    p.add_argument("data", nargs="+", help="hex bytes to send")
    p.add_argument("--cs", default="0", help="0, 1 or none")
    p.add_argument("--keep-cs", action="store_true", help="leave CS asserted afterwards")
    p = spi.add_parser("cs", help="drive a chip select by hand")
    p.add_argument("cs", type=int, choices=[0, 1])
    p.add_argument("state", choices=["on", "off"])
    return ap


def run(args: argparse.Namespace, probe: Probe):
    """Execute one command; returns a JSON-serialisable result."""
    c = args.cmd
    if c == "info":
        i = probe.info()
        return {"protocol": i.protocol, "firmware": i.firmware, "max_payload": i.max_payload,
                "gpio_pins": list(i.gpio_pins)}
    if c == "ping":
        return {"echo": probe.ping(hexbytes(args.data)).hex()}
    if c == "reset":
        probe.reset()
        return {}
    if c == "swd-mode":
        return {"swd_mode": probe.swd_mode(args.mode)}
    if c == "gpio":
        if args.op == "config":
            probe.gpio.config(args.pin, args.mode, args.pull, args.initial)
            return {}
        if args.op == "write":
            probe.gpio.write(args.pin, args.level)
            return {}
        if args.pin is None:
            return {"levels": {str(k): v for k, v in probe.gpio.read_all().items()}}
        return {"pin": args.pin, "level": probe.gpio.read(args.pin)}
    if c == "i2c":
        if args.op == "speed":
            probe.i2c.config(args.hz, args.bus)
            return {}
        if args.op == "scan":
            return {"addresses": [f"0x{a:02x}" for a in probe.i2c.scan(args.bus)]}
        if args.op == "write":
            probe.i2c.write(args.addr, hexbytes(args.data), args.bus)
            return {}
        if args.op == "read":
            return {"data": probe.i2c.read(args.addr, args.n, args.bus).hex()}
        return {"data": probe.i2c.write_read(args.addr, hexbytes(args.data), args.n, args.bus).hex()}
    if c == "spi":
        if args.op == "config":
            probe.spi.config(args.mode, args.freq, args.lsb)
            return {}
        if args.op == "cs":
            probe.spi.cs(args.cs, args.state == "on")
            return {}
        cs = None if args.cs == "none" else int(args.cs)
        return {"rx": probe.spi.transfer(hexbytes(args.data), cs, args.keep_cs).hex()}
    raise AssertionError(c)


def format_human(result: dict) -> str:
    if not result:
        return "ok"
    lines = []
    for k, v in result.items():
        if k in ("data", "rx", "echo"):
            v = " ".join(v[i:i + 2] for i in range(0, len(v), 2)) or "(empty)"
        elif k == "addresses":
            v = " ".join(v) or "(none)"
        elif k == "levels":
            v = "  ".join(f"J3-{p}={lv}" for p, lv in v.items())
        lines.append(f"{k}: {v}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    out = (lambda r: print(json.dumps(r))) if args.json else (lambda r: print(format_human(r)))

    if args.cmd == "list":
        ports = list_probes()
        out({"ports": ports} if args.json else {"ports": " ".join(ports) or "(none)"})
        return 0
    try:
        with Probe.open(args.port) as probe:
            result = run(args, probe)
    except (ProbeError, ProbeTimeout, FileNotFoundError, RuntimeError, ValueError, argparse.ArgumentTypeError) as e:
        if args.json:
            err = {"ok": False, "error": str(e)}
            if isinstance(e, ProbeError):
                err["status"] = getattr(e.status, "name", e.status)
            print(json.dumps(err))
        else:
            print(f"error: {e}", file=sys.stderr)
        return 1
    out({"ok": True, **result} if args.json else result)
    return 0


if __name__ == "__main__":
    sys.exit(main())
