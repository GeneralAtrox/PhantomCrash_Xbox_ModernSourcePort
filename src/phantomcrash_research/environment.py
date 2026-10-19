"""Stage environments: each battle stage's fog and light presets.

The battle light (0x044e9f) and fog (0x0a1590) read one 0x54-byte preset
from the list the stage's pointer at ``PRESETS`` names; the index into that
list (0x41699c) stays 0 in every path found, so each stage uses its first
preset. Preset names ("Fix1", "Default1", "Mizuko1", ...) are the shipped
data's own. The light's direction is a separate per-stage vector.
"""

from __future__ import annotations

import struct

from . import game_tables

PRESETS = 0x2014C8  # preset list pointer per stage
SUN_DIRECTIONS = 0x2015F8  # f32[3] per stage: the way the light travels
STAGE_COUNT = 8
PRESET_SIZE = 0x54
PRESET_LIMIT = 32  # a bound on the walk; the lists end at the first record without a name
# name, fog colour, density, start, end, mode, light colour, diffuse and
# ambient scale, 4 ints packed ARGB into 0x25b490 (the tint the screen
# pass 0x0b7900 multiplies the frame by), per-channel levels (min rgb, max rgb) for the gamma
# ramp (0x044974), a flag, and a value stored to 0x416930.
PRESET = struct.Struct("<2I3f2I2f4i6iIf")
FOG_MODES = ("none", "linear", "exp", "exp2")  # D3DFOG_* (render state 0x8a)
DECIMALS = 4


def _colour(value: int) -> list[int]:
    return [(value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF]


def _round(value: float) -> float:
    return round(value, DECIMALS)


def _preset(image: game_tables._Image, address: int) -> dict | None:
    try:
        raw = image.data[image.offset(address, PRESET_SIZE) :][: PRESET.size]
    except game_tables.GameTablesError:
        return None
    fields = PRESET.unpack(raw)
    name = image.string(fields[0])
    if name is None or fields[5] >= len(FOG_MODES):
        return None
    fog_colour, density, start, end, mode, light, diffuse, ambient = fields[1:9]
    return {
        "name": name,
        "fog": {
            "mode": FOG_MODES[mode],
            "colour": _colour(fog_colour),
            "density": _round(density),
            "start": _round(start),
            "end": _round(end),
        },
        "light": {"colour": _colour(light), "diffuse": _round(diffuse), "ambient": _round(ambient)},
        "stencil_argb": list(fields[9:13]),
        "levels": {"min": list(fields[13:16]), "max": list(fields[16:19])},
        "flag": fields[19],
        "value_0x50": _round(fields[20]),
    }


def stage_environments(executable: bytes) -> list[dict]:
    """Each stage's light direction and its presets, first one in use."""
    image = game_tables._Image(executable)
    stages = []
    for stage in range(STAGE_COUNT):
        direction = struct.unpack_from("<3f", image.data, image.offset(SUN_DIRECTIONS + 12 * stage, 12))
        first = image.u32(PRESETS + 4 * stage)
        presets = []
        for index in range(PRESET_LIMIT):
            preset = _preset(image, first + index * PRESET_SIZE)
            if preset is None:
                break
            presets.append(preset)
        stages.append({"stage": stage, "sun_direction": [_round(v) for v in direction], "presets": presets})
    return stages
