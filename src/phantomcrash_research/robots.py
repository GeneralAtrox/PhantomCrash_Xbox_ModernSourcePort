"""Robot part tables and skeletons read from the European ``default.xbe``.

A robot (the game calls them Scoobees) is a body, legs, two arms and two
shoulder weapons. Each part id has a record in a part table; the record's
type picks the part's model, skeleton and motions from the tables below.
The names are researcher-assigned. See ``docs/ROBOTS.md``.

Skeletons are static trees of 16-word nodes:

====  ==================================================================
Word  Meaning
====  ==================================================================
0     Node type: 0 plain, 1 arm joint, 2 IK chain start, 3 IK bone,
      4 IK end (its track is the target), 7 where the two arms attach
1     Flags (0x2: root motion, 0x4: turns with the aim, 0x100: pitches
      with the aim, 0x20000: effect point)
2     Track of the motion that drives the node, or -1 (static)
3-5   Translation
6-8   Rotation (radians)
9     Children: a zero-terminated pointer list
10    Bone slot the node's matrix fills, or -1
====  ==================================================================
"""

from __future__ import annotations

import struct

from . import game_tables

BODY_PARTS = (0x1B5B88, 0x40, 16)  # (table, record size, count); type: byte 4
LEG_PARTS = (0x1B5F88, 0xC0, 43)  # type: word 2
ARM_PARTS = (0x1BAE48, 0xC8, 76)  # type: word 2 (the lookup at 0x0926be)
SHOULDER_PARTS = (0x1BE9A8, 0xC8, 69)  # type: word 2 (0x0926ee)

BODY_TYPES = 7
LEG_TYPES = 21
ARM_TYPES = 34
SHOULDER_TYPES = 38
PAINTS_PER_LEG_TYPE = 57  # colour index limit checked at 0x081a3b
EMBLEMS = 206

BODY_MODELS = 0x25AC70  # model resource per body type
BODY_EMBLEM_MODELS = 0x25AC8C  # the emblem decal model per body type
LEG_MODELS = 0x25ACA8
ARM_MODELS = (0x25AD88, 0x25AD00)  # left (side 0), right (side 1)
SHOULDER_MODELS = (0x25AF20, 0x25AFB8)  # sides 2 and 3
LEG_SKELETONS = 0x2416B8
ARM_SKELETONS = (0x241798, 0x241710)
ARM_CLASSES = 0x22F398  # 0 or 1: picks one of the leg type's two arm banks
ARM_MOTIONS = (0x259190, 0x259238)  # [leg type][arm class] per side
LEG_MOTIONS = 0x259300  # per leg type
BODY_MOTIONS = 0x2592E0  # per body type
# The pose builder (0x0aa499) turns the forearm nodes (flags 0x30) of a robot
# whose body is of this type by minus the float here (pi/2) about x.
FOREARM_TURN_BODY_TYPE = 1
FOREARM_TURN = 0x19F870
MOTION_TIMINGS = 0x22F4C8  # [leg type][motion] 4 floats (read at 0x08ddf0)
MOTIONS_PER_BANK = 37
LEG_RADII = 0x22F420  # {f32 ?, f32 collision radius} per leg type (radius read at 0x080e41)
# Per leg type: its ground shadow, a mesh of resource 184 (model slots 0-20),
# read by 0x082e60 for robots without a stencil volume.
LEG_SHADOWS = 0x242750
PAINTS = 0x25ABC8  # {int *textures, int} per leg type
EMBLEM_TEXTURES = 0x25A890  # texture resource per emblem

NODE_WORDS = 16
MAX_DEPTH = 32


class RobotTableError(ValueError):
    pass


class _Reader:
    def __init__(self, data: bytes):
        self.image = game_tables._Image(data)
        self.data = data

    def u32(self, address: int) -> int:
        return self.image.u32(address)

    def s32(self, address: int) -> int:
        return struct.unpack_from("<i", self.data, self.image.offset(address))[0]

    def f32(self, address: int) -> float:
        return struct.unpack_from("<f", self.data, self.image.offset(address))[0]

    def words(self, address: int, count: int) -> list[int]:
        return [self.u32(address + 4 * i) for i in range(count)]

    def mapped(self, address: int) -> bool:
        try:
            self.image.offset(address)
        except game_tables.GameTablesError:
            return False
        return True


def _round(value: float) -> float:
    return round(value, 6)


