"""Reader for S.L.A.I.'s disc, its ``PC2.STM`` packages and ``Wp`` compression.

Layout, found by inspection (names are researcher-assigned):

- The disc is plain ISO 9660. Nearly all data is in ``PC2.STM``.
- ``PC2.STM`` is a run of *packages*, each starting on a 2048-byte sector
  with a name table of 32-byte entries: a NUL-padded 24-byte name, then the
  file's offset from the package start and its size (u32 each). The table
  ends at the first entry that is not a name. Textures are mostly one-file
  packages (``S3A02H01.PIG`` ...) after the model's own package.
- Most files are wrapped: a magic, a u32 unpacked size, then the data.
  ``WpNO`` stores it as is. ``Wp08`` and ``Wp16`` are LZ in 1- and 2-byte
  units: a u32 flag word read LSB first (1 a literal unit, 0 a copy), a
  copy being a u16 whose low 5 bits + 3 (``Wp16``: + 2) are the length and
  whose high 11 bits the distance back (0 meaning 2048), both in units.
  Before the start the window reads as zeros.
"""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path
import re
import struct

SECTOR = 2048
ARCHIVE = "PC2.STM"
ENTRY_SIZE = 32
NAME_SIZE = 24
# The LZ unit in bytes and the shortest copy in units.
WRAP_MAGICS = {b"Wp08": (1, 3), b"Wp16": (2, 2)}
STORED_MAGIC = b"WpNO"
WRAP_HEADER = 8
COPY_LENGTH_BITS = 5
WINDOW = 1 << (16 - COPY_LENGTH_BITS)
_NAME = re.compile(rb"[A-Z0-9_.-]{1,20}\.[A-Z0-9]{2,3}\0")
_SCAN_CHUNK = 64 * SECTOR


@dataclass(frozen=True)
class PackedFile:
    package: int  # the package's byte offset in the archive
    name: str
    offset: int  # from the package start
    size: int


def unwrap(data: bytes) -> bytes:
    """A file's contents: unpacked when ``Wp`` wrapped, else as stored."""
    magic = data[:4]
    if magic == STORED_MAGIC:
        return data[WRAP_HEADER : WRAP_HEADER + struct.unpack_from("<I", data, 4)[0]]
    if magic not in WRAP_MAGICS:
        return data
    unit, copy_min = WRAP_MAGICS[magic]
    size = struct.unpack_from("<I", data, 4)[0]
    out = bytearray()
    pos = WRAP_HEADER
    while len(out) < size and pos + 4 <= len(data):
        flags = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        for bit in range(32):
            if len(out) >= size:
                break
            if flags >> bit & 1:
                out += data[pos : pos + unit]
                pos += unit
                continue
            value = struct.unpack_from("<H", data, pos)[0]
            pos += 2
            length = (value & ((1 << COPY_LENGTH_BITS) - 1)) + copy_min
            back = (value >> COPY_LENGTH_BITS or WINDOW) * unit
            for _ in range(length * unit):
                # Before the start the window reads as zeros.
                out.append(out[-back] if back <= len(out) else 0)
    if len(out) < size:
        raise ValueError(f"Wp data ended at {len(out)} of {size} bytes")
    return bytes(out[:size])


class SlaiDisc:
    """The disc image, its ISO 9660 files and the packages in ``PC2.STM``."""

    def __init__(self, path: Path) -> None:
        self._file = path.open("rb")
        self._files = self._read_directory()
        self._archive = self._files[ARCHIVE]
        self._packages: dict[int, list[PackedFile]] | None = None

    def close(self) -> None:
        self._file.close()

    def __enter__(self) -> SlaiDisc:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def _read(self, offset: int, size: int) -> bytes:
        self._file.seek(offset)
        return self._file.read(size)

    def _read_directory(self) -> dict[str, tuple[int, int]]:
        descriptor = self._read(16 * SECTOR, SECTOR)
        root = descriptor[156:190]
        found: dict[str, tuple[int, int]] = {}
        pending = [("", struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0])]
        while pending:
            prefix, extent, size = pending.pop()
            data = self._read(extent * SECTOR, size)
            pos = 0
            while pos < len(data):
                length = data[pos]
                if length == 0:
                    pos = (pos // SECTOR + 1) * SECTOR
                    continue
                record = data[pos : pos + length]
                pos += length
                name = record[33 : 33 + record[32]]
                if name in (b"\0", b"\1"):
                    continue
                path = prefix + name.decode("ascii").split(";")[0]
                where = (struct.unpack_from("<I", record, 2)[0], struct.unpack_from("<I", record, 10)[0])
                if record[25] & 2:
                    pending.append((path + "/", *where))
                else:
                    found[path] = (where[0] * SECTOR, where[1])
        return found

    def disc_files(self) -> dict[str, tuple[int, int]]:
        """Every ISO 9660 file: path -> (byte offset, size)."""
        return dict(self._files)

    def packages(self) -> dict[int, list[PackedFile]]:
        """Each package's files, keyed by the package's offset in PC2.STM."""
        if self._packages is None:
            self._packages = dict(self._scan())
        return self._packages

    def _scan(self) -> Iterator[tuple[int, list[PackedFile]]]:
        start, size = self._archive
        owned_end = 0
        for base in range(0, size, _SCAN_CHUNK):
            data = self._read(start + base, min(_SCAN_CHUNK + 65536, size - base))
            for at in range(0, min(_SCAN_CHUNK, len(data)), SECTOR):
                if base + at < owned_end:
                    continue
                if not _NAME.match(data, at):
                    continue
                if not _NAME.match(data, at + ENTRY_SIZE):
                    # A one-file package: its file follows the table and is wrapped.
                    first = struct.unpack_from("<I", data, at + NAME_SIZE)[0]
                    if first != ENTRY_SIZE or data[at + first : at + first + 2] != b"Wp":
                        continue
                files = []
                for entry in range(at, len(data) - ENTRY_SIZE, ENTRY_SIZE):
                    if not _NAME.match(data, entry):
                        break
                    name = data[entry : entry + NAME_SIZE].split(b"\0")[0].decode("ascii")
                    offset, length = struct.unpack_from("<II", data, entry + NAME_SIZE)
                    files.append(PackedFile(base + at, name, offset, length))
                # Names inside a large package can land on sector boundaries.
                # Such a suffix of the table is not another package: its file
                # offsets still refer to the earlier package's start.
                if not files or files[0].offset < len(files) * ENTRY_SIZE:
                    continue
                if any(f.offset < len(files) * ENTRY_SIZE or
                       base + at + f.offset + f.size > size for f in files):
                    continue
                if self._read(start + base + at + files[0].offset, 4) not in (
                        STORED_MAGIC, *WRAP_MAGICS):
                    continue
                owned_end = base + at + max(f.offset + f.size for f in files)
                yield base + at, files

    def raw(self, file: PackedFile) -> bytes:
        start, _ = self._archive
        return self._read(start + file.package + file.offset, file.size)

    def read(self, file: PackedFile) -> bytes:
        return unwrap(self.raw(file))

    def find(self, name: str) -> list[PackedFile]:
        wanted = name.upper()
        return [f for files in self.packages().values() for f in files if f.name == wanted]
