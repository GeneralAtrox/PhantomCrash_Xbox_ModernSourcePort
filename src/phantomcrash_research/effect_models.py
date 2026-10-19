"""The models and pictures the battle's effects draw, for ``tables/effects.json``.

Besides the particles (``effect_bank``), a battle draws:

- flashes (``FLASHES``, 4 records of 0x20 bytes, started by ``0x0659e5`` from
  an effect group's flash list): a flip-book of texture slots, one a frame,
  drawn by ``0x065ade`` on ``grid_mdl`` (model slot 0x4db, a unit quad)
  facing the camera at the group's position and scaled by the scale the
  group was started with. The record's +0x10 picks the blend: 0 draws
  through ``0x0b0124`` (additive, flags | 0x220), 1 through ``0x0affeb``
  (alpha). Each has an update (table ``0x215ec8``) that sets its colour and
  scale every frame (``FLASH_UPDATES``).
- projectile looks (``LOOKS``, 18 records of 0x2c bytes, indexed by weapon
  record +0x22 and drawn by ``0x09831e``): the model a shot is drawn with,
  scaled, and for missiles an exhaust model (slot ``EXHAUST_SLOT``) behind it
  and a smoke trail.
- impacts (``IMPACTS``, 13 records of 12 bytes, indexed by weapon record
  +0x23, started by ``0x098eda`` through ``0x065f28``): the effect group a
  landing shot starts, the scale of its flashes, and whether it leaves a
  bullet hole (``DECALS``, ``0x0663d2``).

Model slots name one mesh of a model resource, texture slots one texture of
a texture pack (the resource table's slot records, ``game_tables``). The
converter resolves both to the converted files. Field names are
researcher-assigned.
"""

from __future__ import annotations

import struct

from . import game_tables

FLASHES = 0x215E48
FLASH_COUNT = 4
FLASH_SIZE = 0x20
FLASH_UPDATES = ("fade", "rise", "pulse", "still")  # 0x0666be, 0x066770, 0x0668ab, 0x066936
LOOKS = 0x2534A8
LOOK_COUNT = 18
LOOK_SIZE = 0x2C
IMPACTS = 0x253408
IMPACT_COUNT = 13
IMPACT_SIZE = 12
EXHAUST_SLOT = 0x3F3  # the missile flame, drawn by 0x09831e behind a look with a trail
BEAM_SLOT = 0x3A5  # 0x09b9fe: a unit quad stretched from the muzzle to the beam's tip
BEAM_WIDTH = 0.25  # 0x09b9fe scales the quad by (length, 0.25, 1)
BEAM_FADE_FRAMES = 8  # 0x17beec: the beam brightens 0x20 a frame held until fully white
DECAL_SLOTS = (0xDB, 0x104)  # 0x065ade: bullet hole kinds 0 and 1
DECAL_SCALE = 0.25  # 0x065f28 passes it to 0x0663d2
DECAL_COUNT = 100  # the ring 0x0663d2 writes
WRECK_GROUPS = (0x34, 0x35, 0x36, 0x0F)  # 0x07675e: a robot's armour gone (0x34-0x36 kept)
FENCE_FIRST_SLOT = 0x3FD  # 0x071869: 0x41b - remaining (30 down to 1)
FENCE_FRAMES = 30
FENCE_CAPACITY = 20  # 0x07027b
NO_SLOT = -1
DECIMALS = 4


def _models(tables: game_tables.GameTables) -> list[game_tables.Resource]:
    return [r for r in tables.resources if r.first_mesh_slot is not None and r.mesh_count]


def _packs(tables: game_tables.GameTables) -> list[game_tables.Resource]:
    return [r for r in tables.resources if r.type == game_tables.RESOURCE_TEXTURE_PACK and r.first_slot is not None]


def model_slot(tables: game_tables.GameTables, slot: int) -> dict | None:
    """The converted model and mesh that fill model ``slot``."""
    for resource in _models(tables):
        if resource.first_mesh_slot <= slot < resource.first_mesh_slot + resource.mesh_count:
            return {"model": f"models/binlink/{resource.id}.gltf", "mesh": slot - resource.first_mesh_slot}
    return None


def texture_slot(tables: game_tables.GameTables, slot: int) -> str | None:
    """The converted texture that fills texture ``slot``."""
    for resource in _packs(tables):
        if resource.first_slot <= slot < resource.first_slot + resource.slot_count:
            return f"textures/binlink/{resource.id}/{slot - resource.first_slot}.png"
    return None


def model_resources(tables: game_tables.GameTables, slots: list[int]) -> set[int]:
    """The model resources behind ``slots``, to convert with their texture packs."""
    ids = set()
    for resource in _models(tables):
        if any(resource.first_mesh_slot <= s < resource.first_mesh_slot + resource.mesh_count for s in slots):
            ids.add(resource.id)
    return ids


