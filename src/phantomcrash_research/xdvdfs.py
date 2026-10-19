"""Read-only reader for the Xbox XDVDFS game filesystem.

Supports trimmed ``.xiso`` images and full Redump-style images whose game
partition starts at one of the standard XGD offsets.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct
from typing import BinaryIO, Iterator

SECTOR_SIZE = 2048
VOLUME_SECTOR = 32
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# xiso, XGD1, XGD2 and XGD3 game-partition offsets.
PARTITION_OFFSETS = (0x0, 0x18300000, 0xFD90000, 0x2080000)
ATTRIBUTE_DIRECTORY = 0x10
_ENTRY_HEADER = struct.Struct("<HHIIBB")
_EMPTY_NODE = 0xFFFF
_COPY_CHUNK = 8 * 1024 * 1024


class XdvdfsError(ValueError):
    """The image is not a readable XDVDFS filesystem."""


@dataclass(frozen=True)
class Entry:
    path: str
    sector: int
    size: int
    attributes: int

    @property
    def is_directory(self) -> bool:
        return bool(self.attributes & ATTRIBUTE_DIRECTORY)


class XdvdfsImage:
    """An open XDVDFS image. Use as a context manager."""

    def __init__(self, path: str | Path):
        self.path = Path(path)
        self._file: BinaryIO = open(self.path, "rb")
        try:
            self._size = self.path.stat().st_size
            self.partition_offset = self._find_partition()
            self._file.seek(self.partition_offset + VOLUME_SECTOR * SECTOR_SIZE + len(MAGIC))
            self.root_sector, self.root_size = struct.unpack("<II", self._file.read(8))
        except Exception:
            self._file.close()
            raise

    def __enter__(self) -> "XdvdfsImage":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def close(self) -> None:
        self._file.close()

    def _find_partition(self) -> int:
        for offset in PARTITION_OFFSETS:
            start = offset + VOLUME_SECTOR * SECTOR_SIZE
            if start + len(MAGIC) > self._size:
                continue
            self._file.seek(start)
            if self._file.read(len(MAGIC)) == MAGIC:
                return offset
        raise XdvdfsError(f"No XDVDFS volume descriptor found in {self.path}")

    def _read(self, sector: int, size: int) -> bytes:
        start = self.partition_offset + sector * SECTOR_SIZE
        if sector < 0 or size < 0 or start + size > self._size:
            raise XdvdfsError(f"Extent sector={sector} size={size} lies outside the image")
        self._file.seek(start)
        return self._file.read(size)

    def entries(self) -> list[Entry]:
        """Every file and directory, sorted by path."""
        found: list[Entry] = []
        pending = [("", self.root_sector, self.root_size)]
        visited_tables: set[int] = set()
        while pending:
            prefix, sector, size = pending.pop()
            if sector in visited_tables:
                raise XdvdfsError(f"Directory table at sector {sector} is referenced twice")
            visited_tables.add(sector)
            for entry in self._directory(prefix, sector, size):
                found.append(entry)
                if entry.is_directory and entry.size:
                    pending.append((entry.path, entry.sector, entry.size))
        return sorted(found, key=lambda e: e.path.lower())

    def files(self) -> list[Entry]:
        return [entry for entry in self.entries() if not entry.is_directory]

    def _directory(self, prefix: str, sector: int, size: int) -> Iterator[Entry]:
        table = self._read(sector, size)
        pending = [0]
        visited: set[int] = set()
        while pending:
            node = pending.pop()
            offset = node * 4
            if node in visited:
                raise XdvdfsError(f"Directory tree loop at sector {sector}, node {node}")
            visited.add(node)
            if offset + _ENTRY_HEADER.size > len(table):
                raise XdvdfsError(f"Directory node {node} at sector {sector} is truncated")
            left, right, start, length, attributes, name_length = _ENTRY_HEADER.unpack_from(
                table, offset
            )
            if left == _EMPTY_NODE:
                continue  # Empty directory or sector padding.
            name_start = offset + _ENTRY_HEADER.size
            name = table[name_start : name_start + name_length]
            if not name_length or len(name) != name_length:
                raise XdvdfsError(f"Directory node {node} at sector {sector} has a bad name")
            yield Entry(f"{prefix}/{name.decode('latin-1')}", start, length, attributes)
            pending.extend(child for child in (left, right) if child)

    def read(self, entry: Entry) -> bytes:
        return self._read(entry.sector, entry.size)

    def copy_to(self, entry: Entry, destination: BinaryIO) -> None:
        """Stream one file without holding it in memory."""
        start = self.partition_offset + entry.sector * SECTOR_SIZE
        if entry.sector < 0 or start + entry.size > self._size:
            raise XdvdfsError(f"{entry.path} extends past the end of the image")
        self._file.seek(start)
        remaining = entry.size
        while remaining:
            chunk = self._file.read(min(remaining, _COPY_CHUNK))
            if not chunk:
                raise XdvdfsError(f"Unexpected end of image while reading {entry.path}")
            destination.write(chunk)
            remaining -= len(chunk)

    def find(self, path: str) -> Entry:
        wanted = "/" + path.replace("\\", "/").strip("/").lower()
        for entry in self.entries():
            if entry.path.lower() == wanted:
                return entry
        raise FileNotFoundError(path)
