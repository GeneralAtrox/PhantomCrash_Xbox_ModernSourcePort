"""Convert the robots: part tables, skeletons, models, paints and motions.

Output, under the converted assets:

- ``robots/catalogue.json``: part types and what each type uses
  (:func:`robots.catalogue`).
- ``robots/weapon_mounts.json``: ordered muzzle points and their model-node
  transforms, with drawing-node bone indices (:mod:`weapon_mounts`).
- ``robots/movement_mounts.json``: exhaust mounts and movement/contact effect groups.
- ``robots/skeletons.json``: the leg and arm skeletons.
- ``models/binlink/<resource>.gltf``: every robot part model (mesh 0).
  Robot batches draw texture 0, the robot's own paint, which the game binds
  per robot; the glTF points at the first paint of the part's line and the
  port swaps in the robot's paint. A few batches (rollers, Gatling barrels)
  name texture 1 or 2: robot ``n``'s paint sits in slot ``0x20a + n`` and a
  batch's texture is added to its robot's slot (``0x0b3240``), so they read
  a neighbour's paint, whose layout they are mapped to. They get the paint.
- ``textures/binlink/<resource>.png``: every paint and emblem texture.
- ``models/binlink/216.gltf``: the first-person cockpit (``viewer_mdl``'s
  neighbour in the resource table, 21 meshes from object slot 0x4dd, drawn
  by ``0x04c459``), every mesh kept, with its texture pack 1590.
- ``models/binlink/197_30.gltf`` and ``textures/systemdata/muteki01.png``:
  the entry shield (:mod:`effect_convert`).
- ``models/binlink/207.gltf`` and ``208.gltf``: the supply carrier and its
  pickups on even and odd stages (``0x069d52``), with packs 1584 and 1585.
- ``textures/binlink/<resource>/<index>.png``: the binlink texture packs
  that fill 2D sprite slots (the music jackets and artist portraits),
  named like the disc's packs; ``tables/sprites.json``
  lists them as the slots' ``binlink/<resource>`` packs.
- ``motions/<resource>.json``: every motion bank (:mod:`motion`).
- ``tables/lessons.json``: Pepper's in-battle lesson messages, and
  ``enepath/<name>.json``: the drones' paths (:mod:`battle_data`).
- ``collision/<name>.json`` (``collision/binlink/<resource>.json`` for
  resources in a pack): each stage's collision triangles (:mod:`collision`).
- ``models/stages``, ``textures/stages``, the areas' ``enepath`` files and
  ``tables/stages.json``: the city battle stages (:mod:`stage_convert`).
- ``tables/environment.json``: each stage's fog and light presets
  (:mod:`environment`).
- ``tables/parts.json``: the AI chip and weapon records (:mod:`parts`),
  ``tables/chatter.json``: the battle chatter (:mod:`chatter`), and
  ``tables/battle_scenes.json``: the battle's entrance and exit scenes and
  the stages' gates and gate doors (:mod:`battle_scenes`), and
  ``tables/finale.json``: the epilogue after the First Ranker's contest
  (:mod:`finale`), and ``tables/ending_subtitles.json``: the ending
  movie's subtitles (:mod:`finale`).

Everything comes out of the binlink packs (``Media/binlink``), which are
sliced by the resource table's pack offsets.
"""

from __future__ import annotations

import json
import struct
from pathlib import Path
from typing import Callable

from . import arena_monitor, battle_data, battle_scenes, chatter, collision, effect_bank, effect_convert, environment, finale, game_tables, gltf, model, motion, parts, png, robots, stage_convert, texture_pack, weapon_mounts, movement_mounts


def _write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, separators=(",", ":")) + "\n", encoding="utf-8")


class _Packs:
    """Reads binlink resources, keeping each pack file in memory once."""

    def __init__(self, media: Path, tables: game_tables.GameTables):
        self.media = media
        self.names = tables.binlink_packs
        self.resources = {resource.id: resource for resource in tables.resources}
        self.files: dict[int, bytes] = {}

    def read(self, resource_id: int) -> bytes | None:
        resource = self.resources.get(resource_id)
        if resource is None or resource.binlink_pack is None or resource.binlink_offset is None:
            return None
        pack = resource.binlink_pack
        if pack not in self.files:
            path = self.media / self.names[pack]
            if not path.is_file():
                return None
            self.files[pack] = path.read_bytes()
        start = resource.binlink_offset
        return self.files[pack][start : start + resource.size]

    def pack_stem(self, resource_id: int) -> str | None:
        pack = self.pack_of(resource_id)
        return None if pack is None else Path(self.names[pack]).stem.lower()

    def pack_of(self, resource_id: int) -> int | None:
        resource = self.resources.get(resource_id)
        return None if resource is None else resource.binlink_pack


