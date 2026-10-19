import struct

import pytest

from phantomcrash_research import png, texture_pack
from phantomcrash_research.xbox_texture import (
    A8R8G8B8,
    DXT1,
    DXT5,
    P8,
    TextureError,
    TextureFormat,
    decode_rgba,
    swizzle_order,
)


def format_word(kind: int, width: int, height: int, levels: int = 1) -> int:
    return (
        (height.bit_length() - 1) << 24
        | (width.bit_length() - 1) << 20
        | levels << 16
        | kind << 8
        | 0x2A
    )


def swizzle(texels: list, width: int, height: int) -> list:
    out = [None] * (width * height)
    for linear, swizzled in enumerate(swizzle_order(width, height)):
        out[swizzled] = texels[linear]
    return out


def test_format_word_fields():
    fmt = TextureFormat.from_word(0x09A1062A)
    assert (fmt.kind, fmt.width, fmt.height, fmt.levels) == (A8R8G8B8, 1024, 512, 1)
    assert fmt.level_size() == 1024 * 512 * 4
    assert TextureFormat.from_word(format_word(DXT5, 256, 256)).level_size() == 256 * 256


def test_swizzle_interleaves_x_first_then_larger_dimension():
    assert swizzle_order(2, 2) == (0, 1, 2, 3)
    assert swizzle_order(4, 2) == (0, 1, 4, 5, 2, 3, 6, 7)
    assert swizzle_order(2, 4) == (0, 1, 2, 3, 4, 5, 6, 7)


def test_swizzled_argb_decodes_to_rgba_rows():
    width, height = 4, 2
    texels = [bytes((index, 0x10, 0x20, 0x80 + index)) for index in range(width * height)]  # BGRA
    data = b"".join(swizzle(texels, width, height))
    rgba = decode_rgba(TextureFormat(A8R8G8B8, width, height, 1), data)
    assert rgba[:8] == bytes((0x20, 0x10, 0, 0x80, 0x20, 0x10, 1, 0x81))
    assert rgba[-4:] == bytes((0x20, 0x10, 7, 0x87))


def test_p8_uses_palette():
    palette = b"".join(bytes((index, index, index, 255 - index)) for index in range(256))
    indices = swizzle([bytes((value,)) for value in (0, 1, 2, 3)], 2, 2)
    rgba = decode_rgba(TextureFormat(P8, 2, 2, 1), b"".join(indices), palette)
    assert rgba == bytes((0, 0, 0, 255, 1, 1, 1, 254, 2, 2, 2, 253, 3, 3, 3, 252))
    with pytest.raises(TextureError):
        decode_rgba(TextureFormat(P8, 2, 2, 1), b"\0" * 4)


def test_dxt1_block_colours_and_transparency():
    red, blue = 0xF800, 0x001F
    # c0 < c1 selects the 3-colour mode: code 3 is transparent black.
    block = struct.pack("<HHI", blue, red, 0b11_10_01_00)
    rgba = decode_rgba(TextureFormat(DXT1, 4, 4, 1), block)
    assert rgba[0:4] == bytes((0, 0, 255, 255))
    assert rgba[4:8] == bytes((255, 0, 0, 255))
    assert rgba[8:12] == bytes((127, 0, 127, 255))
    assert rgba[12:16] == bytes((0, 0, 0, 0))


def test_dxt5_interpolated_alpha():
    alpha = bytes((255, 0)) + (1).to_bytes(6, "little")  # texel 0 -> a1, others -> a0
    colour = struct.pack("<HHI", 0xFFFF, 0x0000, 0)
    rgba = decode_rgba(TextureFormat(DXT5, 4, 4, 1), alpha + colour)
    assert rgba[3] == 0 and rgba[7] == 255
    assert rgba[0:3] == b"\xff\xff\xff"


def build_pack(textures: list[tuple[int, bytes]], palette_words: bool = True) -> bytes:
    count = len(textures)
    header_words = 2 + count + (count if palette_words else 0)
    descriptors_at = header_words * 4
    data_at = (descriptors_at + 20 * count + 0x7F) & ~0x7F
    offsets, descriptors, blobs = [], b"", b""
    for index, (word, pixels) in enumerate(textures):
        offsets.append(descriptors_at + 20 * index)
        descriptors += struct.pack("<5I", 0x40001, data_at + len(blobs), 0, word, 0)
        blobs += pixels
    header = struct.pack(f"<II{count}I", 0x80000102, count, *offsets)
    if palette_words:
        header += b"\0" * (4 * count)
    body = header + descriptors
    return body + b"\0" * (data_at - len(body)) + blobs


@pytest.mark.parametrize("palette_words", [True, False])
def test_texture_pack_layouts(palette_words):
    first = (format_word(A8R8G8B8, 2, 2), bytes(range(16)))
    second = (format_word(DXT1, 4, 4), struct.pack("<HHI", 0xFFFF, 0xFFFF, 0))
    pack = build_pack([first, second], palette_words)
    textures = texture_pack.parse(pack)
    assert [texture.format.width for texture in textures] == [2, 4]
    assert textures[1].rgba(pack) == b"\xff" * 64


def test_texture_pack_rejects_bad_magic():
    with pytest.raises(texture_pack.TexturePackError):
        texture_pack.parse(b"\0" * 64)


def test_png_round_trip(tmp_path):
    rgba = bytes(range(256)) * 3  # 8 x 24 pixels
    path = tmp_path / "t.png"
    png.write_rgba(path, 8, 24, rgba)
    assert png.decode_rgba(path.read_bytes()) == (8, 24, rgba)
