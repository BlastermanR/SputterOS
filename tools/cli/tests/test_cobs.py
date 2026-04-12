"""Tests for COBS encoder/decoder.

Test vectors are shared with the C++ test suite (test_CobsCodec.cpp)
to ensure cross-implementation consistency.
"""

import pytest

from sputterctl.cobs import decode, encode


# ── Roundtrip tests ──────────────────────────────────────────────────


def _roundtrip(data: bytes) -> None:
    """Encode then decode and verify output matches input."""
    encoded = encode(data)
    # Encoded data must not contain 0x00.
    assert 0x00 not in encoded, f"encoded data contains 0x00: {encoded.hex()}"
    decoded = decode(encoded)
    assert decoded == data


class TestRoundtrip:
    def test_single_byte(self):
        _roundtrip(b"\x42")

    def test_two_bytes(self):
        _roundtrip(b"\x01\x02")

    def test_all_zeros(self):
        _roundtrip(b"\x00\x00\x00")

    def test_all_0xff(self):
        _roundtrip(bytes([0xFF] * 10))

    def test_zero_in_middle(self):
        _roundtrip(b"\x01\x00\x02")

    def test_longer_data(self):
        _roundtrip(bytes(range(256)))

    def test_254_non_zero_bytes(self):
        """254 = max run length before code byte is 0xFF."""
        _roundtrip(bytes([0x42] * 254))

    def test_255_non_zero_bytes(self):
        """255 bytes forces a block split."""
        _roundtrip(bytes([0x42] * 255))

    def test_mixed_data(self):
        _roundtrip(b"\x00\x01\x02\x00\x03\x00")


# ── Encode-specific tests ────────────────────────────────────────────


class TestEncode:
    def test_empty_input(self):
        encoded = encode(b"")
        assert encoded == b"\x01"

    def test_single_zero(self):
        encoded = encode(b"\x00")
        # 0x00 → code byte 0x01 (distance to next zero) + code byte 0x01
        assert 0x00 not in encoded


# ── Decode error handling ─────────────────────────────────────────────


class TestDecodeErrors:
    def test_empty_data(self):
        with pytest.raises(ValueError, match="empty"):
            decode(b"")

    def test_zero_byte_in_data(self):
        with pytest.raises(ValueError, match="unexpected zero"):
            decode(b"\x03\x01\x00\x02")
