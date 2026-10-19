"""Minimal PNG encoder and decoder for 8-bit RGBA images (standard library only)."""

from __future__ import annotations

from pathlib import Path
import struct
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _chunk(kind: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)


def encode_rgba(width: int, height: int, rgba: bytes, level: int = 6) -> bytes:
    stride = width * 4
    if len(rgba) != stride * height:
        raise ValueError(f"Expected {stride * height} bytes of RGBA, got {len(rgba)}")
    raw = b"".join(b"\0" + rgba[row * stride : (row + 1) * stride] for row in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (
        SIGNATURE
        + _chunk(b"IHDR", header)
        + _chunk(b"IDAT", zlib.compress(raw, level))
        + _chunk(b"IEND", b"")
    )


def write_rgba(path: Path, width: int, height: int, rgba: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encode_rgba(width, height, rgba))


def _paeth(left: int, up: int, up_left: int) -> int:
    estimate = left + up - up_left
    distance_left = abs(estimate - left)
    distance_up = abs(estimate - up)
    distance_up_left = abs(estimate - up_left)
    if distance_left <= distance_up and distance_left <= distance_up_left:
        return left
    return up if distance_up <= distance_up_left else up_left


def decode_rgba(data: bytes) -> tuple[int, int, bytes]:
    """Decode a non-interlaced 8-bit RGBA PNG (the files this package writes)."""
    if not data.startswith(SIGNATURE):
        raise ValueError("Not a PNG file")
    offset = len(SIGNATURE)
    compressed = b""
    width = height = 0
    while offset < len(data):
        length, kind = struct.unpack_from(">I4s", data, offset)
        payload = data[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", payload)
            if (depth, colour, interlace) != (8, 6, 0):
                raise ValueError("Only 8-bit non-interlaced RGBA PNGs are supported")
        elif kind == b"IDAT":
            compressed += payload
        elif kind == b"IEND":
            break
    raw = zlib.decompress(compressed)
    stride = width * 4
    out = bytearray(stride * height)
    previous = bytearray(stride)
    for row in range(height):
        start = row * (stride + 1)
        method = raw[start]
        line = bytearray(raw[start + 1 : start + 1 + stride])
        for index in range(stride):
            left = line[index - 4] if index >= 4 else 0
            up = previous[index]
            up_left = previous[index - 4] if index >= 4 else 0
            predictor = (0, left, up, (left + up) // 2, _paeth(left, up, up_left))[method]
            line[index] = (line[index] + predictor) & 0xFF
        out[row * stride : (row + 1) * stride] = line
        previous = line
    return width, height, bytes(out)
