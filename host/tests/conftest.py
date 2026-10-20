import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))  # for fake_probe


def pytest_addoption(parser):
    parser.addoption("--hardware", action="store_true", help="also run tests that need a connected IO Probe")


def pytest_collection_modifyitems(config, items):
    if config.getoption("--hardware"):
        return
    skip = pytest.mark.skip(reason="needs --hardware and a connected IO Probe")
    for item in items:
        if "hardware" in item.keywords:
            item.add_marker(skip)