def _default_paints(catalogue: dict, packs: _Packs) -> dict[int, int]:
    """Pack -> a paint texture for models of that pack (a line's parts share
    one UV layout), and each emblem model's first emblem."""
    paints: dict[int, int] = {}
    for legs in catalogue["leg_types"]:
        pack = packs.pack_of(legs["model"])
        if pack is not None and legs["paints"]:
            paints.setdefault(pack, legs["paints"][0])
    return paints


def _convert_textures(ids: set[int], packs: _Packs, output: Path, report) -> int:
    written = 0
    for resource_id in sorted(ids):
        data = packs.read(resource_id)
        if data is None:
            continue
        for texture in texture_pack.parse(data):
            fmt = texture.format
            name = f"{resource_id}.png" if texture.index == 0 else f"{resource_id}_{texture.index}.png"
            png.write_rgba(output / "textures" / "binlink" / name, fmt.width, fmt.height, texture.rgba(data))
            written += 1
    report(f"{written:>4} robot textures")
    return written


def _convert_slot_textures(tables: game_tables.GameTables, packs: _Packs, output: Path, report) -> int:
    """The binlink texture packs that fill sprite slots, as the disc's packs are written."""
    written = 0
    for resource in game_tables.sprite_binlink_packs(tables):
        data = packs.read(resource.id)
        if data is None:
            continue
        for texture in texture_pack.parse(data):
            fmt = texture.format
            target = output / "textures" / "binlink" / str(resource.id) / f"{texture.index}.png"
            png.write_rgba(target, fmt.width, fmt.height, texture.rgba(data))
            written += 1
    report(f"{written:>4} binlink sprite textures")
    return written


# Robots' paint slots (0x20a..0x20f): a batch's texture past its own paint
# reads another robot's.
ROBOT_PAINT_SLOTS = 6


def _convert_models(ids: set[int], catalogue: dict, packs: _Packs, output: Path, report) -> list[dict]:
    paints = _default_paints(catalogue, packs)
    emblem_models = {body["emblem_model"] for body in catalogue["body_types"]}
    records = []
    for resource_id in sorted(ids):
        data = packs.read(resource_id)
        if data is None:
            continue
        texture = catalogue["emblems"][0] if resource_id in emblem_models else paints.get(packs.pack_of(resource_id))
        uris = [f"../../textures/binlink/{texture}.png"] * ROBOT_PAINT_SLOTS if texture is not None else None
        target = output / "models" / "binlink" / f"{resource_id}.gltf"
        summary = gltf.write(model.parse(data), target, uris, mesh_limit=1)
        records.append({"resource": resource_id, "paint": texture, **summary})
    report(f"{len(records):>4} robot models")
    return records


# Binlink models with their texture packs: the cockpit, the supply
# carrier with its pickups (day and night stages, model slots 0x49e and
# 0x4ad: the carrier is mesh 0, pickup type t mesh 5 + t or 6 + t) and the
# robots' ground shadows (slots 0-20, one flat quad per leg shape).
HUD_MODELS = {216: 1590, 207: 1584, 208: 1585, 184: 1555}


def _convert_hud_models(packs: _Packs, output: Path, report) -> list[dict]:
    records = []
    for resource_id, pack_id in sorted(HUD_MODELS.items()):
        data = packs.read(resource_id)
        textures = packs.read(pack_id)
        if data is None or textures is None:
            continue
        uris = []
        for texture in texture_pack.parse(textures):
            fmt = texture.format
            name = f"{pack_id}.png" if texture.index == 0 else f"{pack_id}_{texture.index}.png"
            png.write_rgba(output / "textures" / "binlink" / name, fmt.width, fmt.height, texture.rgba(textures))
            uris.append(f"../../textures/binlink/{name}")
        target = output / "models" / "binlink" / f"{resource_id}.gltf"
        summary = gltf.write(model.parse(data), target, uris)
        records.append({"resource": resource_id, "textures": pack_id, **summary})
    report(f"{len(records):>4} HUD models")
    return records


