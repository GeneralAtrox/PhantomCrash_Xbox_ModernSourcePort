"""The garage and shop tables read from the European ``default.xbe``.

Written to ``tables/garage.json``:

- ``robots``: the robot records (``0x19f920``, 1190 records of 0x3c bytes,
  looked up by ``0x05fa83``): a whole Scoobee, as the dealers sell it and
  the computer's teams field it. Shorts: flags (+0), then an (id, level)
  pair per part (body +2, legs +6, arm weapons +0xa and +0xe, shoulder
  weapons +0x12 and +0x16, CHIP +0x1a; an id of -1 is none, the level is
  the low byte, signed), the paint (+0x1e), the emblem (+0x20; 205 is
  none), ten modules (+0x22; -1 for none) and the name (+0x38, a pointer).
- ``dealer_lineups``: what a dealer shows (``0x24fc18``, 65 lineups of
  three (robot, picture) int pairs). A calendar day's first three words
  name each dealer's lineup (``0x06ce5d``; tables/town.json ``dealers``).
- ``modules`` (``0x1c1f90``, 107 records of 0x2c bytes, ``0x09274b``): the
  name, the weight (+0xc, a float the robot's weight adds, ``0x075f93``),
  two floats added to the robot at +0x4538 and +0x4540 (+0x14, +0x18,
  ``0x075d4a``), the armour it adds (+8, ``0x0bb44a``), the socket holes
  it uses (+0x10, a byte; 0 uses one, ``0x0bb5d3``), the laser reduction
  percentage, summed and clamped to 0..100 (+0x20, ``0x0bb6e8``), the O.C
  active and charge times in display frames, which a hangar sums (+0x22 by
  ``0x0b9de3``, +0x24 by ``0x0b9e69``) and the price (+0x28,
  ``0x0932c7``); the other words are kept by offset. The parts shop shows
  them (``0x03cbc3``).
- ``paints`` (``0x1c31f8``, 57) and ``emblems`` (``0x1c34a8``, 206):
  records of 12 bytes holding the name, the index and the price.
- Wild Machines' stock, each lineup chosen by a calendar day's word
  (tables/town.json): ``chip_lineups`` (``0x250230``, 24, by ``chips``,
  ``0x06d373``) and ``module_lineups`` (``0x2508f0``, 135, by each of the
  seven ``modules``, ``0x06d3b9``), records of 0x48 bytes holding up to
  nine (record, level) int pairs, an id of -1 for none; ``used_sv_lineups``
  (``0x252ee8``, 24, by ``used_svs``, ``0x06d3fb``), records of 0x30 bytes
  holding six (robot, unused) int pairs.

What a SV costs at a dealer is the sum of its parts', modules', paint's
and emblem's prices (``0x0392ff``). Names are the game's own (Shift-JIS,
normalised to plain Unicode); the field names are researcher-assigned.
"""

from __future__ import annotations

import struct

from .game_tables import _Image, _plain

ROBOTS = 0x19F920
ROBOT_SIZE = 0x3C
ROBOT_COUNT = 0x4A6
# Side 0 of each weapon pair is the one the game calls RIGHT.
ROBOT_PARTS = ("body", "legs", "right_arm", "left_arm", "right_shoulder", "left_shoulder", "chip")
ROBOT_MODULES = 10
ROBOT_NAME = 0x38
EMBLEM_NONE = 205
LINEUPS = 0x24FC18
LINEUP_COUNT = 0x41
LINEUP_SIZE = 3
MODULES = 0x1C1F90
MODULE_SIZE = 0x2C
MODULE_COUNT = 107
PAINTS = 0x1C31F8
PAINT_COUNT = 57
EMBLEMS = 0x1C34A8
EMBLEM_COUNT = 206
ENTRY_SIZE = 12
SHOP_LINEUPS = 0x250230
SHOP_LINEUP_SIZE = 0x48
SHOP_LINEUP_ITEMS = 9
CHIP_LINEUP_COUNT = 24
MODULE_LINEUPS = SHOP_LINEUPS + CHIP_LINEUP_COUNT * SHOP_LINEUP_SIZE
MODULE_LINEUP_COUNT = 135
USED_SV_LINEUPS = 0x252EE8
USED_SV_LINEUP_SIZE = 0x30
USED_SV_LINEUP_ITEMS = 6
USED_SV_LINEUP_COUNT = 24


