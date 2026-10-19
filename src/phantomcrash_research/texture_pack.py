"""Reader for the game's texture packs (``Media\\scroll\\*_tex.bin``, ``Media\\texture``).

Layout (little-endian):

- ``u32 magic``: ``0x80000101`` or ``0x80000102``; the low byte's meaning is
  unknown and both read the same way.
- ``u32 count``, then ``count`` offsets to texture descriptors, then usually
  ``count`` palette-descriptor offsets (0 when the texture has no palette).
  Some packs omit the palette words; their first descriptor follows directly.
- Texture descriptor (20 bytes, an XDK ``D3DTexture``): common word, absolute
  data offset, lock word, format word, size word (0 = derived from format).
- Palette descriptor: 64-byte header, then 256 B8G8R8A8 entries.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct

from .xbox_texture import TextureFormat, decode_rgba

MAGICS = (0x80000101, 0x80000102)
DESCRIPTOR_SIZE = 20
PALETTE_HEADER_SIZE = 0x40
PALETTE_BYTES = 256 * 4


class TexturePackError(ValueError):
    """The data is not a texture pack this reader understands."""


@dataclass(frozen=True)
class PackTexture:
    index: int
    format: TextureFormat
    data_offset: int
    palette_offset: int

    def rgba(self, pack: bytes) -> bytes:
        palette = None
        if self.palette_offset:
            start = self.palette_offset + PALETTE_HEADER_SIZE
            palette = pack[start : start + PALETTE_BYTES]
        end = self.data_offset + self.format.level_size(0)
        return decode_rgba(self.format, pack[self.data_offset : end], palette)


def parse(data: bytes) -> list[PackTexture]:
    if len(data) < 8:
        raise TexturePackError("File too short for a texture pack header")
    magic, count = struct.unpack_from("<II", data, 0)
    if magic not in MAGICS:
        raise TexturePackError(f"Unknown texture pack magic 0x{magic:08x}")
    if 8 + count * 8 > len(data):
        raise TexturePackError(f"Offset table for {count} textures lies outside the file")
    offsets = struct.unpack_from(f"<{count}I", data, 8)
    # Most packs follow the offsets with one palette offset per texture; a few
    # place the first descriptor immediately after the offsets instead.
    gap_words = (min(offsets, default=8) - 8 - count * 4) // 4
    if gap_words >= count:
        palettes = struct.unpack_from(f"<{count}I", data, 8 + count * 4)
    else:
        palettes = (0,) * count
    textures = []
    for index, (offset, palette) in enumerate(zip(offsets, palettes)):
        if offset + DESCRIPTOR_SIZE > len(data):
            raise TexturePackError(f"Descriptor {index} lies outside the file")
        _common, data_offset, _lock, word, _size = struct.unpack_from("<5I", data, offset)
        fmt = TextureFormat.from_word(word)
        if data_offset + fmt.level_size(0) > len(data):
            raise TexturePackError(f"Texture {index} data lies outside the file")
        if palette and palette + PALETTE_HEADER_SIZE + PALETTE_BYTES > len(data):
            raise TexturePackError(f"Palette {index} lies outside the file")
        textures.append(PackTexture(index, fmt, data_offset, palette))
    return textures
