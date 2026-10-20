import pytest

from fake_probe import FakeTransport
from ioprobe import Probe, ProbeError, ProbeTimeout, Status
from ioprobe.protocol import Cmd, encode_response


@pytest.fixture
def fake():
    return FakeTransport(i2c_devices={0x50: bytearray(range(256)), 0x68: bytearray(16)})


@pytest.fixture
def probe(fake):
    return Probe(fake, timeout=0.3)


def last_payload(fake, cmd):
    return [p for c, _, p in fake.requests if c == cmd][-1]


def test_info(probe):
    info = probe.info()
    assert info.firmware == "0.6.0"
    assert info.max_payload == 4096
    assert 27 in info.gpio_pins and 13 not in info.gpio_pins


def test_ping_echo_large(probe):
    blob = bytes(range(256)) * 15
    assert probe.ping(blob) == blob


def test_ping_limit(probe):
    assert len(probe.ping(bytes(4095))) == 4095
    with pytest.raises(ValueError):
        probe.ping(bytes(4096))


def test_seq_increments(probe, fake):
    probe.ping()
    probe.ping()
    assert [s for _, s, _ in fake.requests] == [1, 2]


def test_response_split_into_small_chunks(probe, fake):
    fake.chunk = 3
    assert probe.ping(b"chunky") == b"chunky"


def test_stale_and_garbage_before_reply_are_ignored(probe, fake):
    fake.prefix = b"\x00junk" + encode_response(Cmd.PING, 0x99, 0, b"stale")
    assert probe.ping(b"fresh") == b"fresh"


def test_timeout(probe, fake):
    fake.mute = True
    with pytest.raises(ProbeTimeout):
        probe.ping()


def test_gpio_config_encoding(probe, fake):
    probe.gpio.config(27, "od", pull="up", initial=1)
    assert last_payload(fake, Cmd.GPIO_CONFIG) == bytes([27, 2, 1, 1])


def test_gpio_write_read(probe):
    probe.gpio.config(27, "out")
    probe.gpio.write(27, 1)
    assert probe.gpio.read(27) == 1
    assert probe.gpio.read_all()[27] == 1


def test_gpio_bad_pin_raises_bad_arg(probe):
    with pytest.raises(ProbeError) as e:
        probe.gpio.read(1)
    assert e.value.status is Status.BAD_ARG


def test_gpio_rejects_unknown_mode(probe):
    with pytest.raises(ValueError):
        probe.gpio.config(27, "output")


def test_i2c_scan(probe):
    assert probe.i2c.scan() == [0x50, 0x68]


def test_i2c_read_reg(probe, fake):
    assert probe.i2c.read_reg(0x50, 0x10, 4) == bytes([0x10, 0x11, 0x12, 0x13])
    assert last_payload(fake, Cmd.I2C_WRITE_READ) == bytes([0, 0x50, 4, 0, 0x10])


def test_i2c_write_reg_then_read(probe):
    probe.i2c.write_reg(0x68, 0x02, b"\xAB\xCD")
    assert probe.i2c.read_reg(0x68, 0x02, 2) == b"\xAB\xCD"


def test_i2c_nack(probe):
    with pytest.raises(ProbeError) as e:
        probe.i2c.read(0x3C, 1)
    assert e.value.status is Status.NACK
    assert "I2C_READ failed: NACK" in str(e.value)


@pytest.mark.parametrize("call", [
    lambda p: p.i2c.read(0x80, 1),
    lambda p: p.i2c.read(0x50, 0),
    lambda p: p.i2c.write(0x50, b""),
    lambda p: p.i2c.config(123),
])
def test_i2c_argument_validation(probe, call):
    with pytest.raises(ValueError):
        call(probe)


def test_i2c_speed_encoding(probe, fake):
    probe.i2c.config(400_000)
    assert last_payload(fake, Cmd.I2C_CONFIG) == bytes([0, 1])


def test_spi_config_encoding(probe, fake):
    probe.spi.config(mode=3, freq=10_000_000, lsb_first=True)
    assert fake.spi_cfg == (0, 3, 1, 10_000_000)


def test_spi_transfer_and_cs_flags(probe, fake):
    assert probe.spi.transfer(b"\x9f\x00\x00", cs=1, keep_cs=True) == b"\x9f\x00\x00"
    assert last_payload(fake, Cmd.SPI_TRANSFER)[:3] == bytes([0, 1, 0x01])
    probe.spi.transfer(b"\x01", cs=None)
    assert last_payload(fake, Cmd.SPI_TRANSFER)[:3] == bytes([0, 0xFF, 0])


def test_spi_transfer_size_limit(probe):
    probe.spi.transfer(bytes(4093))
    with pytest.raises(ValueError):
        probe.spi.transfer(bytes(4094))


def test_context_manager_closes(fake):
    with Probe(fake) as p:
        p.ping()
    assert fake.closed

