import pytest

from ioprobe.protocol import (MAX_PAYLOAD, SOF, FrameParser, crc16, encode_request,
                              encode_response)


def test_crc16_ccitt_false_check_value():
    # Standard check value for CRC-16/CCITT-FALSE
    assert crc16(b"123456789") == 0x29B1
    assert crc16(b"") == 0xFFFF


def test_request_layout():
    frame = encode_request(0x22, 7, b"\x00\x50\xAA")
    assert frame[:5] == bytes([SOF, 0x22, 7, 3, 0])
    assert frame[5:8] == b"\x00\x50\xAA"
    assert int.from_bytes(frame[8:], "little") == crc16(frame[1:8])


def test_seq_wraps_to_one_byte():
    assert encode_request(0x01, 0x1FF)[2] == 0xFF


def test_payload_limit():
    encode_request(0x01, 1, bytes(MAX_PAYLOAD))
    with pytest.raises(ValueError):
        encode_request(0x01, 1, bytes(MAX_PAYLOAD + 1))


def test_response_roundtrip():
    frames = FrameParser().feed(encode_response(0x23, 9, 0, b"\x12\x34"))
    assert len(frames) == 1
    f = frames[0]
    assert (f.cmd, f.seq, f.is_response, f.status, f.data) == (0x23, 9, True, 0, b"\x12\x34")


def test_request_parses_as_non_response():
    (f,) = FrameParser().feed(encode_request(0x13, 2))
    assert (f.cmd, f.is_response, f.payload) == (0x13, False, b"")


def test_byte_by_byte_feed():
    raw = encode_response(0x31, 1, 0, bytes(range(200)))
    parser = FrameParser()
    frames = []
    for b in raw:
        frames += parser.feed(bytes([b]))
    assert len(frames) == 1 and frames[0].data == bytes(range(200))


def test_several_frames_in_one_chunk():
    raw = b"".join(encode_response(0x01, s, 0, bytes([s])) for s in range(5))
    frames = FrameParser().feed(raw)
    assert [f.seq for f in frames] == [0, 1, 2, 3, 4]


def test_garbage_before_frame_is_skipped():
    frames = FrameParser().feed(b"\x00\xff junk \x13" + encode_response(0x01, 3, 0, b"ok"))
    assert len(frames) == 1 and frames[0].data == b"ok"


def test_bad_crc_dropped_and_parser_recovers():
    bad = bytearray(encode_response(0x01, 1, 0, b"abc"))
    bad[-1] ^= 0xFF
    parser = FrameParser()
    frames = parser.feed(bytes(bad) + encode_response(0x01, 2, 0, b"def"))
    assert [f.seq for f in frames] == [2]
    assert parser.dropped == 1


def test_sof_byte_inside_payload_does_not_confuse_parser():
    payload = bytes([SOF, SOF, 0x05, 0x00, SOF])
    frames = FrameParser().feed(encode_response(0x01, 4, 0, payload))
    assert frames[0].data == payload


def test_false_sof_with_huge_length_resyncs():
    # A stray SOF followed by a length bigger than any legal frame must not stall the parser.
    frames = FrameParser().feed(bytes([SOF, 0x01, 0x00, 0xFF, 0xFF]) + encode_response(0x02, 5, 0, b"x"))
    assert [f.seq for f in frames] == [5]


def test_partial_frame_waits_for_rest():
    raw = encode_response(0x01, 1, 0, b"hello")
    parser = FrameParser()
    assert parser.feed(raw[:6]) == []
    assert len(parser.feed(raw[6:])) == 1
