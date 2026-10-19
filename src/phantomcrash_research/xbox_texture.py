"""Decode Original Xbox (NV2A) texture data to row-major RGBA8.

Covers the formats the game's 2D packs use: swizzled A8R8G8B8/X8R8G8B8, swizzled
8-bit palettised (P8) and the DXT1/DXT3/DXT5 block formats. Swizzled formats
store texels in Morton order: texel address bits interleave x and y, starting
with x, until the smaller dimension runs out; the rest belong to the larger one.
"""

from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache
import struct

# Format numbers from the D3D format field (bits 8-15 of the format word).
A8R8G8B8 = 0x06
X8R8G8B8 = 0x07
P8 = 0x0B
DXT1 = 0x0C
DXT3 = 0x0E
DXT5 = 0x0F
LINEAR_A8R8G8B8 = 0x12

_BYTES_PER_TEXEL = {A8R8G8B8: 4, X8R8G8B8: 4, P8: 1, LINEAR_A8R8G8B8: 4}
_BLOCK_BYTES = {DXT1: 8, DXT3: 16, DXT5: 16}
FORMAT_NAMES = {
    A8R8G8B8: "A8R8G8B8",
    X8R8G8B8: "X8R8G8B8",
    P8: "P8",
    DXT1: "DXT1",
    DXT3: "DXT3",
    DXT5: "DXT5",
    LINEAR_A8R8G8B8: "LIN_A8R8G8B8",
}


class TextureError(ValueError):
    """The texture uses an unknown format or its data is truncated."""


