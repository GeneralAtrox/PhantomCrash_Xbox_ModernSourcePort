"""Convert extracted disc files into the open formats the port loads.

Output (``artifacts/europe/assets`` by default):

- ``textures/<pack>/<index>.png``: every texture of every ``*_tex.bin`` pack.
- ``models/model/<name>.gltf`` (and ``.bin``): every standalone model as
  glTF 2.0, its materials pointing at the PNGs of its texture pack.
- ``sound/sfx/<bank>/<index>.wav``: every effect of every SE BIN bank, as PCM.
- ``robots/``, ``models/binlink/``, ``textures/binlink/``, ``motions/``: the
  robots' part tables, skeletons, models, paints and motion banks
  (:mod:`robot_convert`).
- ``tables/*.json``: resources, sprites, music (with credits and the volume
  curve), movie and system font tables from the XBE, and the story events
  with their scripts (``events.json``) and talk backgrounds and characters
  (``talk.json``).
- ``tables/text.json``: the game's own text that the port's data files
  reference as ``{xbe:0x...}`` (see :func:`write_game_text`).
- ``manifest.json``: what was converted from which input.

Movies (``.sfd``) and music (PCM ``.wav``) are already open formats; the port
reads them from the extracted ``files/Media`` tree, so they are not copied.
"""

from __future__ import annotations

from dataclasses import asdict
import json
from pathlib import Path
import re
import shutil
from typing import Callable

from . import audio, events, game_tables, garage_tables, gltf, model, png, robot_convert, texture_pack, town_tables

MANIFEST = "manifest.json"
FORMAT_VERSION = 1
TEXTURE_DIRECTORIES = ("scroll", "texture")
GAME_TEXT = "tables/text.json"
GAME_TEXT_REFERENCE = re.compile(r"\{xbe:0x([0-9A-Fa-f]+)\}")
# The data files whose references the conversion reads: the shipped data and
# the mods overlaying it (game_text_sources in the port).
REPOSITORY = Path(__file__).resolve().parents[2]
GAME_TEXT_SOURCES = (REPOSITORY / "data", REPOSITORY / "mods" / "data")


def _write_json(path: Path, data: object, readable_text: bool = False) -> None:
    """``readable_text`` keeps non-ASCII text as UTF-8 rather than escapes."""
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(data, indent=1, ensure_ascii=not readable_text)
    path.write_text(text + "\n", encoding="utf-8")


def _prepare_output(output: Path, replace: bool) -> None:
    if output.exists() and any(output.iterdir()):
        if not replace:
            raise FileExistsError(f"Output directory is not empty: {output} (use --replace)")
        if not (output / MANIFEST).is_file():
            raise FileExistsError(f"Refusing to replace {output}: it has no {MANIFEST}")
        shutil.rmtree(output)
    output.mkdir(parents=True, exist_ok=True)


def _pack_name(relative: str) -> str:
    """``scroll/logo_tex.bin`` -> ``scroll/logo_tex``."""
    return relative.rsplit(".", 1)[0]


def convert_textures(media: Path, output: Path, report: Callable[[str], None]) -> list[dict]:
    records = []
    for directory in TEXTURE_DIRECTORIES:
        for source in sorted((media / directory).glob("*.bin")):
            relative = f"{directory}/{source.name}"
            data = source.read_bytes()
            textures = []
            for texture in texture_pack.parse(data):
                fmt = texture.format
                target = output / "textures" / _pack_name(relative) / f"{texture.index}.png"
                png.write_rgba(target, fmt.width, fmt.height, texture.rgba(data))
                textures.append(
                    {
                        "index": texture.index,
                        "format": fmt.name,
                        "width": fmt.width,
                        "height": fmt.height,
                        "levels": fmt.levels,
                    }
                )
            records.append({"pack": relative, "textures": textures})
            report(f"{len(textures):>3} textures  {relative}")
    return records


def convert_models(
    media: Path, output: Path, tables: game_tables.GameTables, report: Callable[[str], None]
) -> list[dict]:
    """Standalone models (``Media/model``) to glTF. Binlink models (robots,
    city stages) wait for their texture banks to be converted."""
    records = []
    packs = {resource.id: resource for resource in tables.resources}
    for resource in tables.resources:
        if resource.type != game_tables.RESOURCE_MODEL or resource.path is None:
            continue
        source = media / resource.path
        if not source.is_file():
            continue
        target = output / "models" / f"{_pack_name(resource.path)}.gltf"
        uris = None
        pack = packs.get(resource.texture_resource)
        if pack is not None and pack.path is not None:
            textures = output / "textures" / _pack_name(pack.path)
            count = len(list(textures.glob("*.png")))
            relative = Path("../" * (len(Path(resource.path).parts))) / "textures" / _pack_name(pack.path)
            uris = [(relative / f"{index}.png").as_posix() for index in range(count)]
        summary = gltf.write(model.parse(source.read_bytes()), target, uris)
        records.append({"model": resource.path, "resource": resource.id, **summary})
        report(f"{summary['meshes']:>3} meshes    {resource.path}")
    return records


