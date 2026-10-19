"""Reader for S.L.A.I.'s ``P2IG`` images (``.PIG``).

Layout, found by inspection (names are researcher-assigned):

- ``P2IG``, u32 0x61, u32, u16 0x0e, u16 1, an 8-byte name (``S3A02H01``),
  8 bytes; at +0x20 u16 log2 width, u16 log2 height, u16 GS pixel format
  (0x13 PSMT8, 0x14 PSMT4); at +0x40 u32 palette offset, palette size,
  pixel offset, pixel size.
- The palette is RGBA8888 with alpha 0..0x80, in the GS's CSM1 order
  (entries 8-15 and 16-23 of every 32 swapped). PSMT8 pixels are stored in
  the GS's swizzled order (uploaded as 32-bit words).
"""

from __future__ import annotations

import struct

from PIL import Image

MAGIC = b"P2IG"
PSMT8 = 0x13
PSMT4 = 0x14


def _unswizzle_palette(entries: list[bytes]) -> list[bytes]:
    if len(entries) != 256:
        return entries
    out = list(entries)
    for block in range(0, 256, 32):
        out[block + 8 : block + 16] = entries[block + 16 : block + 24]
        out[block + 16 : block + 24] = entries[block + 8 : block + 16]
    return out


def _unswizzle8(data: bytes, width: int, height: int) -> bytes:
    """PSMT8 pixels uploaded as 32-bit words back to linear order."""
    out = bytearray(width * height)
    for y in range(height):
        block_row = (y & ~0xF) * width
        swap = (((y + 2) >> 2) & 1) * 4
        column_y = (((y & ~3) >> 1) + (y & 1)) & 7
        for x in range(width):
            at = (
                block_row
                + (x & ~0xF) * 2
                + column_y * width * 2
                + ((x + swap) & 7) * 4
                + ((y >> 1) & 1)
                + ((x >> 2) & 2)
            )
            out[y * width + x] = data[at]
    return bytes(out)


def read_pig(data: bytes) -> Image.Image:
    if data[:4] != MAGIC:
        raise ValueError("Not a P2IG image")
    width_log, height_log, pixel_format = struct.unpack_from("<3H", data, 0x20)
    width, height = 1 << width_log, 1 << height_log
    at_palette, palette_size, at_pixels, _ = struct.unpack_from("<4I", data, 0x40)
    raw = data[at_palette : at_palette + palette_size]
    palette = _unswizzle_palette([raw[i : i + 4] for i in range(0, len(raw), 4)])
    rgba = []
    for entry in palette:
        r, g, b, a = entry
        rgba.append((r, g, b, min(255, a * 2)))
    if pixel_format == PSMT8:
        indices = _unswizzle8(data[at_pixels : at_pixels + width * height], width, height)
    elif pixel_format == PSMT4:
        packed = data[at_pixels : at_pixels + width * height // 2]
        indices = bytes(v for byte in packed for v in (byte & 0xF, byte >> 4))
    else:
        raise ValueError(f"Unsupported P2IG pixel format {pixel_format:#x}")
    image = Image.new("RGBA", (width, height))
    image.putdata([rgba[i] for i in indices])
    return image
