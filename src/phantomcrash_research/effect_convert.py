"""Convert the battle effects the port draws as models.

Output, under the converted assets:

- ``textures/systemdata/muteki01.png``: the entry shield's texture
  (``systemdata\\muteki01.tga``, loaded at ``0x0bef8a``), a white image whose
  alpha holds waves.
- ``models/binlink/197_30.gltf``: the entry shield (``0x06f592`` draws object
  slot 0x41b, mesh 30 of resource 197: a sphere one unit across). The game
  draws it with its rainbow program. The native shader supplies animated
  height-dependent colour, lighting and scrolling planar UVs; see
  docs/ENTRY_SHIELD_RENDERING.md. The export retains white vertices and
  fallback planar UVs for external viewers, with an additive two-sided
  material.
"""

from __future__ import annotations

import dataclasses
from pathlib import Path
import struct
from typing import Callable

from . import gltf, model, png

SHIELD_RESOURCE = 197
SHIELD_MESH = 30  # object slot 0x41b, the model's first slot 0x3fd plus 30
SHIELD_TEXTURE = "systemdata/muteki01.tga"
SHIELD_REPEAT = 2.0  # the texture's copies across the sphere
WHITE = 0xFFFFFFFF
TGA_HEADER = 18
TGA_TRUE_COLOUR = 2
TGA_TOP_LEFT = 0x20  # image descriptor bit: rows run top to bottom


def read_tga(data: bytes) -> tuple[int, int, bytes]:
    """An uncompressed 32-bit TGA as width, height and RGBA rows from the top."""
    id_length, colour_map, kind = data[0], data[1], data[2]
    width, height, depth, descriptor = struct.unpack_from("<HHBB", data, 12)
    if kind != TGA_TRUE_COLOUR or colour_map or depth != 32:
        raise ValueError("only uncompressed 32-bit TGA images are read")
    start = TGA_HEADER + id_length
    row = width * 4
    rows = [data[start + y * row : start + (y + 1) * row] for y in range(height)]
    if not descriptor & TGA_TOP_LEFT:
        rows.reverse()
    rgba = bytearray()
    for line in rows:
        for x in range(0, row, 4):
            b, g, r, a = line[x : x + 4]
            rgba += bytes((r, g, b, a))
    return width, height, bytes(rgba)


def _shield_mesh(mesh: model.Mesh) -> model.Mesh:
    vertices = [
        dataclasses.replace(
            vertex,
            colour=WHITE,
            uv=((vertex.position[0] + 0.5) * SHIELD_REPEAT, (0.5 - vertex.position[1]) * SHIELD_REPEAT),
        )
        for vertex in mesh.vertices
    ]
    flags = model.BATCH_UNLIT | model.BATCH_ADDITIVE | model.BATCH_TWO_SIDED
    wrap = model.STAGE_WRAP_U | model.STAGE_WRAP_V | model.STAGE_TEXTURE_ALPHA
    batches = [
        dataclasses.replace(
            batch,
            flags=batch.flags | flags,
            stage_flags=(batch.stage_flags[0] | wrap, *batch.stage_flags[1:]),
            textures=(0, -1, -1, -1),
            diffuse=WHITE,
        )
        for batch in mesh.batches
    ]
    return dataclasses.replace(mesh, vertices=vertices, batches=batches)


def convert_effects(media: Path, read: Callable[[int], bytes | None], output: Path, report) -> list[dict]:
    """``read`` gives a binlink resource's bytes (robot_convert's packs)."""
    source = media / SHIELD_TEXTURE
    data = read(SHIELD_RESOURCE)
    if not source.is_file() or data is None:
        return []
    width, height, rgba = read_tga(source.read_bytes())
    texture = output / "textures" / SHIELD_TEXTURE.replace(".tga", ".png")
    png.write_rgba(texture, width, height, rgba)
    parsed = model.parse(data)
    shield = model.Model(parsed.magic, [_shield_mesh(parsed.meshes[SHIELD_MESH])])
    target = output / "models" / "binlink" / f"{SHIELD_RESOURCE}_{SHIELD_MESH}.gltf"
    summary = gltf.write(shield, target, ["../../textures/systemdata/muteki01.png"])
    report("   1 effect model")
    return [{"resource": SHIELD_RESOURCE, "mesh": SHIELD_MESH, **summary}]
