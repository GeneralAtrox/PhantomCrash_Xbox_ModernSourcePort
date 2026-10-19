"""MechAssault's ``.tif`` textures and ``.geomVert``/``.geomFace`` buffers.

Both are ``MGIc`` containers: 32 bytes of header, then a chunk named for the
content (``PIC ``, ``VERS``, ``INDX``) holding sub-chunks of (tag, size
including this 8-byte head, payload).

A ``PIC `` carries ``flgs``, ``wdth``, ``hgt ``, ``mips``, ``size`` and the
``bits`` of every mip, largest first. The flags give the pixel format:
16 is A8R8G8B8, 17 is R5G6B5, 19 is A4R4G4B4 and 256 is DXT1. The first three are Xbox
Morton-swizzled; DXT1 is linear.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct

FORMAT_ARGB8888 = 16
FORMAT_RGB565 = 17
FORMAT_ARGB4444 = 19
FORMAT_DXT1 = 256
_HEADER = 32
_CHUNK_HEAD = 16


def chunks(data: bytes) -> dict[str, bytes]:
    """The sub-chunks of an MGIc container's one top-level chunk."""
    if data[4:8] != b"MGIc":
        raise ValueError("not an MGIc container")
    body = data[_HEADER:]
    position = _CHUNK_HEAD
    out: dict[str, bytes] = {}
    while position + 8 <= len(body):
        tag = body[position : position + 4].decode("latin1")
        size = struct.unpack_from("<I", body, position + 4)[0]
        if size < 8:
            break
        out[tag] = body[position + 8 : position + size]
        position += size
    return out


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    rgba: bytes


def _morton_table(width: int, height: int) -> list[int]:
    x_bits = (width - 1).bit_length()
    y_bits = (height - 1).bit_length()
    table = []
    for y in range(height):
        for x in range(width):
            index = 0
            shift = 0
            for bit in range(max(x_bits, y_bits)):
                if bit < x_bits:
                    index |= ((x >> bit) & 1) << shift
                    shift += 1
                if bit < y_bits:
                    index |= ((y >> bit) & 1) << shift
                    shift += 1
            table.append(index)
    return table


def _565(value: int) -> tuple[int, int, int]:
    return (
        (value >> 11 & 31) * 255 // 31,
        (value >> 5 & 63) * 255 // 63,
        (value & 31) * 255 // 31,
    )


def _dxt1(data: bytes, width: int, height: int) -> bytes:
    out = bytearray(width * height * 4)
    for block_y in range(height // 4):
        for block_x in range(width // 4):
            colour0, colour1, bits = struct.unpack_from(
                "<HHI", data, (block_y * (width // 4) + block_x) * 8
            )
            c0, c1 = _565(colour0), _565(colour1)
            if colour0 > colour1:
                palette = [
                    (*c0, 255),
                    (*c1, 255),
                    (*((2 * a + b) // 3 for a, b in zip(c0, c1)), 255),
                    (*((a + 2 * b) // 3 for a, b in zip(c0, c1)), 255),
                ]
            else:
                palette = [
                    (*c0, 255),
                    (*c1, 255),
                    (*((a + b) // 2 for a, b in zip(c0, c1)), 255),
                    (0, 0, 0, 0),
                ]
            for row in range(4):
                for column in range(4):
                    pixel = palette[bits >> (2 * (row * 4 + column)) & 3]
                    start = ((block_y * 4 + row) * width + block_x * 4 + column) * 4
                    out[start : start + 4] = bytes(pixel)
    return bytes(out)


def decode_texture(data: bytes) -> Image:
    parts = chunks(data)
    flags, width, height = (struct.unpack("<I", parts[k])[0] for k in ("flgs", "wdth", "hgt "))
    bits = parts["bits"]
    if flags == FORMAT_DXT1:
        return Image(width, height, _dxt1(bits, width, height))
    order = _morton_table(width, height)
    out = bytearray(width * height * 4)
    if flags == FORMAT_ARGB8888:
        for pixel, source in enumerate(order):
            b, g, r, a = bits[source * 4 : source * 4 + 4]
            out[pixel * 4 : pixel * 4 + 4] = bytes((r, g, b, a))
    elif flags == FORMAT_RGB565:
        for pixel, source in enumerate(order):
            r, g, b = _565(bits[source * 2] | bits[source * 2 + 1] << 8)
            out[pixel * 4 : pixel * 4 + 4] = bytes((r, g, b, 255))
    elif flags == FORMAT_ARGB4444:
        for pixel, source in enumerate(order):
            value = bits[source * 2] | bits[source * 2 + 1] << 8
            a, r, g, b = (value >> 12 & 15, value >> 8 & 15, value >> 4 & 15, value & 15)
            out[pixel * 4 : pixel * 4 + 4] = bytes((r * 17, g * 17, b * 17, a * 17))
    else:
        raise ValueError(f"unknown texture format flags {flags}")
    return Image(width, height, bytes(out))


@dataclass(frozen=True)
class Geometry:
    """Vertices as (x, y, z, nx, ny, nz, u, v) and triangle indices."""

    vertices: list[tuple[float, ...]]
    indices: list[int]


def decode_geometry(vertex_data: bytes, face_data: bytes) -> Geometry:
    vertex_parts = chunks(vertex_data)
    count = struct.unpack_from("<I", vertex_parts["info"], 4)[0]
    raw = vertex_parts["data"]
    if count * 32 > len(raw):
        raise ValueError("vertex buffer is smaller than its count")
    vertices = [struct.unpack_from("<8f", raw, 32 * i) for i in range(count)]
    face_parts = chunks(face_data)
    index_count = struct.unpack("<I", face_parts["nidx"])[0]
    indices = list(struct.unpack_from(f"<{index_count}H", face_parts["data"]))
    return Geometry(vertices, indices)