def _node(reader: _Reader, address: int, depth: int) -> dict:
    if depth > MAX_DEPTH:
        raise RobotTableError(f"Skeleton deeper than {MAX_DEPTH} at 0x{address:08x}")
    node = {
        "type": reader.s32(address),
        "flags": reader.u32(address + 4),
        "track": reader.s32(address + 8),
        "translation": [_round(reader.f32(address + 4 * i)) for i in range(3, 6)],
        "rotation": [_round(reader.f32(address + 4 * i)) for i in range(6, 9)],
        "bone": reader.s32(address + 40),
    }
    children = []
    child_list = reader.u32(address + 36)
    # Lists outside the file image are filled at run time and start empty.
    while child_list and reader.mapped(child_list):
        child = reader.u32(child_list)
        if child == 0:
            break
        if child != 0xFFFFFFFF:  # the arm node's placeholders
            children.append(_node(reader, child, depth + 1))
        child_list += 4
    if children:
        node["children"] = children
    return node


def _skeletons(reader: _Reader, table: int, count: int) -> list[dict | None]:
    trees: list[dict | None] = []
    for index in range(count):
        root = reader.u32(table + 4 * index)
        trees.append(_node(reader, root, 0) if root else None)
    return trees


def _part_types(reader: _Reader, table: tuple[int, int, int], byte: bool = False) -> list[int]:
    base, size, count = table
    if byte:
        return [reader.u32(base + size * i + 4) & 0xFF for i in range(count)]
    return [reader.s32(base + size * i + 8) for i in range(count)]


def _paints(reader: _Reader) -> list[list[int]]:
    paints = []
    for leg_type in range(LEG_TYPES):
        textures = reader.u32(PAINTS + 8 * leg_type)
        paints.append(reader.words(textures, PAINTS_PER_LEG_TYPE) if textures else [])
    return paints


def _timings(reader: _Reader, leg_type: int) -> list[list[float]]:
    """Per motion: first frame, loop start, loop end and last frame."""
    base = MOTION_TIMINGS + leg_type * MOTIONS_PER_BANK * 16
    return [
        [_round(reader.f32(base + 16 * motion + 4 * i)) for i in range(4)]
        for motion in range(MOTIONS_PER_BANK)
    ]


def catalogue(data: bytes) -> dict:
    """Every part's type and every type's models, skeletons, motions and paints."""
    reader = _Reader(data)
    return {
        "parts": {
            "body": _part_types(reader, BODY_PARTS, byte=True),
            "legs": _part_types(reader, LEG_PARTS),
            "arm": _part_types(reader, ARM_PARTS),
            "shoulder": _part_types(reader, SHOULDER_PARTS),
        },
        "body_types": [
            {
                "model": reader.u32(BODY_MODELS + 4 * t),
                "emblem_model": reader.u32(BODY_EMBLEM_MODELS + 4 * t),
                "motions": reader.u32(BODY_MOTIONS + 4 * t),
                "forearm_turn": -_round(reader.f32(FOREARM_TURN)) if t == FOREARM_TURN_BODY_TYPE else 0.0,
            }
            for t in range(BODY_TYPES)
        ],
        "leg_types": [
            {
                "model": reader.u32(LEG_MODELS + 4 * t),
                "motions": reader.u32(LEG_MOTIONS + 4 * t),
                "arm_motions": [reader.words(table + 8 * t, 2) for table in ARM_MOTIONS],
                "paints": paints,
                "timings": _timings(reader, t),
                "radius": _round(reader.f32(LEG_RADII + 8 * t + 4)),
                "shadow_mesh": reader.u32(LEG_SHADOWS + 4 * t),
            }
            for t, paints in enumerate(_paints(reader))
        ],
        "arm_types": [
            {
                "models": [reader.u32(table + 4 * t) for table in ARM_MODELS],
                "class": reader.u32(ARM_CLASSES + 4 * t),
            }
            for t in range(ARM_TYPES)
        ],
        "shoulder_types": [
            {"models": [reader.u32(table + 4 * t) for table in SHOULDER_MODELS]}
            for t in range(SHOULDER_TYPES)
        ],
        "emblems": reader.words(EMBLEM_TEXTURES, EMBLEMS),
    }


def skeletons(data: bytes) -> dict:
    reader = _Reader(data)
    return {
        "legs": _skeletons(reader, LEG_SKELETONS, LEG_TYPES),
        "arms": [_skeletons(reader, table, ARM_TYPES) for table in ARM_SKELETONS],
    }


def referenced_resources(tables: dict) -> dict[str, set[int]]:
    """The model, texture and motion resources the catalogue names."""
    models: set[int] = set()
    textures: set[int] = set(tables["emblems"])
    motions: set[int] = set()
    for body in tables["body_types"]:
        models.update((body["model"], body["emblem_model"]))
        motions.add(body["motions"])
    for legs in tables["leg_types"]:
        models.add(legs["model"])
        motions.add(legs["motions"])
        for pair in legs["arm_motions"]:
            motions.update(pair)
        textures.update(legs["paints"])
    for arm in tables["arm_types"]:
        models.update(arm["models"])
    for shoulder in tables["shoulder_types"]:
        models.update(shoulder["models"])
    motions.discard(0)
    return {"models": models, "textures": textures, "motions": motions}
