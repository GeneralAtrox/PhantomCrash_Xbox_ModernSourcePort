"""Parser for the Xbox executable (XBE) header, certificate and section table."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
import struct

MAGIC = b"XBEH"
ENTRY_KEYS = {"retail": 0xA8FC57AB, "debug": 0x94859D4B}
THUNK_KEYS = {"retail": 0x5B6D40B6, "debug": 0xEFB1F152}
SECTION_HEADER_SIZE = 56
LIBRARY_VERSION_SIZE = 16


class XbeError(ValueError):
    """The data is not a readable XBE."""


@dataclass(frozen=True)
class Section:
    name: str
    flags: int
    virtual_address: int
    virtual_size: int
    raw_address: int
    raw_size: int


@dataclass(frozen=True)
class Library:
    name: str
    major: int
    minor: int
    build: int
    flags: int


@dataclass
class Xbe:
    base_address: int
    image_size: int
    timestamp_utc: str
    build_kind: str
    entry_point: int
    kernel_thunk_address: int
    title_id: int
    title_name: str
    region_flags: int
    disk_number: int
    version: int
    debug_path: str
    debug_filename: str
    libraries: list[Library] = field(default_factory=list)
    sections: list[Section] = field(default_factory=list)

    @property
    def title_code(self) -> str:
        """Publisher letters and number, e.g. ``PL-001``."""
        letters = bytes([(self.title_id >> 24) & 0xFF, (self.title_id >> 16) & 0xFF])
        return f"{letters.decode('latin-1')}-{self.title_id & 0xFFFF:03d}"

    @property
    def xdk_build(self) -> int | None:
        builds = {library.build for library in self.libraries if library.name == "XAPILIB"}
        return builds.pop() if len(builds) == 1 else None

    def section(self, name: str) -> Section:
        for section in self.sections:
            if section.name == name:
                return section
        raise KeyError(name)

    def to_dict(self) -> dict:
        data = asdict(self)
        data["title_code"] = self.title_code
        data["xdk_build"] = self.xdk_build
        for key in ("base_address", "entry_point", "kernel_thunk_address", "title_id"):
            data[key] = f"0x{data[key]:08x}"
        for section in data["sections"]:
            for key in ("flags", "virtual_address", "virtual_size", "raw_address", "raw_size"):
                section[key] = f"0x{section[key]:x}"
        return data


class _Reader:
    def __init__(self, data: bytes, base: int):
        self.data = data
        self.base = base

    def u32(self, offset: int) -> int:
        if offset < 0 or offset + 4 > len(self.data):
            raise XbeError(f"Header field at 0x{offset:x} lies outside the file")
        return struct.unpack_from("<I", self.data, offset)[0]

    def offset(self, address: int, size: int = 1) -> int:
        offset = address - self.base
        if offset < 0 or offset + size > len(self.data):
            raise XbeError(f"Address 0x{address:08x} lies outside the XBE headers")
        return offset

    def c_string(self, address: int) -> str:
        if not address:
            return ""
        start = self.offset(address)
        end = self.data.find(b"\0", start)
        if end < 0:
            raise XbeError(f"Unterminated string at 0x{address:08x}")
        return self.data[start:end].decode("latin-1")


def _decode_xor(value: int, keys: dict[str, int], low: int, high: int) -> tuple[str, int]:
    for kind, key in keys.items():
        decoded = value ^ key
        if low <= decoded < high:
            return kind, decoded
    raise XbeError(f"Encoded address 0x{value:08x} matches neither retail nor debug keys")


def parse(data: bytes) -> Xbe:
    if data[:4] != MAGIC:
        raise XbeError("Missing XBEH magic")
    reader = _Reader(data, struct.unpack_from("<I", data, 0x104)[0])
    base = reader.base
    image_size = reader.u32(0x10C)
    kind, entry = _decode_xor(reader.u32(0x128), ENTRY_KEYS, base, base + image_size)
    thunk_value = reader.u32(0x158) ^ THUNK_KEYS[kind]

    certificate = reader.offset(reader.u32(0x118), 0xB0)
    title_name = data[certificate + 0x0C : certificate + 0x5C].decode("utf-16-le")

    libraries = []
    library_table = reader.offset(reader.u32(0x164), reader.u32(0x160) * LIBRARY_VERSION_SIZE)
    for index in range(reader.u32(0x160)):
        offset = library_table + index * LIBRARY_VERSION_SIZE
        name = data[offset : offset + 8].rstrip(b"\0").decode("latin-1")
        libraries.append(Library(name, *struct.unpack_from("<HHHH", data, offset + 8)))

    sections = []
    count = reader.u32(0x11C)
    table = reader.offset(reader.u32(0x120), count * SECTION_HEADER_SIZE)
    for index in range(count):
        offset = table + index * SECTION_HEADER_SIZE
        flags, address, virtual_size, raw, raw_size, name_address = struct.unpack_from(
            "<6I", data, offset
        )
        if raw + raw_size > len(data):
            raise XbeError(f"Section {index} raw data lies outside the file")
        sections.append(
            Section(reader.c_string(name_address), flags, address, virtual_size, raw, raw_size)
        )

    timestamp = datetime.fromtimestamp(reader.u32(0x114), timezone.utc)
    return Xbe(
        base_address=base,
        image_size=image_size,
        timestamp_utc=timestamp.isoformat(),
        build_kind=kind,
        entry_point=entry,
        kernel_thunk_address=thunk_value,
        title_id=struct.unpack_from("<I", data, certificate + 0x08)[0],
        title_name=title_name.split("\0", 1)[0],
        region_flags=struct.unpack_from("<I", data, certificate + 0xA0)[0],
        disk_number=struct.unpack_from("<I", data, certificate + 0xA8)[0],
        version=struct.unpack_from("<I", data, certificate + 0xAC)[0],
        debug_path=reader.c_string(reader.u32(0x14C)),
        debug_filename=reader.c_string(reader.u32(0x150)),
        libraries=libraries,
        sections=sections,
    )
