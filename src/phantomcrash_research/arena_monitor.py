"""Advertising ship and arena TV data (researcher-assigned names).

0x066940 selects its stage route at 0x21c768 and last segment at 0x21c788.
0x06a38d follows the cubic segments at .003 per 30 Hz tick. 0x06a5ac
submits the day/night ship models 210/211. Its screens are slots 1218,
1222 / 1233, 1237; 0x06aa07 replaces their UVs for the battle framebuffer.
The port improvement keeps that battle feed and cycles its subject.
"""

from __future__ import annotations

import struct
from dataclasses import replace
from pathlib import Path

from . import game_tables, gltf, model, png, texture_pack


def ship_table(executable: bytes) -> dict:
    image = game_tables._Image(executable)
    routes = {}
    for stage in range(8):
        address = image.u32(0x21C768 + stage * 4)
        count = image.u32(0x21C788 + stage * 4) + 1
        routes[str(stage)] = [
            [list(struct.unpack_from("<3f", executable, image.offset(address + segment * 48 + p * 12, 12)))
             for p in range(4)] for segment in range(count)
        ]
    number = lambda address: struct.unpack_from("<f", executable, image.offset(address, 4))[0]
    models = []
    for night in (False, True):
        shift = 4 if night else 0
        models.append({
            "model": f"models/binlink/{211 if night else 210}.gltf",
            "screens": [4 + shift, 8 + shift],
            "hinges": [
                {"meshes": [2 + shift, 3 + shift, 4 + shift, 5 + shift], "x": 40.2, "sign": -1},
                {"meshes": [6 + shift, 7 + shift, 8 + shift, 9 + shift], "x": -40.2, "sign": 1},
            ],
            "spinners": ([{"mesh": i, "pivot": p} for i, p in enumerate([
                [3.06, -9.16, 13.33], [-2.67, -9.16, -3.96],
                [3.24, -9.16, -20.97], [-4.54, -9.16, -38.55]])] if night else []),
        })
    return {"routes": routes, "models": models,
            "segment_per_second": number(0x1B59FC) * 30,
            "hinge_per_second": number(0x1B59F8) * 30,
            "spin_per_second": number(0x1B59E4) * 30,
            "hinge_max": number(0x19F8DC),
            "open_segments": [12, 22], "close_segments": [15, 27]}


def convert_ship(executable: bytes, read, output: Path, write_json) -> dict:
    table = ship_table(executable)
    for resource, texture_id in ((210, 1586), (211, 1587)):
        data, textures = read(resource), read(texture_id)
        if data is None or textures is None:
            raise ValueError(f"Missing arena ship resource {resource}")
        uris = []
        for texture in texture_pack.parse(textures):
            name = f"{texture_id}_{texture.index}.png"
            fmt = texture.format
            png.write_rgba(output / "textures/binlink" / name, fmt.width, fmt.height,
                           texture.rgba(textures))
            uris.append(f"../../textures/binlink/{name}")
        parsed = model.parse(data)
        # The shipped atlas coordinates are overwritten by 0x06aa07 when
        # showing the framebuffer. D3D11 samples normalized coordinates.
        meshes = list(parsed.meshes)
        for index in table["models"][resource - 210]["screens"]:
            mesh = meshes[index]
            if len(mesh.vertices) != 4:
                raise ValueError("Ship monitor must be a four-vertex panel")
            uv = ((0.0, 0.0), (0.0, 1.0), (1.0, 0.0), (1.0, 1.0))
            meshes[index] = replace(mesh, vertices=tuple(replace(v, uv=at)
                                                        for v, at in zip(mesh.vertices, uv)))
        gltf.write(replace(parsed, meshes=tuple(meshes)),
                   output / "models/binlink" / f"{resource}.gltf", uris)
    write_json(output / "tables/arena_monitor.json", table)
    return {"models": 2, "routes": len(table["routes"])}
