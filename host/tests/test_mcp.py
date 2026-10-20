"""MCP server tests: real MCP client talking in-process to the server, backed by the fake firmware."""
import asyncio
import json

import pytest

pytest.importorskip("mcp")

from fake_probe import FakeTransport
from ioprobe import Probe
from ioprobe_mcp import create_server
from mcp import Client

EXPECTED_TOOLS = {"probe_info", "probe_reset", "gpio_config", "gpio_write", "gpio_read", "i2c_config",
                  "i2c_scan", "i2c_write", "i2c_read", "i2c_write_read", "spi_config", "spi_transfer", "spi_cs"}


@pytest.fixture
def fake():
    return FakeTransport(i2c_devices={0x68: bytearray(range(128))})


def call(server, tool, **args):
    async def go():
        async with Client(server) as c:
            r = await c.call_tool(tool, args)
            assert not r.is_error, r
            return json.loads(r.content[0].text)
    return asyncio.run(go())


@pytest.fixture
def server(fake):
    return create_server(lambda: Probe(fake, timeout=0.3))


def test_lists_all_tools(server):
    async def go():
        async with Client(server) as c:
            return {t.name for t in (await c.list_tools()).tools}
    assert asyncio.run(go()) == EXPECTED_TOOLS


def test_probe_info(server):
    r = call(server, "probe_info")
    assert r["ok"] and r["firmware"] == "0.2.1" and r["gpio_pins"] == [13, 27, 28, 32]
    assert "elapsed_ms" in r


def test_i2c_scan_and_register_read(server):
    assert call(server, "i2c_scan")["addresses_hex"] == ["0x68"]
    assert call(server, "i2c_write_read", addr=0x68, data="75", length=2)["data"] == "75 76"


def test_i2c_write_hex_formats(server, fake):
    assert call(server, "i2c_write", addr=0x68, data="0x10,0xAB 0xcd")["ok"]
    assert fake.i2c_devices[0x68][0x10:0x12] == b"\xab\xcd"


def test_i2c_nack_is_structured_error(server):
    r = call(server, "i2c_read", addr=0x3C, length=1)
    assert r == {"ok": False, "error": "NACK", "detail": "I2C_READ failed: NACK", "elapsed_ms": r["elapsed_ms"]}


def test_bad_hex_is_bad_arg(server):
    r = call(server, "spi_transfer", data="9g")
    assert r["ok"] is False and r["error"] == "BAD_ARG"


def test_gpio_roundtrip(server):
    assert call(server, "gpio_config", pin=13, mode="out", initial=1)["ok"]
    assert call(server, "gpio_read", pin=13)["level"] == 1
    assert call(server, "gpio_write", pin=13, level=0)["ok"]
    assert call(server, "gpio_read")["levels"]["13"] == 0


def test_spi_transfer_cs_null(server, fake):
    r = call(server, "spi_transfer", data="9f 00 00", cs=None, keep_cs=True)
    assert r["rx"] == "9f 00 00"
    assert fake.requests[-1][2][:3] == bytes([0, 0xFF, 0x01])


def test_reconnects_after_timeout():
    fakes = []

    def factory():
        fakes.append(FakeTransport())
        return Probe(fakes[-1], timeout=0.2)

    server = create_server(factory)
    assert call(server, "probe_info")["ok"]
    fakes[0].mute = True
    assert call(server, "probe_info")["error"] == "TIMEOUT"
    assert fakes[0].closed
    assert call(server, "probe_info")["ok"]  # fresh connection
    assert len(fakes) == 2


def test_not_connected_when_no_probe():
    def factory():
        raise FileNotFoundError("no IO Probe found")

    r = call(create_server(factory), "i2c_scan")
    assert r["ok"] is False and r["error"] == "NOT_CONNECTED"