@dataclass(frozen=True)
class TextureFormat:
    kind: int
    width: int
    height: int
    levels: int

    @classmethod
    def from_word(cls, word: int) -> "TextureFormat":
        """Parse a D3D ``Format`` word (log2 sizes in bits 20-27, levels in 16-19)."""
        return cls(
            kind=(word >> 8) & 0xFF,
            width=1 << ((word >> 20) & 0xF),
            height=1 << ((word >> 24) & 0xF),
            levels=max(1, (word >> 16) & 0xF),
        )

    @property
    def name(self) -> str:
        return FORMAT_NAMES.get(self.kind, f"0x{self.kind:02x}")

    def level_size(self, level: int = 0) -> int:
        """Bytes used by one mip level."""
        width = max(1, self.width >> level)
        height = max(1, self.height >> level)
        if self.kind in _BLOCK_BYTES:
            return max(1, (width + 3) // 4) * max(1, (height + 3) // 4) * _BLOCK_BYTES[self.kind]
        if self.kind in _BYTES_PER_TEXEL:
            return width * height * _BYTES_PER_TEXEL[self.kind]
        raise TextureError(f"Unsupported texture format {self.name}")

    def total_size(self) -> int:
        return sum(self.level_size(level) for level in range(self.levels))


@lru_cache(maxsize=16)
def swizzle_order(width: int, height: int) -> tuple[int, ...]:
    """Swizzled texel index for each row-major texel of a power-of-two texture."""
    x_masks = []
    y_masks = []
    bit = 0
    x_bits = width.bit_length() - 1
    y_bits = height.bit_length() - 1
    x_used = y_used = 0
    while x_used < x_bits or y_used < y_bits:
        if x_used < x_bits:
            x_masks.append(1 << bit)
            bit += 1
            x_used += 1
        if y_used < y_bits:
            y_masks.append(1 << bit)
            bit += 1
            y_used += 1
    x_part = [sum(mask for index, mask in enumerate(x_masks) if x >> index & 1) for x in range(width)]
    order = []
    for y in range(height):
        y_part = sum(mask for index, mask in enumerate(y_masks) if y >> index & 1)
        order.extend(y_part | value for value in x_part)
    return tuple(order)


def _bgra_to_rgba(data: bytes, opaque: bool) -> bytes:
    out = bytearray(len(data))
    out[0::4] = data[2::4]
    out[1::4] = data[1::4]
    out[2::4] = data[0::4]
    out[3::4] = b"\xff" * (len(data) // 4) if opaque else data[3::4]
    return bytes(out)


def _unswizzle(data: bytes, width: int, height: int, texel_bytes: int) -> bytes:
    order = swizzle_order(width, height)
    if texel_bytes == 1:
        return bytes(data[index] for index in order)
    words = struct.unpack(f"<{width * height}I", data[: width * height * 4])
    return struct.pack(f"<{width * height}I", *(words[index] for index in order))


def _rgb565(value: int) -> tuple[int, int, int]:
    red = (value >> 11) & 0x1F
    green = (value >> 5) & 0x3F
    blue = value & 0x1F
    return (red << 3 | red >> 2, green << 2 | green >> 4, blue << 3 | blue >> 2)


def _colour_palette(c0: int, c1: int, allow_transparent: bool) -> list[tuple[int, int, int, int]]:
    r0, g0, b0 = _rgb565(c0)
    r1, g1, b1 = _rgb565(c1)
    colours = [(r0, g0, b0, 255), (r1, g1, b1, 255)]
    if c0 > c1 or not allow_transparent:
        colours.append(((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255))
        colours.append(((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255))
    else:
        colours.append(((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255))
        colours.append((0, 0, 0, 0))
    return colours


def _dxt5_alphas(a0: int, a1: int) -> list[int]:
    if a0 > a1:
        return [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    return [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]


def _decode_blocks(kind: int, width: int, height: int, data: bytes) -> bytes:
    out = bytearray(width * height * 4)
    block_bytes = _BLOCK_BYTES[kind]
    blocks_x = max(1, (width + 3) // 4)
    blocks_y = max(1, (height + 3) // 4)
    stride = width * 4
    offset = 0
    for block_y in range(blocks_y):
        for block_x in range(blocks_x):
            block = data[offset : offset + block_bytes]
            offset += block_bytes
            colour = block[block_bytes - 8 :]
            c0, c1, bits = struct.unpack("<HHI", colour)
            colours = _colour_palette(c0, c1, kind == DXT1)
            if kind == DXT5:
                alphas = _dxt5_alphas(block[0], block[1])
                alpha_bits = int.from_bytes(block[2:8], "little")
                alpha = [alphas[(alpha_bits >> (3 * i)) & 7] for i in range(16)]
            elif kind == DXT3:
                explicit = int.from_bytes(block[0:8], "little")
                alpha = [((explicit >> (4 * i)) & 0xF) * 17 for i in range(16)]
            else:
                alpha = None
            for row in range(4):
                y = block_y * 4 + row
                if y >= height:
                    break
                base = y * stride + block_x * 16
                for column in range(4):
                    if block_x * 4 + column >= width:
                        break
                    texel = row * 4 + column
                    red, green, blue, opacity = colours[(bits >> (2 * texel)) & 3]
                    position = base + column * 4
                    out[position : position + 4] = bytes(
                        (red, green, blue, opacity if alpha is None else alpha[texel])
                    )
    return bytes(out)


def decode_rgba(fmt: TextureFormat, data: bytes, palette: bytes | None = None) -> bytes:
    """Decode mip level 0 of ``data`` to RGBA8, rows top to bottom."""
    size = fmt.level_size(0)
    if len(data) < size:
        raise TextureError(f"{fmt.name} {fmt.width}x{fmt.height} needs {size} bytes, got {len(data)}")
    data = data[:size]
    if fmt.kind in _BLOCK_BYTES:
        return _decode_blocks(fmt.kind, fmt.width, fmt.height, data)
    if fmt.kind in (A8R8G8B8, X8R8G8B8):
        texels = _unswizzle(data, fmt.width, fmt.height, 4)
        return _bgra_to_rgba(texels, opaque=fmt.kind == X8R8G8B8)
    if fmt.kind == LINEAR_A8R8G8B8:
        return _bgra_to_rgba(data, opaque=False)
    if fmt.kind == P8:
        if palette is None or len(palette) < 1024:
            raise TextureError("P8 texture without a 256-entry palette")
        colours = _bgra_to_rgba(palette[:1024], opaque=False)
        indices = _unswizzle(data, fmt.width, fmt.height, 1)
        return b"".join(colours[4 * index : 4 * index + 4] for index in indices)
    raise TextureError(f"Unsupported texture format {fmt.name}")
