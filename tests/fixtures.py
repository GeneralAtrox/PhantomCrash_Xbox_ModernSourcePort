"""Builders for small synthetic XDVDFS images and XBE headers."""

from __future__ import annotations

import struct

from phantomcrash_research import xbe, xdvdfs

SECTOR = xdvdfs.SECTOR_SIZE


def directory_entry(left: int, right: int, sector: int, size: int, attributes: int, name: str):
    encoded = name.encode("latin-1")
    raw = struct.pack("<HHIIBB", left, right, sector, size, attributes, len(encoded)) + encoded
    return raw + b"\xff" * (-len(raw) % 4)


def build_xiso(partition_offset: int = 0, xbe_data: bytes = b"XBE-DATA") -> bytes:
    """Root: ``default.xbe`` and ``Media/`` containing ``a.bin``."""
    a_data = b"hello world"
    root_sector, media_sector, xbe_sector = 33, 34, 35
    a_sector = xbe_sector + (len(xbe_data) + SECTOR - 1) // SECTOR

    media_table = directory_entry(0, 0, a_sector, len(a_data), 0x80, "a.bin")
    # Root node 0 is default.xbe; its right child is the Media directory entry.
    first_size = len(directory_entry(0, 0, xbe_sector, len(xbe_data), 0x80, "default.xbe"))
    first = directory_entry(0, first_size // 4, xbe_sector, len(xbe_data), 0x80, "default.xbe")
    second = directory_entry(0, 0, media_sector, SECTOR, xdvdfs.ATTRIBUTE_DIRECTORY, "Media")

    def padded_table(table: bytes) -> bytes:
        return table + b"\xff" * (SECTOR - len(table))

    image = bytearray(partition_offset + (a_sector + 1) * SECTOR)
    volume = xdvdfs.MAGIC + struct.pack("<II", root_sector, SECTOR)
    for sector, payload in (
        (32, volume),
        (root_sector, padded_table(first + second)),
        (media_sector, padded_table(media_table)),
        (xbe_sector, xbe_data),
        (a_sector, a_data),
    ):
        start = partition_offset + sector * SECTOR
        image[start : start + len(payload)] = payload
    return bytes(image)


def build_xbe() -> bytes:
    """A minimal retail XBE header with one library and one section."""
    base = 0x10000
    data = bytearray(0x1000)
    data[0:4] = xbe.MAGIC
    entry, thunk = 0x11234, 0x80010000

    def put(offset: int, value: int) -> None:
        struct.pack_into("<I", data, offset, value)

    put(0x104, base)
    put(0x10C, 0x20000)
    put(0x114, 1034726400)  # 2002-10-16
    put(0x118, base + 0x400)
    put(0x11C, 1)
    put(0x120, base + 0x300)
    put(0x128, entry ^ xbe.ENTRY_KEYS["retail"])
    put(0x14C, base + 0x600)
    put(0x150, base + 0x610)
    put(0x158, thunk ^ xbe.THUNK_KEYS["retail"])
    put(0x160, 1)
    put(0x164, base + 0x500)

    struct.pack_into("<6I", data, 0x300, 0x16, 0x11000, 0x100, 0x800, 0x100, base + 0x620)
    put(0x408, 0x504C0001)
    title = "phantomcrash".encode("utf-16-le")
    data[0x40C : 0x40C + len(title)] = title
    data[0x500:0x508] = b"XAPILIB\0"
    struct.pack_into("<HHHH", data, 0x508, 1, 0, 4721, 0x4001)
    data[0x600:0x60A] = b"K:\\x.exe\0\0"
    data[0x610:0x616] = b"x.exe\0"
    data[0x620:0x626] = b".text\0"
    return bytes(data)
