"""Convert the particle effects into ``tables/effects.json`` and their textures.

The game builds every spark, puff and burst from three tables in the
executable, and draws them all from one texture atlas in ``c00_effect.bin``
(binlink pack 7):

- groups (``GROUPS``, 79 of 8 bytes): what a caller starts through
  ``0x057760(group, owner, scale, position, ...)``. Each names a list of
  scripts ending in -1, and a list of flash ids ending in -2 that go to
  ``0x0659e5`` (``effect_models``, which also adds the shots' looks and
  impacts).
- scripts (``SCRIPTS``, 62 pointers): each a list of (flags, definition)
  pairs, one emitter per pair, the list ending with flag ``END``. The flags
  become the emitter's flags word (``+0x10``) and choose how its particles
  start, move and draw (``SCRIPT_FLAGS``; names are researcher-assigned).
- definitions (0x150 bytes each, between ``0x20d720`` and ``0x215cf8``): the
  emitter's tuning, read by the spawn (``0x060b7c``), the motion
  (``0x061d35``, ``0x063c16``) and the draw (``0x0625e4``).

Curves are lists of (time, value) keys ending at a negative time, read
with linear interpolation; colour curves hold bytes. Times count frames
(``curve time`` modes: 0 none, 1 particle age, 2 emitter age, 3 percent of
life). A battle runs its effects at 30 frames a second (``0x255ab0``),
which the port converts to seconds; speeds, rates and forces stay in the
original's per-second units here.

The atlas cells (``ATLAS_CELLS``) are the corners a static constructor at
``0x11d060`` writes to ``0x9660e0``; each cell spans one eighth of the
texture across and a quarter down. Every field name below is
researcher-assigned from the code that reads it.
"""

from __future__ import annotations

import struct
from pathlib import Path

from . import effect_models, game_tables, gltf, png, texture_pack
from . import model as mdl

GROUPS = 0x205C10
GROUP_COUNT = 79
SCRIPTS = 0x215928
SCRIPT_COUNT = 62
DEFINITION_SIZE = 0x150
END_SCRIPTS = 0xFFFFFFFF  # -1 ends a group's script list
END_FLASHES = 0xFFFFFFFE  # -2 ends its flash list
END = 0x200000  # script flag: the last pair
ATLAS_CELLS = 0x11D060  # constructor filling the cell corners at 0x9660e0
ATLAS_CELL_COUNT = 22
ATLAS_CELL_SIZE = (0.125, 0.25)
ATLAS_RESOURCE = 1589  # texture slot 400, bound for the particles
GLOW_RESOURCE = 1574  # texture slot 0x13f, the supply pickups' glow
EFFECT_RATE = 30  # frames a second in a battle (0x255ab0)
HUE_DEGREES = 360.0
PERCENT = 100.0
BYTE = 255.0

SCRIPT_FLAGS = {
    0x2: "spawn",
    0x4: "replaceable",
    0x8: "inherit_velocity",
    0x10: "exact_spread",
    0x20: "radial_push",
    0x40: "random_direction",
    0x80: "spin",
    0x100: "streak",
    0x200: "streak_long",
    0x400: "streak_short",
    0x800: "random_offset",
    0x1000: "fragments",
    0x2000: "detached",
    0x4000: "random_speed",
    0x8000: "swirl",
    0x10000: "die_at_target",
    0x20000: "debris",
    0x40000: "debris_kind",
    0x80000: "kept",
    0x100000: "streak_line",
}

SHAPES = {0: "point", 1: "point", 3: "sphere", 5: "cluster"}

