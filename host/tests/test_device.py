"""Port discovery for the composite probe (control CDC + target-UART CDC), without hardware."""
from types import SimpleNamespace

import pytest

from ioprobe import device


def port(dev, interface=None, location=None, serial="IOPROBE-0001", vid=0x34B7, pid=0x5301, product="IO Probe CMSIS-DAP"):
    return SimpleNamespace(device=dev, interface=interface, location=location, serial_number=serial,
                           vid=vid, pid=pid, product=product)


@pytest.fixture
def ports(monkeypatch):
    found = []
    monkeypatch.setattr(device.list_ports, "comports", lambda: found)
    return found


def test_macos_names_sort_in_interface_order(ports):
    ports += [port("/dev/cu.usbmodemIOPROBE_00014"), port("/dev/cu.usbmodemIOPROBE_00012")]
    assert device.find_probe() == "/dev/cu.usbmodemIOPROBE_00012"
    assert device.find_target_uart() == "/dev/cu.usbmodemIOPROBE_00014"


def test_interface_names_win_over_order(ports):
    ports += [port("/dev/ttyACM0", interface="IO Probe Target UART"), port("/dev/ttyACM1", interface="IO Probe Control")]
    assert device.find_probe() == "/dev/ttyACM1"
    assert device.find_target_uart() == "/dev/ttyACM0"


def test_linux_location_interface_number(ports):
    ports += [port("/dev/ttyACM5", location="1-2:1.3"), port("/dev/ttyACM4", location="1-2:1.1")]
    assert device.find_probe() == "/dev/ttyACM4"
    assert device.find_target_uart() == "/dev/ttyACM5"


def test_other_devices_ignored(ports):
    ports += [port("/dev/cu.usbmodem1", vid=0x1366, pid=0x0105, product="J-Link"), port("/dev/cu.usbmodemIOPROBE_00012")]
    assert device.list_probes() == ["/dev/cu.usbmodemIOPROBE_00012"]


def test_firmware_010_single_port_still_works(ports):
    ports += [port("/dev/cu.usbmodemIOPROBE_00011", product="IO Probe")]
    assert device.find_probe() == "/dev/cu.usbmodemIOPROBE_00011"
    with pytest.raises(FileNotFoundError):
        device.find_target_uart()


def test_two_probes_need_explicit_port(ports):
    ports += [port("/dev/a2", serial="A"), port("/dev/a4", serial="A"), port("/dev/b2", serial="B"), port("/dev/b4", serial="B")]
    assert device.list_probes() == ["/dev/a2", "/dev/b2"]
    with pytest.raises(RuntimeError):
        device.find_probe()


def test_none_connected(ports):
    with pytest.raises(FileNotFoundError):
        device.find_probe()