def convert_sound_banks(media: Path, output: Path, report: Callable[[str], None]) -> list[dict]:
    records = []
    voices = {}
    for source in sorted((media / "sound").glob("*.bin")):
        data = source.read_bytes()
        if not data.startswith(audio.BANK_MAGIC):
            continue
        bank = source.stem
        effects = []
        for entry in audio.parse_bank(data):
            voices[f"sound/sfx/{bank}/{entry.index:02d}.wav"] = {"voices": entry.voices}
            audio.write_pcm16_wav(output / "sound" / "sfx" / bank / f"{entry.index:02d}.wav", entry.wave)
            wave = entry.wave
            effects.append(
                {
                    "index": entry.index,
                    "flags": entry.flags,
                    "slot": entry.slot,
                    "parameters": entry.parameters,
                    "source_format": wave.format_tag,
                    "channels": wave.channels,
                    "sample_rate": wave.sample_rate,
                }
            )
        records.append({"bank": f"sound/{source.name}", "effects": effects})
        report(f"{len(effects):>3} effects   sound/{source.name}")
    _write_json(output / "tables" / "sound_effects.json", {"effects": voices})
    return records


def _slot_sources(tables: game_tables.GameTables) -> dict[str, list[dict]]:
    """Texture slot -> every pack texture that can fill it (usually one): the
    disc's packs first, then the binlink packs (``binlink/<resource>``)."""
    sources: dict[str, list[dict]] = {}
    for resource in tables.resources:
        if resource.path is None or resource.first_slot is None:
            continue
        for index in range(resource.slot_count):
            entry = {"pack": _pack_name(resource.path), "texture": index}
            sources.setdefault(str(resource.first_slot + index), []).append(entry)
    for resource in game_tables.sprite_binlink_packs(tables):
        for index in range(resource.slot_count):
            entry = {"pack": f"binlink/{resource.id}", "texture": index}
            sources.setdefault(str(resource.first_slot + index), []).append(entry)
    return sources


def _sprite_source(tables: game_tables.GameTables, sprite_id: int) -> dict | None:
    """A sprite as the port's data files give one: its image and texel rect."""
    sources = _slot_sources(tables)
    sprite = next((entry for entry in tables.sprites if entry.id == sprite_id), None)
    if sprite is None or str(sprite.slot) not in sources:
        return None
    source = sources[str(sprite.slot)][0]
    return {
        "image": f"textures/{source['pack']}/{source['texture']}.png",
        "rect": [sprite.left, sprite.top, sprite.right, sprite.bottom],
    }


def _credit_record(tables: game_tables.GameTables, credit: game_tables.MusicCredit,
                   image: game_tables._Image) -> dict:
    """A credit with its music file, jacket/artist sprites and original biography."""
    record = {"file": f"sound/{tables.music[credit.track]}", "track": credit.track}
    record.update(title=credit.title, artist=credit.artist)
    for key, sprite_id in (
        ("jacket", credit.jacket),
        ("jacket_small", credit.jacket_small),
        ("artist_name", credit.artist_name),
        ("artist_link", credit.artist_link),
    ):
        if (source := _sprite_source(tables, sprite_id)) is not None:
            record[key] = source
    if credit.artist_profile:
        # Like conversation text, these scripts pair ruby with the main text.
        # The biographies show one static text block and then hold until closed.
        script = events.commands(image, tables, credit.artist_profile)
        text = [command for command in script if command["op"] == "text"]
        if len(text) != 1 or text[0]["frames_per_character"] != 0:
            raise ValueError(f"Unsupported artist profile for {credit.artist}")
        record["artist_profile"] = {
            key: text[0][key] for key in ("text", "position", "colour", "layer")
        }
    return record