# (offset, name, format) of the plain fields; 'f' float, 'i' int, 'b' byte.
# force_time, drag_time, mass_jitter and speed_time are never read by the
# game (spawn 0x060b7c, emitter 0x060107/0x06077e, motion 0x061d35,
# acceleration 0x063c16, draw 0x0625e4); they are named from the layout's
# value, time, jitter runs and kept so each record is complete.
FIELDS = (
    (0x00, "force_scale", "f"),
    (0x04, "force_time", "i"),
    (0x08, "force_jitter", "f"),
    (0x0C, "drag", "f"),
    (0x10, "drag_time", "i"),
    (0x14, "drag_jitter", "f"),
    (0x18, "additive", "b"),
    (0x1C, "no_gravity", "b"),
    (0x2C, "gravity", "f"),
    (0x30, "wind_off", "b"),
    (0x34, "wind", "f"),
    (0x44, "pull_off", "b"),
    (0x48, "pull", "f"),
    (0x4C, "pull_to_target", "b"),
    (0x50, "pull_range", "f"),
    (0x6C, "colour_time", "i"),
    (0x70, "alpha_time", "i"),
    (0x74, "life", "f"),
    (0x78, "life_jitter", "f"),
    (0x7C, "mass", "f"),
    (0x80, "mass_time", "i"),
    (0x84, "mass_jitter", "f"),
    (0x88, "size", "f"),
    (0x8C, "scale_time", "i"),
    (0x90, "size_jitter", "f"),
    (0x94, "min_speed", "f"),
    (0x98, "min_speed_time", "i"),
    (0x9C, "max_speed", "f"),
    (0xA0, "max_speed_time", "i"),
    (0xB0, "alpha", "f"),
    (0xC0, "alpha_extra", "f"),
    (0xC4, "shape_type", "i"),
    (0xC8, "shape_variant", "i"),
    (0xCC, "direction_mode", "i"),
    (0xF4, "rate", "f"),
    (0xF8, "rate_jitter", "f"),
    (0xFC, "spread", "f"),
    (0x100, "spread_jitter", "f"),
    (0x104, "speed", "f"),
    (0x108, "speed_jitter", "f"),
    (0x10C, "speed_time", "i"),
    (0x110, "push", "f"),
)
VECTORS = (
    (0x20, "gravity_angles"),
    (0x38, "wind_angles"),
    (0x54, "pull_angles"),
    (0x60, "target"),
    (0xA4, "colour"),
    (0xB4, "colour_jitter_hsv"),
    (0xD0, "offset"),
    (0xDC, "rotation"),
    (0xE8, "scale"),
)
# (offset, name, byte values)
CURVES = (
    (0x11C, "blue", True),
    (0x120, "green", True),
    (0x124, "red", True),
    (0x128, "alpha", True),
    (0x12C, "scale", False),
    (0x130, "pull", False),
    (0x134, "wind", False),
    (0x138, "rate", False),
    (0x13C, "mass", False),
    (0x140, "speed", False),
    (0x144, "min_speed", False),
    (0x148, "max_speed", False),
)
CELL = 0x114  # short: the first atlas cell
FRAMES = 0x116  # short: cells of animation, at 15 a second
CURVE_LIMIT = 64
DECIMALS = 5


def _round(value: float) -> float:
    return round(value, DECIMALS)


def _flags(value: int) -> list[str]:
    names = []
    for bit in range(32):
        mask = 1 << bit
        if value & mask and mask != END:
            names.append(SCRIPT_FLAGS.get(mask, f"bit_{bit}"))
    return names


def _curve(image: game_tables._Image, address: int, as_bytes: bool) -> list[list[float]]:
    keys = []
    for index in range(CURVE_LIMIT):
        at = image.offset(address + index * 8, 8)
        time = struct.unpack_from("<f", image.data, at)[0]
        if time < 0.0:
            break
        if as_bytes:
            value: float = image.data[at + 4]
        else:
            value = _round(struct.unpack_from("<f", image.data, at + 4)[0])
        keys.append([_round(time), value])
    return keys


