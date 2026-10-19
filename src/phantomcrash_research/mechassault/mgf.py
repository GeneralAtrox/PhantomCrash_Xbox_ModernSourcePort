"""Reader for MechAssault's ``.mgf`` archives and its property streams.

Layout, found by inspection (names are researcher-assigned):

- An ``.mgf`` is a 32-byte header (``count`` at +8) and ``count`` 12-byte
  records of (offset, size, checksum), then zlib blocks. Every block inflates
  to 128 KiB (the last to less); together they are one *stream*.
- The stream starts with ``mgf `` and a resource table of 28-byte records at
  +0x30: (name hash, path hash, size, offset, ?, id, timestamp), ``count``
  at stream +0x0c. The path hash is ``~crc32(lowercase path)`` with
  backslashes, e.g. ``\\specialeffects\\weapons\\energy\\laser_l1\\laser_l1.node``.
- Binary resources (``.mtb``, entities) are property streams: records
  ``02 2f 02 ff, type, ~crc32(name), size, data`` grouped under
  ``01 2f 01 ff, count, ~crc32(class)`` objects. The names come from the
  XBE's strings.
"""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path
import re
import struct
import zlib

from ..xdvdfs import XdvdfsImage

BACKSLASH = "\\"
STREAM_MAGIC = b"mgf "
TABLE_OFFSET = 0x30
RECORD_SIZE = 28
_OBJECT_MARK = bytes([1, 0x2F, 1, 0xFF])
_PROPERTY_MARK = bytes([2, 0x2F, 2, 0xFF])
_STREAM_MARK = bytes([0, 0x2F, 0, 0xFF])


def path_hash(path: str) -> int:
    return zlib.crc32(path.lower().encode("latin1")) ^ 0xFFFFFFFF


def inflate(data: bytes) -> bytes:
    count = struct.unpack_from("<I", data, 8)[0]
    out = bytearray()
    for index in range(count):
        offset, size, _check = struct.unpack_from("<III", data, 32 + 12 * index)
        out += zlib.decompress(data[offset : offset + size])
    return bytes(out)


class Archive:
    def __init__(self, name: str, stream: bytes):
        if stream[:4] != STREAM_MAGIC:
            raise ValueError(f"{name} does not hold an mgf stream")
        self.name = name
        self.stream = stream
        count = struct.unpack_from("<I", stream, 0x0C)[0]
        self._records = [
            struct.unpack_from("<7I", stream, TABLE_OFFSET + RECORD_SIZE * i) for i in range(count)
        ]
        self._by_path: dict[int, int] = {}
        for index, record in enumerate(self._records):
            self._by_path.setdefault(record[1], index)

    def get(self, path: str) -> bytes | None:
        index = self._by_path.get(path_hash(path))
        if index is None:
            return None
        _name, _path, size, offset = self._records[index][:4]
        return self.stream[offset : offset + size]


class Collection:
    """The disc's archives, opened on demand in order until a path is found."""

    def __init__(self, image: Path, order: list[str] | None = None):
        self._image = Path(image)
        with XdvdfsImage(self._image) as disc:
            self._files = {
                entry.path.lstrip("/"): entry
                for entry in disc.files()
                if entry.path.lower().endswith(".mgf") and entry.path.count("/") == 1
            }
        names = sorted(self._files)
        if order:
            names = [n for n in order if n in self._files] + [n for n in names if n not in order]
        self._pending = names
        self._open: list[Archive] = []
        self._cache: dict[str, bytes | None] = {}

    def _load(self, name: str) -> Archive | None:
        entry = self._files[name]
        with XdvdfsImage(self._image) as disc:
            data = disc._read(entry.sector, entry.size)
        try:
            return Archive(name, inflate(data))
        except (zlib.error, ValueError, struct.error):
            return None  # movies.mgf and text.mgf use other layouts

    def _open_next(self) -> bool:
        while self._pending:
            archive = self._load(self._pending.pop(0))
            if archive is not None:
                self._open.append(archive)
                return True
        return False

    def archive(self, name: str) -> Archive:
        """The named archive, opened now if it is not yet."""
        for archive in self._open:
            if archive.name == name:
                return archive
        if name in self._pending:
            self._pending.remove(name)
        archive = self._load(name)
        if archive is None:
            raise ValueError(f"{name} is not a readable archive")
        self._open.append(archive)
        return archive

    def get(self, path: str) -> bytes | None:
        key = path.lower()
        if key in self._cache:
            return self._cache[key]
        searched = 0
        while True:
            for archive in self._open[searched:]:
                data = archive.get(path)
                if data is not None:
                    self._cache[key] = data
                    return data
            searched = len(self._open)
            if not self._open_next():
                self._cache[key] = None
                return None


def xbe_names(image: Path) -> dict[int, str]:
    """~crc32 of every string in default.xbe: the property and class names."""
    with XdvdfsImage(image) as disc:
        entry = next(e for e in disc.files() if e.path.lower() == "/default.xbe")
        xbe = disc._read(entry.sector, entry.size)
    names: dict[int, str] = {}
    for match in re.finditer(rb"[\x20-\x7e]{2,80}", xbe):
        text = match.group()
        names.setdefault(zlib.crc32(text) ^ 0xFFFFFFFF, text.decode("latin1"))
    return names


def _value(kind: int, data: bytes) -> object:
    if kind == 9:
        return data.split(b"\0")[0].decode("latin1")
    if kind == 7 and len(data) == 4:
        return struct.unpack("<f", data)[0]
    if kind == 3 and len(data) == 4:
        return struct.unpack("<i", data)[0]
    if kind in (1, 6) and len(data) == 4:
        return struct.unpack("<I", data)[0]
    if kind == 11 and len(data) == 12:
        return list(struct.unpack("<3f", data))
    if kind == 1 and len(data) == 1:
        return data[0]
    return data.hex()


def parse_properties(blob: bytes, names: dict[int, str]) -> Iterator[tuple[str, dict[str, object]]]:
    """Yield (class name, {property: value}) for each object of a stream."""
    if blob[:4] != _STREAM_MARK:
        raise ValueError("not a property stream")
    position = 4 + 16
    while position < len(blob):
        if blob[position : position + 4] != _OBJECT_MARK:
            raise ValueError(f"object mark expected at {position:#x}")
        count, class_hash = struct.unpack_from("<II", blob, position + 4)
        position += 12
        properties: dict[str, object] = {}
        for _ in range(count):
            if blob[position : position + 4] != _PROPERTY_MARK:
                raise ValueError(f"property mark expected at {position:#x}")
            kind, name_hash, size = struct.unpack_from("<III", blob, position + 4)
            data = blob[position + 16 : position + 16 + size]
            position += 16 + size
            properties[names.get(name_hash, f"#{name_hash:08x}")] = _value(kind, data)
        yield names.get(class_hash, f"#{class_hash:08x}"), properties