def _convert_motions(ids: set[int], packs: _Packs, output: Path, report) -> int:
    written = 0
    for resource_id in sorted(ids):
        data = packs.read(resource_id)
        if data is None:
            continue
        _write_json(output / "motions" / f"{resource_id}.json", motion.parse(data))
        written += 1
    report(f"{written:>4} motion banks")
    return written


def _convert_collision(
    executable: bytes, tables: game_tables.GameTables, media: Path, packs: _Packs, output: Path, report
) -> list[dict]:
    image = game_tables._Image(executable)
    count = collision.STAGE_COUNT
    stage_ids = struct.unpack_from(
        f"<{count}I", image.data, image.offset(collision.STAGE_COLLISION, 4 * count)
    )
    resources = {resource.id: resource for resource in tables.resources}
    records = []
    for resource_id in sorted(set(stage_ids)):
        resource = resources[resource_id]
        if resource.path is not None:
            source = media / resource.path
            data = source.read_bytes() if source.is_file() else None
            name = Path(resource.path).stem.lower()
        else:
            data = packs.read(resource_id)
            name = f"binlink/{resource_id}"
        if data is None:
            continue
        parsed = collision.parse(data)
        _write_json(output / "collision" / f"{name}.json", parsed)
        stages = [stage for stage, stage_id in enumerate(stage_ids) if stage_id == resource_id]
        records.append(
            {
                "resource": resource_id,
                "file": f"collision/{name}.json",
                "stages": stages,
                "walls": len(parsed["walls"]),
                "floor": len(parsed["floor"]),
            }
        )
    report(f"{len(records):>4} stage collisions")
    return records


def convert_robots(
    media: Path,
    executable: bytes,
    tables: game_tables.GameTables,
    output: Path,
    report: Callable[[str], None],
) -> dict:
    catalogue = robots.catalogue(executable)
    _write_json(output / "robots" / "catalogue.json", catalogue)
    _write_json(output / "robots" / "skeletons.json", robots.skeletons(executable))
    wanted = robots.referenced_resources(catalogue)
    packs = _Packs(media, tables)
    _write_json(output / "robots" / "movement_mounts.json",
                movement_mounts.extract(executable, catalogue, packs))
    _write_json(output / "robots" / "weapon_mounts.json",
                weapon_mounts.extract(executable, catalogue, packs))
    _write_json(output / "tables" / "lessons.json", battle_data.lesson_messages(executable, tables))
    _write_json(output / "tables" / "environment.json", environment.stage_environments(executable))
    _write_json(output / "tables" / "parts.json", parts.part_tables(executable, tables))
    _write_json(output / "tables" / "chatter.json", chatter.chatter(executable, tables))
    _write_json(output / "tables" / "battle_scenes.json", battle_scenes.battle_scenes(executable))
    _write_json(output / "tables" / "finale.json", finale.finale(executable, tables))
    _write_json(output / "tables" / "ending_subtitles.json", finale.ending_subtitles(executable))
    paths = sorted((media / "enepath").glob("*_enepath.bin"))
    for path in paths:
        name = path.name.lower().removesuffix(".bin")
        _write_json(output / "enepath" / f"{name}.json", battle_data.enemy_path(path.read_bytes()))
    return {
        "enemy_paths": len(paths),
        "models": _convert_models(wanted["models"], catalogue, packs, output, report),
        "hud_models": _convert_hud_models(packs, output, report),
        "arena_monitor": arena_monitor.convert_ship(executable, packs.read, output, _write_json),
        "effect_models": effect_convert.convert_effects(media, packs.read, output, report),
        "effect_bank": effect_bank.convert_effect_bank(executable, packs.read, output, _write_json, report),
        "textures": _convert_textures(wanted["textures"], packs, output, report),
        "slot_textures": _convert_slot_textures(tables, packs, output, report),
        "motion_banks": _convert_motions(wanted["motions"], packs, output, report),
        "collision": _convert_collision(executable, tables, media, packs, output, report),
        "stages": stage_convert.convert_stages(
            executable, packs.read, packs.pack_stem, output, _write_json, report
        ),
    }