def _hue_red(hue: float, saturation: float, value: float) -> float:
    """The red of an HSV colour as 0x0647fe works it out: the definitions'
    colour jitter is a brightness this large either way."""
    h = min(max(hue / HUE_DEGREES, 0.0), 1.0)
    s = min(max(saturation / PERCENT, 0.0), 1.0)
    v = min(max(value / PERCENT, 0.0), 1.0)
    wrapped = h - 1.0 if h > 0.5 else h
    red = min(max(2.0 - abs(wrapped) * 6.0, 0.0), 1.0)
    return ((1.0 - red) * (1.0 - s) + red) * v


def _definition(image: game_tables._Image, address: int) -> dict:
    raw = image.data[image.offset(address, DEFINITION_SIZE) :][:DEFINITION_SIZE]
    out: dict = {"address": f"0x{address:06x}"}
    for offset, name, kind in FIELDS:
        if kind == "f":
            out[name] = _round(struct.unpack_from("<f", raw, offset)[0])
        elif kind == "i":
            out[name] = struct.unpack_from("<i", raw, offset)[0]
        else:
            out[name] = bool(raw[offset])
    for offset, name in VECTORS:
        out[name] = [_round(v) for v in struct.unpack_from("<3f", raw, offset)]
    hsv = struct.unpack_from("<3f", raw, 0xB4)
    out["colour_jitter"] = _round(_hue_red(hsv[0] * HUE_DEGREES, hsv[1] * PERCENT, hsv[2] * PERCENT))
    out["shape"] = SHAPES.get(out["shape_type"], f"shape_{out['shape_type']}")
    out["cell"], out["frames"] = struct.unpack_from("<hh", raw, CELL)
    curves = {}
    for offset, name, as_bytes in CURVES:
        pointer = struct.unpack_from("<I", raw, offset)[0]
        if pointer:
            curves[name] = _curve(image, pointer, as_bytes)
    out["curves"] = curves
    return out


def _id_list(image: game_tables._Image, address: int, end: int) -> list[int]:
    ids = []
    while (value := image.u32(address)) != end:
        ids.append(value)
        address += 4
    return ids


def _atlas_cells(image: game_tables._Image) -> list[list[float]]:
    """Walks the constructor: push y, push x, mov ecx, cell, call."""
    at = image.offset(ATLAS_CELLS, 1) + 3  # after push ebp; mov ebp, esp
    data = image.data
    cells = []
    while len(cells) < ATLAS_CELL_COUNT:
        pushed = []
        while len(pushed) < 2:
            if data[at] == 0x6A:
                pushed.append(float(data[at + 1]))
                at += 2
            elif data[at] == 0x68:
                pushed.append(struct.unpack_from("<f", data, at + 1)[0])
                at += 5
            else:
                raise game_tables.GameTablesError("unexpected atlas constructor code")
        if data[at] != 0xB9 or data[at + 5] != 0xE8:
            raise game_tables.GameTablesError("unexpected atlas constructor code")
        at += 10
        y, x = pushed
        cells.append([_round(x), _round(y)])
    return cells


def effect_bank(executable: bytes) -> dict:
    return _bank(executable)[0]


