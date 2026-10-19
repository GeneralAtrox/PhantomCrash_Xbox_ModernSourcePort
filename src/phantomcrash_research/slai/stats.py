"""S.L.A.I. PAL executable part tables, separate from the native adapter.

Table addresses/counts/strides are recovered from moduleparam's selectors:
0x24d210 body, 0x24d290 legs, 0x24d320 arms, 0x24d3b0 shoulders.
0x24dea8 evaluates the damage curve at +0x54; 0x24dec4 applies +0x52's
floor. Labels below are ours. Raw originals always accompany adapted parts.
"""

from __future__ import annotations

import struct
from pathlib import Path

from .archive import SlaiDisc
from .music import messages

TABLES = {"bodies": (0x3CA320, 34, 60, "UK_BODYNAME.MSG"),
          "legs": (0x3CAB60, 89, 208, "UK_LEGNAME.MSG"),
          "arm_weapons": (0x3CF480, 101, 312, "UK_ARMNAME.MSG"),
          "shoulder_weapons": (0x3D70D0, 106, 312, "UK_SHOULDERNAME.MSG")}


def _elf_offset(data: bytes, address: int, size: int) -> int:
    if data[:16] != b"\x7fELF\x01\x01\x01" + bytes(9):
        raise ValueError("Expected the S.L.A.I. PAL 32-bit little-endian ELF")
    start = struct.unpack_from("<I", data, 28)[0]
    entry_size, count = struct.unpack_from("<HH", data, 42)
    for i in range(count):
        kind, offset, va, _, filesz = struct.unpack_from("<5I", data, start + i * entry_size)
        if kind == 1 and va <= address and address + size <= va + filesz:
            return offset + address - va
    raise ValueError(f"Unmapped S.L.A.I. table address {address:#x}")


def _curve(data: bytes, at: int, kind: str) -> dict:
    base, mid, top, knee, scale = struct.unpack_from(f"<3{kind}2b", data, at)
    if not 0 <= knee <= 100 or scale not in (-1, 0, 1):
        raise ValueError(f"Invalid S.L.A.I. level curve at {at:#x}")
    return dict(zip(("base", "mid", "top", "knee", "low_scale"),
                    (base, mid, top, knee, scale)))


def read_stats(disc: SlaiDisc) -> dict:
    offset, size = disc.disc_files()["SLES_529.40"]
    elf = disc._read(offset, size)
    # Guard the selectors against applying PAL offsets to another revision.
    expected = {0x24D234: 0x3C02003D, 0x24D23C: 0x2442A320,
                0x24D2BC: 0x2442AB60, 0x24D34C: 0x2442F480,
                0x24D3DC: 0x244270D0}
    for va, instruction in expected.items():
        if struct.unpack_from("<I", elf, _elf_offset(elf, va, 4))[0] != instruction:
            raise ValueError("S.L.A.I. executable revision does not match the researched selectors")
    result = {"source": {"executable": "SLES_529.40", "names": "shipped UK MSG",
                          "field_labels": "researcher assigned", "native_units_verified": False}}
    for category, (va, count, stride, text) in TABLES.items():
        found = disc.find(text)
        if not found:
            raise ValueError(f"Missing {text}")
        names = messages(disc.read(found[0]))
        base = _elf_offset(elf, va, count * stride)
        records = []
        for i in range(count):
            at = base + i * stride
            name, maker = struct.unpack_from("<2H", elf, at)
            if name == 0xFFFF:
                records.append({"index": i, "unused": True,
                                "source": {"address": hex(va + i * stride), "record_size": stride}})
                continue
            if name >= len(names):
                raise ValueError(f"Invalid {category} name index {name}")
            record = {"index": i, "name": names[name] or f"S.L.A.I. {category} {name}",
                      "name_index": name, "maker": maker,
                      "source": {"address": hex(va + i * stride), "record_size": stride}}
            if category in ("arm_weapons", "shoulder_weapons"):
                record["model"] = struct.unpack_from("<I", elf, at + 4)[0]
                record["weight"] = _curve(elf, at + 8, "f")
                record["kind"], record["flight"] = elf[at + 0x18:at + 0x1A]
                record["fire_frames"], record["life_frames"] = struct.unpack_from("<2H", elf, at + 0x20)
                for field, off in (("rounds_min", 0x48), ("damage_min", 0x52), ("ammo_min", 0x80)):
                    record[field] = struct.unpack_from("<h", elf, at + off)[0]
                for field, off in (("rounds", 0x4A), ("damage", 0x54), ("blast_radius", 0x5C),
                                   ("blast_frames", 0x66), ("ammo", 0x82), ("recharge_frames", 0x8A)):
                    record[field] = _curve(elf, at + off, "h")
                record["blast_damage"] = _curve(elf, at + 0x70, "i")
                record["price"] = _curve(elf, at + 0xBC, "i")
            else:
                record["armour"] = _curve(elf, at + (4 if category == "bodies" else 8), "i")
                record["weight"] = _curve(elf, at + (0x14 if category == "bodies" else 0x48), "f")
                record["price"] = _curve(elf, at + (0x2C if category == "bodies" else 0x88), "i")
                if category == "legs":
                    record["model"], record["alternate_model"] = struct.unpack_from("<2H", elf, at + 4)
                    for field, off in (("speed", 0x18), ("jump", 0x28), ("turn", 0x38),
                                       ("capacity", 0x58), ("knockback_resistance", 0x78),
                                       ("dodge", 0x98), ("air_dodge", 0xA8)):
                        record[field] = _curve(elf, at + off, "f")
            records.append(record)
        result[category] = records
    return result


def extract_stats(image: Path) -> dict:
    with SlaiDisc(image) as disc:
        return read_stats(disc)
