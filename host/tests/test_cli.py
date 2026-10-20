import json

import pytest

from fake_probe import FakeTransport
from ioprobe import Probe, cli


@pytest.fixture
def fake(monkeypatch):
    fake = FakeTransport(i2c_devices={0x50: bytearray(range(256))})
    monkeypatch.setattr(Probe, "open", classmethod(lambda cls, port=None, timeout=0.3: cls(fake, timeout)))
    return fake


@pytest.mark.parametrize("tokens, expected", [
    (["9f", "00", "ab"], b"\x9f\x00\xab"),
    (["9f00ab"], b"\x9f\x00\xab"),
    (["0x9F", "0X01"], b"\x9f\x01"),
    (["de_ad"], b"\xde\xad"),
])
def test_hexbytes(tokens, expected):
    assert cli.hexbytes(tokens) == expected


def test_hexbytes_odd_digits():
    with pytest.raises(Exception):
        cli.hexbytes(["abc"])


def test_i2c_scan_json(fake, capsys):
    assert cli.main(["--json", "i2c", "scan"]) == 0
    assert json.loads(capsys.readouterr().out) == {"ok": True, "addresses": ["0x50"]}


def test_i2c_wr_human(fake, capsys):
    assert cli.main(["i2c", "wr", "50", "3", "10"]) == 0
    assert capsys.readouterr().out.strip() == "data: 10 11 12"


def test_nack_is_error_exit(fake, capsys):
    assert cli.main(["--json", "i2c", "read", "3c", "1"]) == 1
    out = json.loads(capsys.readouterr().out)
    assert out["ok"] is False and out["status"] == "NACK"


def test_gpio_config_and_read_all(fake, capsys):
    assert cli.main(["gpio", "config", "27", "out", "--initial", "1"]) == 0
    assert cli.main(["--json", "gpio", "read"]) == 0
    levels = json.loads(capsys.readouterr().out.splitlines()[-1])["levels"]
    assert levels["27"] == 1


def test_spi_xfer_cs_none(fake, capsys):
    assert cli.main(["--json", "spi", "xfer", "01", "02", "--cs", "none"]) == 0
    assert json.loads(capsys.readouterr().out)["rx"] == "0102"
    assert fake.requests[-1][2][:3] == bytes([0, 0xFF, 0])

