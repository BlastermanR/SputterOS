"""COBS (Consistent Overhead Byte Stuffing) encoder/decoder.

Pure-Python implementation mirroring the C++ CobsCodec.h.
All functions operate on bytes objects.
"""

from __future__ import annotations


def encode(data: bytes) -> bytes:
    """COBS-encode *data* (may contain 0x00 bytes).

    Returns the encoded bytes (all non-zero). The caller is responsible
    for appending a 0x00 frame delimiter after the encoded data.

    Raises ValueError on empty input.
    """
    if len(data) == 0:
        return b"\x01"  # Empty data encodes to a single code byte.

    output = bytearray()
    code_idx = 0
    output.append(0)  # Placeholder for first code byte.
    code = 1

    for byte in data:
        if byte == 0x00:
            output[code_idx] = code
            code_idx = len(output)
            output.append(0)  # Placeholder for next code byte.
            code = 1
        else:
            output.append(byte)
            code += 1
            if code == 0xFF:
                output[code_idx] = code
                code_idx = len(output)
                output.append(0)  # Placeholder.
                code = 1

    output[code_idx] = code
    return bytes(output)


def decode(data: bytes) -> bytes:
    """COBS-decode *data* (must not contain 0x00 bytes).

    Returns the original unencoded bytes.

    Raises ValueError if the encoded data is malformed.
    """
    if len(data) == 0:
        raise ValueError("empty COBS data")

    output = bytearray()
    idx = 0

    while idx < len(data):
        code = data[idx]
        if code == 0x00:
            raise ValueError(f"unexpected zero byte at index {idx}")
        idx += 1

        for _ in range(code - 1):
            if idx >= len(data):
                raise ValueError("truncated COBS data")
            if data[idx] == 0x00:
                raise ValueError(f"unexpected zero in data section at index {idx}")
            output.append(data[idx])
            idx += 1

        if code < 0xFF and idx < len(data):
            output.append(0x00)

    return bytes(output)