def texture_resources(tables: game_tables.GameTables, slots: list[int]) -> set[int]:
    ids = set()
    for resource in _packs(tables):
        if any(resource.first_slot <= s < resource.first_slot + resource.slot_count for s in slots):
            ids.add(resource.id)
    return ids


def _flash(tables, image: game_tables._Image, index: int) -> tuple[dict, list[int]]:
    first, end, step, billboard, blended, draw_flags = struct.unpack_from(
        "<iiiiiI", image.data, image.offset(FLASHES + index * FLASH_SIZE, 24)
    )
    slots = [first + frame for frame in range(end)] if first != NO_SLOT else []
    flash = {
        "id": index,
        "frames": [texture_slot(tables, s) for s in slots],
        "step": step,
        "billboard": bool(billboard),
        "additive": not blended,
        "update": FLASH_UPDATES[index],
        "draw_flags": f"0x{draw_flags:x}",
    }
    return flash, slots


def _look(tables, image: game_tables._Image, index: int) -> tuple[dict, list[int], list[int]]:
    address = LOOKS + index * LOOK_SIZE
    billboard, _, _, model, sprite, trail = struct.unpack_from("<6i", image.data, image.offset(address, 24))
    scale, exhaust_offset, *exhaust_scale = struct.unpack_from("<5f", image.data, image.offset(address + 24, 20))
    look = {
        "id": index,
        "billboard": bool(billboard),
        "model": model_slot(tables, model) if model != NO_SLOT else None,
        "sprite": texture_slot(tables, sprite) if sprite != NO_SLOT else None,
        "scale": round(scale, DECIMALS),
        "trail": bool(trail),
    }
    if trail:
        look["exhaust"] = {
            "offset": round(exhaust_offset, DECIMALS),
            "scale": [round(v, DECIMALS) for v in exhaust_scale],
        }
    models = [model] if model != NO_SLOT else []
    if trail:
        models.append(EXHAUST_SLOT)
    return look, models, ([sprite] if sprite != NO_SLOT else [])


def _impact(image: game_tables._Image, index: int) -> dict:
    scale, group, decal = struct.unpack_from("<fiB", image.data, image.offset(IMPACTS + index * IMPACT_SIZE, 9))
    return {"id": index, "group": group, "flash_scale": round(scale, DECIMALS), "decal": bool(decal)}


def effect_models(executable: bytes) -> tuple[dict, set[int], set[int]]:
    """The tables for ``effects.json``, and the model and texture resources they draw."""
    tables = game_tables.load(executable)
    image = game_tables._Image(executable)
    model_slots: list[int] = list(range(FENCE_FIRST_SLOT, FENCE_FIRST_SLOT + FENCE_FRAMES))
    texture_slots: list[int] = [*DECAL_SLOTS, 0x16D]  # missile smoke, 0x071a04
    flashes = []
    for index in range(FLASH_COUNT):
        flash, textures = _flash(tables, image, index)
        flashes.append(flash)
        texture_slots += textures
    looks = []
    for index in range(LOOK_COUNT):
        look, models, textures = _look(tables, image, index)
        looks.append(look)
        model_slots += models
        texture_slots += textures
    model_slots.append(BEAM_SLOT)
    out = {
        "flashes": flashes,
        "projectiles": looks,
        "exhaust": model_slot(tables, EXHAUST_SLOT),
        "beam": {**model_slot(tables, BEAM_SLOT), "width": BEAM_WIDTH, "fade_frames": BEAM_FADE_FRAMES},
        "missile_trail": {
            "texture": texture_slot(tables, 0x16D), "points": 33, "capacity": 50,
            "sample_hz": 15, "fade_seconds": 1.0, "head_width": 0.1, "tail_width": 1.5,
        },
        "impacts": [_impact(image, index) for index in range(IMPACT_COUNT)],
        "wreck_groups": list(WRECK_GROUPS),
        "fence": {
            "model": model_slot(tables, FENCE_FIRST_SLOT)["model"],
            "meshes": [model_slot(tables, slot)["mesh"] for slot in model_slots[:FENCE_FRAMES]],
            "capacity": FENCE_CAPACITY, "frame_rate": 30,
            "scale": round(struct.unpack_from("<f", image.data, image.offset(0x17AC38))[0], 6),
            "growth": round(struct.unpack_from("<f", image.data, image.offset(0x17BEC4))[0], 6),
            "depth": 0.2, "offset": -0.5,
            # effopt's sphere is set at the hit with radius zero (0x071869).
            # The refraction parameters are shared with 0x0c05c2's optical path.
            "refraction": {"edge": 1.0, "reach": 4.0, "push": 1.0, "least": 0.25},
        },
        "decals": {
            "textures": [texture_slot(tables, s) for s in DECAL_SLOTS],
            "scale": DECAL_SCALE,
            "count": DECAL_COUNT,
            "depth_offset": 0.01,
            "scorch_scale": 1.0,
        },
    }
    return out, model_resources(tables, model_slots), texture_resources(tables, texture_slots)
