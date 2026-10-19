"""Named S.L.A.I. shop songs, converted to Ogg and sold through Sonic Amp.

The 90 song/artist entries are the shipped UK shop messages. bgm_90 onward
are not registered as songs. ILD layout is independently cross-checked with
vgmstream src/meta/ild.c. Full overlays rebuild from the base extraction.
"""

from __future__ import annotations

import json
from pathlib import Path
import struct
import subprocess
import tempfile

import numpy as np
from typing import Callable

from .. import png
from .archive import SlaiDisc
from .pig import read_pig


def message_glyphs(data: bytes) -> list[list[int]]:
    """Retain font indices until a verified Unicode mapping exists."""
    count = struct.unpack_from("<I", data)[0]
    if not 0 < count < 1000 or 4 + count * 4 > len(data):
        raise ValueError("Invalid S.L.A.I. shop MSG table")
    offsets = struct.unpack_from(f"<{count}I", data, 4)
    result = []
    for at, end in zip(offsets, (*offsets[1:], len(data))):
        if at < 4 + count * 4 or end < at or end > len(data) or (end - at) % 2:
            raise ValueError("Invalid S.L.A.I. shop MSG string offset")
        codes = struct.unpack_from(f"<{(end - at) // 2}H", data, at)
        if 0xFFFF not in codes:
            raise ValueError("Unterminated S.L.A.I. shop MSG string")
        result.append(list(codes[:codes.index(0xFFFF)]))
    return result


def messages(data: bytes) -> list[str]:
    """ASCII subset of shipped glyphs; non-Latin indices remain in inventory."""
    return ["".join(chr(code + 32) for code in codes if code <= 94).strip()
            for codes in message_glyphs(data)]


def stream_header(data: bytes, size: int) -> dict:
    if len(data) < 84 or data[:4] != b"ILD\0":
        raise ValueError("Missing ILD header")
    channels, start, length = struct.unpack_from("<3I", data, 4)
    interleave = struct.unpack_from("<I", data, 0x20)[0]
    rate, loop_start, loop_end = struct.unpack_from("<3I", data, 0x28)
    if channels != 2 or interleave != 16 or start < 84 or start + length > size or length % 32:
        raise ValueError("Unsupported ILD layout")
    return {"channels": channels, "sample_rate": rate, "frames": length // 32 * 28,
            "loop_start": loop_start // 16 * 28, "loop_end": loop_end // 16 * 28}


def _json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def convert_music(image: Path, assets: Path, data: Path, mods: Path, converter: Path,
                  report: Callable[[str], None] = print) -> dict:
    if not converter.is_file():
        raise ValueError("Build the port's phantomcrash_slai_audio converter first")
    tables = json.loads((assets / "tables/music.json").read_text(encoding="utf-8"))
    shop = json.loads((data / "ui/sonic_amp.json").read_text(encoding="utf-8"))
    base_count = len(tables["credits"])
    inventory = []
    with SlaiDisc(image) as disc, tempfile.TemporaryDirectory(prefix="phantomcrash-slai-") as work:
        name_data = disc.read(disc.find("UK_SONGNAME.MSG")[0])
        artist_data = disc.read(disc.find("UK_ARTISTNAME.MSG")[0])
        names, artists = messages(name_data), messages(artist_data)
        name_glyphs, artist_glyphs = message_glyphs(name_data), message_glyphs(artist_data)
        listed = disc.read(disc.find("SNDSTM.LST")[0]).decode("ascii").splitlines()
        if len(names) != len(artists) or listed[:len(names)] != [f"adpcm\\bgm_{i:02d}.ild" for i in range(len(names))]:
            raise ValueError("Song messages and stream listing do not agree")
        start, size = disc.disc_files()["SNDSTM.BIN"]
        header = disc._read(start, 2048)
        if header[:4] != b"Wpbb" or struct.unpack_from("<I", header, 4)[0] != len(listed):
            raise ValueError("Stream container does not match its listing")
        tracks = tables["tracks"]
        track_index = len(tracks)
        for i, (title, artist) in enumerate(zip(names, artists)):
            offset, length = struct.unpack_from("<II", header, 8 + i * 8)
            if offset < 2048 or offset + length > size:
                raise ValueError(f"Invalid stream extent {i}")
            ild = disc._read(start + offset, length)
            info = stream_header(ild[:128], len(ild))
            relative = f"sound/slai/bgm_{i:02d}.ogg"
            output = mods / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            source = Path(work) / "input.ild"
            source.write_bytes(ild)
            # Rebuild into a sibling temp file, so interrupted encoding never
            # replaces an existing playable song with a truncated Ogg.
            temporary = output.with_suffix(".converting.ogg")
            try:
                result = subprocess.run([str(converter.resolve()), str(source), str(temporary)],
                                        capture_output=True, text=True, timeout=180)
                if result.returncode:
                    raise ValueError(f"Song {i}: {result.stderr[-600:]}")
                temporary.replace(output)
            finally:
                temporary.unlink(missing_ok=True)
            jacket = disc.find(f"CD_B_{i:03d}.PIG")
            credit = {"file": relative, "track": track_index + i, "title": title,
                      "artist": artist, "source": {"game": "S.L.A.I.", "stream": i,
                        "title_glyphs": name_glyphs[i], "artist_glyphs": artist_glyphs[i]}}
            if jacket:
                picture = read_pig(disc.read(jacket[0]))
                texture = f"textures/slai/music/cd_{i:03d}.png"
                png.write_rgba(mods / texture, picture.width, picture.height, picture.tobytes())
                credit["jacket"] = {"image": texture, "rect": [0, 0, picture.width, picture.height]}
                pixels = np.frombuffer(picture.tobytes(), dtype=np.uint8).reshape(picture.height, picture.width, 4)
                if picture.width != 128 or picture.height != 128:
                    raise ValueError("Expected a 128x128 S.L.A.I. CD jacket")
                small = pixels.astype(np.uint16).reshape(64, 2, 64, 2, 4).mean(axis=(1, 3)).round().astype(np.uint8)
                small_texture = f"textures/slai/music/cd_{i:03d}_small.png"
                png.write_rgba(mods / small_texture, 64, 64, small.tobytes())
                credit["jacket_small"] = {"image": small_texture, "rect": [0, 0, 64, 64]}
            tables["credits"].append(credit)
            tracks.append(relative)
            inventory.append({"stream": i, "title": title, "artist": artist, "file": relative,
                              "title_glyphs": name_glyphs[i], "artist_glyphs": artist_glyphs[i], **info})
            report(f"S.L.A.I. music {i + 1}/{len(names)}: {title}")
    catalogue = shop["grid"]["catalogue"]
    ids = list(range(catalogue["tracks"])) + list(range(base_count, len(tables["credits"])))
    catalogue["credit_ids"] = ids
    catalogue["tracks"] = len(ids)
    per_page = catalogue["columns"] * catalogue["rows"]
    catalogue["category_pages"][-1] = (len(ids) + per_page - 1) // per_page - sum(catalogue["category_pages"][:-1])
    _json(mods / "tables/music.json", tables)
    _json(mods / "data/ui/sonic_amp.json", {"grid": {"catalogue": catalogue}})
    _json(mods / "slai/music_inventory.json", {"songs": inventory, "excluded_streams": list(range(len(inventory), len(listed)))})
    return {"songs": len(inventory), "bytes": sum((mods / s["file"]).stat().st_size for s in inventory)}
