"""IO Probe wire protocol. Mirrors src/protocol.h in the firmware; keep the two in sync.

Request : SOF | cmd | seq | len(u16 LE) | payload | crc16(u16 LE)
Response: SOF | cmd|0x80 | seq | len(u16 LE) | status | data | crc16(u16 LE)

crc16 = CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over cmd..payload.
"""
from __future__ import annotations

import binascii
import enum
import struct
from dataclasses import dataclass

PROTO_VERSION = 1
SOF = 0xA5
RESP_FLAG = 0x80
HDR_LEN = 5
CRC_LEN = 2
MAX_PAYLOAD = 4096                # request payload / response status+data
MAX_RESP_DATA = MAX_PAYLOAD - 1  # response data after the status byte


class Cmd(enum.IntEnum):
    PING = 0x01
    GET_INFO = 0x02
    RESET = 0x03

    GPIO_CONFIG = 0x10
    GPIO_WRITE = 0x11
    GPIO_READ = 0x12
    GPIO_READ_ALL = 0x13

    I2C_CONFIG = 0x20
    I2C_SCAN = 0x21
    I2C_WRITE = 0x22
    I2C_READ = 0x23
    I2C_WRITE_READ = 0x24

    SPI_CONFIG = 0x30
    SPI_TRANSFER = 0x31
    SPI_CS = 0x32


class Status(enum.IntEnum):
    OK = 0
    BAD_CRC = 1
    UNKNOWN_CMD = 2
    BAD_ARG = 3
    NACK = 4
    TIMEOUT = 5
    BUS_BUSY = 6
    IO_ERROR = 7
    TOO_LONG = 8


SPI_CS_NONE = 0xFF
SPI_FLAG_KEEP_CS = 0x01


def crc16(data: bytes) -> int:
    # binascii.crc_hqx is CRC-16 poly 0x1021, MSB first, no final XOR: with init 0xFFFF that is
    # CRC-16/CCITT-FALSE, computed in C (the pure-Python loop cost ~10 ms per 4 KB round trip).
    return binascii.crc_hqx(data, 0xFFFF)


def encode_request(cmd: int, seq: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload too long: {len(payload)} > {MAX_PAYLOAD}")
    body = struct.pack("<BBH", cmd, seq & 0xFF, len(payload)) + payload
    return bytes([SOF]) + body + struct.pack("<H", crc16(body))


def encode_response(cmd: int, seq: int, status: int, data: bytes = b"") -> bytes:
    """Build a device->host frame. Used by tests and fakes; the firmware does this in C."""
    payload = bytes([status]) + data
    body = struct.pack("<BBH", cmd | RESP_FLAG, seq & 0xFF, len(payload)) + payload
    return bytes([SOF]) + body + struct.pack("<H", crc16(body))


@dataclass(frozen=True)
class Frame:
    cmd: int  # with RESP_FLAG stripped for responses
    seq: int
    payload: bytes
    is_response: bool

    @property
    def status(self) -> int:
        return self.payload[0]

    @property
    def data(self) -> bytes:
        return self.payload[1:]


class FrameParser:
    """Incremental parser: feed() arbitrary chunks, get back complete, CRC-checked frames.

    Garbage before a SOF and frames with a bad CRC or oversize length are dropped,
    and parsing resyncs on the next SOF byte.
    """

    def __init__(self) -> None:
        self._buf = bytearray()
        self.dropped = 0  # frames discarded for bad CRC / length

    def feed(self, data: bytes) -> list[Frame]:
        self._buf += data
        frames = []
        while True:
            start = self._buf.find(SOF)
            if start < 0:
                self._buf.clear()
                break
            del self._buf[:start]
            if len(self._buf) < HDR_LEN:
                break
            length = self._buf[3] | (self._buf[4] << 8)
            if length > MAX_PAYLOAD + 1:
                self.dropped += 1
                del self._buf[0]
                continue
            total = HDR_LEN + length + CRC_LEN
            if len(self._buf) < total:
                break
            raw = bytes(self._buf[:total])
            got_crc = raw[-2] | (raw[-1] << 8)
            if crc16(raw[1:-2]) != got_crc:
                self.dropped += 1
                del self._buf[0]  # resync: the real SOF may be inside this candidate
                continue
            del self._buf[:total]
            cmd = raw[1]
            frames.append(Frame(cmd=cmd & ~RESP_FLAG & 0xFF, seq=raw[2], payload=raw[HDR_LEN:-2],
                                is_response=bool(cmd & RESP_FLAG)))
        return frames