def _bank(executable: bytes) -> tuple[dict, set[int], set[int]]:
    """The bank, and the model and texture resources its flashes and shots draw."""
    image = game_tables._Image(executable)
    definitions: dict[int, int] = {}
    scripts = []
    for script in range(SCRIPT_COUNT):
        address = image.u32(SCRIPTS + script * 4)
        emitters = []
        while True:
            flags, definition = struct.unpack_from("<2I", image.data, image.offset(address, 8))
            if definition:
                index = definitions.setdefault(definition, len(definitions))
                emitters.append({"flags": _flags(flags), "definition": index})
            if flags & END:
                break
            address += 8
        scripts.append({"id": script, "emitters": emitters})
    groups = []
    for group in range(GROUP_COUNT):
        script_list, flash_list = struct.unpack_from("<2I", image.data, image.offset(GROUPS + group * 8, 8))
        groups.append(
            {
                "id": group,
                "scripts": _id_list(image, script_list, END_SCRIPTS),
                "flashes": _id_list(image, flash_list, END_FLASHES),
            }
        )
    by_index = sorted(definitions.items(), key=lambda item: item[1])
    pictures, models, packs = effect_models.effect_models(executable)
    bank = {
        "frame_rate": EFFECT_RATE,
        "particle_motion": {
            "fragment_copies": 8, "fragment_step": 0.1, "fragment_jitter": 0.1,
            # 0x062115 stops debris on flat ground at 0.05 rather than
            # bouncing it. debris_stage_contacts settles it on the stage's
            # surfaces instead, with editable bounce/slide controls.
            "debris_restitution": 0.0, "debris_friction": 0.0,
            "debris_rest_speed": 0.02, "debris_clearance": 0.05,
            "debris_stage_contacts": False,
            # 0x060107 starts an emitter at its definition's offset before
            # 0x05fda8 moves it, so its first frame sweeps in from there.
            "start_at_offset": True,
        },
        "atlas": {
            "texture": f"textures/binlink/{ATLAS_RESOURCE}.png",
            "cell_size": list(ATLAS_CELL_SIZE),
            "cells": _atlas_cells(image),
        },
        "groups": groups,
        "scripts": scripts,
        "definitions": [_definition(image, address) for address, _ in by_index],
        **pictures,
    }
    return bank, models, packs


def _write_pack(read, resource_id: int, output: Path) -> int:
    data = read(resource_id)
    if data is None:
        return 0
    written = 0
    for texture in texture_pack.parse(data):
        fmt = texture.format
        name = f"{resource_id}.png" if texture.index == 0 else f"{resource_id}_{texture.index}.png"
        png.write_rgba(output / "textures" / "binlink" / name, fmt.width, fmt.height, texture.rgba(data))
        written += 1
    return written


def _write_slot_pack(read, resource_id: int, output: Path) -> int:
    """A texture pack whose textures fill slots: ``binlink/<id>/<index>.png``."""
    data = read(resource_id)
    if data is None:
        return 0
    for texture in texture_pack.parse(data):
        fmt = texture.format
        target = output / "textures" / "binlink" / str(resource_id) / f"{texture.index}.png"
        png.write_rgba(target, fmt.width, fmt.height, texture.rgba(data))
    return len(texture_pack.parse(data))


def _write_model(read, resource: game_tables.Resource, output: Path) -> bool:
    """A model resource with every mesh, over its texture pack."""
    data = read(resource.id)
    if data is None:
        return False
    uris = []
    if resource.texture_resource is not None:
        _write_pack(read, resource.texture_resource, output)
        textures = read(resource.texture_resource) or b""
        for texture in texture_pack.parse(textures):
            name = str(resource.texture_resource) if texture.index == 0 else f"{resource.texture_resource}_{texture.index}"
            uris.append(f"../../textures/binlink/{name}.png")
    gltf.write(mdl.parse(data), output / "models" / "binlink" / f"{resource.id}.gltf", uris)
    return True


def convert_effect_bank(executable: bytes, read, output: Path, write_json, report) -> dict:
    """``read`` gives a binlink resource's bytes (robot_convert's packs)."""
    bank, models, packs = _bank(executable)
    write_json(output / "tables" / "effects.json", bank)
    textures = _write_pack(read, ATLAS_RESOURCE, output) + _write_pack(read, GLOW_RESOURCE, output)
    textures += sum(_write_slot_pack(read, pack, output) for pack in sorted(packs))
    resources = game_tables.load(executable).resources
    written = sum(_write_model(read, resources[model], output) for model in sorted(models))
    report(f"{len(bank['definitions']):>4} effect emitters, {written} effect models")
    return {
        "groups": len(bank["groups"]),
        "scripts": len(bank["scripts"]),
        "definitions": len(bank["definitions"]),
        "textures": textures,
        "models": written,
    }