def write_tables(executable: Path, output: Path) -> dict:
    data = executable.read_bytes()
    tables = game_tables.load(data)
    image = game_tables._Image(data)
    directory = output / "tables"
    _write_json(directory / "resources.json", [asdict(resource) for resource in tables.resources])
    _write_json(
        directory / "sprites.json",
        {
            "slots": _slot_sources(tables),
            "sprites": [asdict(sprite) for sprite in tables.sprites],
        },
    )
    _write_json(
        directory / "music.json",
        {
            "tracks": tables.music,
            "scene_music": tables.scene_music,
            "credits": [
                _credit_record(tables, credit, image)
                for credit in tables.music_credits
                if credit.track < len(tables.music)
            ],
            "volume_millibels": tables.volume_millibels,
        },
    )
    _write_json(
        directory / "font.json",
        {
            "image": "textures/scroll/ascii_font_sys_tex/0.png",
            "cell": 18,
            "columns": 28,
            "characters": tables.font_characters,
            "metrics": [list(metric) for metric in tables.font_metrics],
        },
    )
    _write_json(
        directory / "movies.json",
        {
            "movies": tables.movies,
            "binlink_packs": tables.binlink_packs,
        },
    )
    story = events.events(data, tables)
    _write_json(directory / "events.json", {"events": story}, readable_text=True)
    _write_json(directory / "talk.json", events.talk_tables(data, tables))
    _write_json(directory / "town.json", town_tables.town_tables(data, tables), readable_text=True)
    _write_json(directory / "garage.json", garage_tables.garage_tables(data), readable_text=True)
    return {
        "events": len(story),
        "resources": len(tables.resources),
        "sprites": len(tables.sprites),
        "music": len(tables.music),
        "movies": len(tables.movies),
        "font_glyphs": len(tables.font_characters),
    }


def write_game_text(data: bytes, output: Path, sources=GAME_TEXT_SOURCES) -> int:
    """``tables/text.json``: the strings of the executable ``data`` that the
    data files under ``sources`` reference as ``{xbe:0x...}`` (Shift-JIS at
    that address, NFKC and trimmed), so the repository carries none of them."""
    image = game_tables._Image(data)
    addresses: set[int] = set()
    for source in sources:
        if source.is_dir():
            for path in sorted(source.rglob("*.json")):
                text = path.read_text(encoding="utf-8")
                addresses.update(int(found, 16) for found in GAME_TEXT_REFERENCE.findall(text))
    strings = {
        f"0x{address:06x}": game_tables._plain(image.raw_string(address))
        for address in sorted(addresses)
    }
    notes = ("The game's text that the data files reference as {xbe:0x...}: the Shift-JIS "
             "string at that address in default.xbe, as NFKC and trimmed.")
    _write_json(output / GAME_TEXT, {"notes": notes, "strings": strings}, readable_text=True)
    return len(strings)


def convert(
    files: Path,
    output: Path,
    replace: bool = False,
    progress: Callable[[str], None] | None = None,
) -> dict:
    """Convert ``files`` (an extraction's ``files`` directory) into ``output``."""
    report = progress or (lambda _message: None)
    media = files / "Media"
    executable = files / "default.xbe"
    if not media.is_dir() or not executable.is_file():
        raise FileNotFoundError(f"{files} does not look like an extracted disc (Media, default.xbe)")
    _prepare_output(output, replace)
    manifest = {"format_version": FORMAT_VERSION, "source": str(files), "complete": False}
    # Written first so an interrupted run can still be replaced.
    _write_json(output / MANIFEST, manifest)
    manifest["tables"] = write_tables(executable, output)
    manifest["tables"]["text"] = write_game_text(executable.read_bytes(), output)
    manifest["texture_packs"] = convert_textures(media, output, report)
    tables = game_tables.load(executable.read_bytes())
    manifest["models"] = convert_models(media, output, tables, report)
    manifest["robots"] = robot_convert.convert_robots(
        media, executable.read_bytes(), tables, output, report
    )
    manifest["sound_banks"] = convert_sound_banks(media, output, report)
    manifest["complete"] = True
    _write_json(output / MANIFEST, manifest)
    return manifest


def update_robots(
    files: Path, output: Path, progress: Callable[[str], None] | None = None
) -> dict:
    """Re-run only the robot conversion into an existing conversion."""
    report = progress or (lambda _message: None)
    manifest_path = output / MANIFEST
    if not manifest_path.is_file():
        raise FileNotFoundError(f"{output} has no {MANIFEST}: run a full conversion first")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    executable = (files / "default.xbe").read_bytes()
    tables = game_tables.load(executable)
    manifest["robots"] = robot_convert.convert_robots(
        files / "Media", executable, tables, output, report
    )
    _write_json(manifest_path, manifest)
    return manifest["robots"]


def update_town(files: Path, output: Path) -> dict:
    """Re-write only the town's tables (tables/town.json) into an existing conversion."""
    if not (output / MANIFEST).is_file():
        raise FileNotFoundError(f"{output} has no {MANIFEST}: run a full conversion first")
    data = (files / "default.xbe").read_bytes()
    town = town_tables.town_tables(data, game_tables.load(data))
    _write_json(output / "tables" / "town.json", town, readable_text=True)
    return town
