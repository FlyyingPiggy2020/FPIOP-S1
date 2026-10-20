"""End-to-end checks against a real probe: pytest --hardware

Loopback tests are skipped unless the jumpers are fitted and the matching env var is set:
  IOPROBE_SPI_LOOP=1   J3-19 (MOSI) <-> J3-21 (MISO)
  IOPROBE_GPIO_LOOP=1  J3-27 <-> J3-32
  IOPROBE_UART_LOOP=1  J3-8 <-> J3-10 (target UART TX -> RX)
"""
import os

import pytest

import serial

from ioprobe import Probe, ProbeError, Status, find_probe, find_target_uart

pytestmark = pytest.mark.hardware


@pytest.fixture(scope="module")
def probe():
    with Probe.open() as p:
        yield p


def test_info(probe):
    info = probe.info()
    assert info.protocol == 1
    assert info.gpio_pins == (11, 27, 31, 32)


def test_ping_max_payload(probe):
    blob = (bytes(range(256)) * 16)[:4095]
    assert probe.ping(blob) == blob


def test_gpio_output_readback(probe):
    for pin in probe.info().gpio_pins:
        probe.gpio.config(pin, "out", initial=0)
        assert probe.gpio.read(pin) == 0, f"J3-{pin} stuck high"
        probe.gpio.write(pin, 1)
        assert probe.gpio.read(pin) == 1, f"J3-{pin} stuck low"
        probe.gpio.config(pin, "in", pull="down")


def test_i2c_scan_runs(probe):
    probe.i2c.config(100_000)
    assert all(0x08 <= a <= 0x77 for a in probe.i2c.scan())


def test_i2c_nack_on_empty_address(probe):
    if 0x3F in probe.i2c.scan():
        pytest.skip("0x3F is populated")
    with pytest.raises(ProbeError) as e:
        probe.i2c.read(0x3F, 1)
    assert e.value.status is Status.NACK


@pytest.mark.skipif(not os.environ.get("IOPROBE_SPI_LOOP"), reason="set IOPROBE_SPI_LOOP=1 with J3-19<->J3-21")
@pytest.mark.parametrize("mode", [0, 1, 2, 3])
def test_spi_loopback(probe, mode):
    probe.spi.config(mode=mode, freq=10_000_000)
    data = bytes(range(256)) * 4
    assert probe.spi.transfer(data, cs=0) == data


@pytest.mark.skipif(not os.environ.get("IOPROBE_GPIO_LOOP"), reason="set IOPROBE_GPIO_LOOP=1 with J3-27<->J3-32")
def test_gpio_loopback(probe):
    probe.gpio.config(32, "in")
    probe.gpio.config(27, "out", initial=0)
    assert probe.gpio.read(32) == 0
    probe.gpio.write(27, 1)
    assert probe.gpio.read(32) == 1
    probe.gpio.config(27, "in", pull="down")


def test_composite_ports_found():
    ctrl, uart = find_probe(), find_target_uart()
    assert ctrl != uart


@pytest.mark.skipif(not os.environ.get("IOPROBE_UART_LOOP"), reason="set IOPROBE_UART_LOOP=1 with J3-8<->J3-10")
@pytest.mark.parametrize("baud", [115200, 921600])
def test_target_uart_loopback(baud):
    data = bytes(range(256)) * 8
    with serial.Serial(find_target_uart(), baud, timeout=2) as s:
        s.reset_input_buffer()
        s.write(data)
        assert s.read(len(data)) == data


def test_swd_mode_switch(probe):
    assert probe.swd_mode() in ("spi", "gpio")
    assert probe.swd_mode("gpio") == "gpio"
    assert probe.swd_mode("spi") == "spi"