def _shorts(image: _Image, address: int, count: int) -> list[int]:
    return list(struct.unpack_from(f"<{count}h", image.data, image.offset(address, 2 * count)))


def _ints(image: _Image, address: int, count: int) -> list[int]:
    return list(struct.unpack_from(f"<{count}i", image.data, image.offset(address, 4 * count)))


def _text(image: _Image, pointer: int) -> str | None:
    return _plain(image.raw_string(pointer)) if pointer else None


def _level(value: int) -> int:
    return struct.unpack("<b", bytes([value & 0xFF]))[0]


def robot(image: _Image, index: int) -> dict:
    record = ROBOTS + index * ROBOT_SIZE
    shorts = _shorts(image, record, ROBOT_NAME // 2)
    parts = {}
    for slot, name in enumerate(ROBOT_PARTS):
        part, level = shorts[1 + slot * 2], shorts[2 + slot * 2]
        parts[name] = {"id": part, "level": _level(level)} if part >= 0 else None
    return {
        "name": _text(image, image.u32(record + ROBOT_NAME)),
        "flags": shorts[0],
        "parts": parts,
        "paint": shorts[15],
        "emblem": shorts[16],
        "modules": [module for module in shorts[17 : 17 + ROBOT_MODULES] if module >= 0],
        "short_36": shorts[27],
    }


def lineup(image: _Image, index: int) -> list[dict]:
    words = _ints(image, LINEUPS + index * LINEUP_SIZE * 8, LINEUP_SIZE * 2)
    return [{"robot": words[k * 2], "picture": words[k * 2 + 1]} for k in range(LINEUP_SIZE)]


def shop_lineup(image: _Image, table: int, index: int) -> list[dict]:
    words = _ints(image, table + index * SHOP_LINEUP_SIZE, SHOP_LINEUP_ITEMS * 2)
    pairs = zip(words[0::2], words[1::2])
    return [{"id": item, "level": level} for item, level in pairs if item >= 0]


def used_sv_lineup(image: _Image, index: int) -> list[int]:
    words = _ints(image, USED_SV_LINEUPS + index * USED_SV_LINEUP_SIZE, USED_SV_LINEUP_ITEMS * 2)
    return [robot for robot in words[0::2] if robot >= 0]


def module(image: _Image, index: int) -> dict:
    record = MODULES + index * MODULE_SIZE
    words = _ints(image, record, MODULE_SIZE // 4)
    weight, float_14, float_18 = (
        round(struct.unpack_from("<f", image.data, image.offset(record + at))[0], 5)
        for at in (0xC, 0x14, 0x18)
    )
    percent = image.data[image.offset(record + 0x20, 1)]
    short_22, short_24 = _shorts(image, record + 0x22, 2)
    return {
        "name": _text(image, words[0]),
        "weight": weight,
        "robot_4538": float_14,
        "robot_4540": float_18,
        "laser_reduction": percent,
        "active_frames": short_22,
        "charge_frames": short_24,
        "price": words[10],
        "armour": words[2],
        "sockets": words[4] & 0xFF,
        "word_04": words[1],
        "word_1c": words[7],
    }


def _entry(image: _Image, table: int, index: int) -> dict:
    name, number, price = _ints(image, table + index * ENTRY_SIZE, 3)
    return {"name": _text(image, name & 0xFFFFFFFF), "index": number, "price": price}


def garage_tables(data: bytes) -> dict:
    """``{"robots", "dealer_lineups", "modules", "paints", "emblems", "chip_lineups",
    "module_lineups", "used_sv_lineups"}``."""
    image = _Image(data)
    return {
        "robots": [robot(image, index) for index in range(ROBOT_COUNT)],
        "dealer_lineups": [lineup(image, index) for index in range(LINEUP_COUNT)],
        "modules": [module(image, index) for index in range(MODULE_COUNT)],
        "paints": [_entry(image, PAINTS, index) for index in range(PAINT_COUNT)],
        "emblems": [_entry(image, EMBLEMS, index) for index in range(EMBLEM_COUNT)],
        "chip_lineups": [
            shop_lineup(image, SHOP_LINEUPS, index) for index in range(CHIP_LINEUP_COUNT)
        ],
        "module_lineups": [
            shop_lineup(image, MODULE_LINEUPS, index) for index in range(MODULE_LINEUP_COUNT)
        ],
        "used_sv_lineups": [used_sv_lineup(image, index) for index in range(USED_SV_LINEUP_COUNT)],
    }
