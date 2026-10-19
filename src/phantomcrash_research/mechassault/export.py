"""Writes MechAssault's weapon effects as a Phantom Crash mod.

Output under the mod folder (the port overlays it on its assets):

- ``textures/mechassault/<slug>.png``  every texture the effects use
- ``models/mechassault/<slug>.gltf``   every effect mesh, one unlit material
- ``data/world/mechassault_vfx.json``  the node trees, modifiers and the
  weapon-to-effect map the port's ``mech_vfx`` reads

The game's space is left-handed and glTF's is right-handed, so Z is negated
on positions and normals and the winding reversed (as ``gltf.py`` does).
"""

from __future__ import annotations

import base64
import json
from pathlib import Path
import re
import struct
from typing import Any

from .. import png
from .effects import EFFECT_ROOT, Loader, MeshAsset
from .mgf import BACKSLASH, Collection, xbe_names

MOD_TEXTURES = "textures/mechassault"
MOD_MODELS = "models/mechassault"
MOD_DATA = "data/world/mechassault_vfx.json"
ADDITIVE_FLAG = 0x200  # extras.phantomcrash.flags, read by world_model.cpp
ARCHIVE_ORDER = ["Common.mgf", "Mission01.mgf", "Mission02.mgf", "Mission03.mgf"]

# Phantom Crash's weapon kinds (hud_kind) to the effects shown for them. Each
# entry names the muzzle, the shot and the impact effect under the disc's
# \SpecialEffects\Weapons; edit the mod's JSON to remap, or add by_name.
DEFAULT_WEAPONS: dict[str, dict[str, str]] = {
    "machine_gun": {
        "fire": "Ballistic/MachineGun_Fire_L1",
        "shot": "Ballistic/MachineGun_L1",
        "hit": "Ballistic/MachineGun_Hit_L1",
    },
    "cannon": {
        "fire": "Ballistic/Autocannon_Fire_L1",
        "shot": "Ballistic/Autocannon_L1",
        "hit": "Ballistic/Autocannon_Hit_L1",
    },
    "missile": {
        "fire": "Missile/Javelin_Fire_L1",
        "shot": "Missile/Javelin_L1",
        "hit": "Missile/Javelin_Hit_L1",
    },
    "beam": {
        "fire": "Energy/Laser_Fire_L1",
        "shot": "Energy/Laser_L1",
        "hit": "Energy/Laser_Hit_L1",
    },
    "grenade": {
        "fire": "Missile/Hammer_Fire_L1",
        "shot": "Missile/Hammer_L1",
        "hit": "Missile/Hammer_Hit_L1",
    },
}


def discover_weapon_effects(files: Collection) -> list[str]:
    """``Ballistic/MachineGun_L1``-style names of every weapon effect with a .NODE."""
    stream = files.archive("Common.mgf").stream
    prefix = (EFFECT_ROOT + "Weapons" + BACKSLASH).encode()
    found = set()
    for match in re.finditer(re.escape(prefix) + rb"([A-Za-z0-9_ ]+)\\([A-Za-z0-9_ ]+)\\\2\.NODE", stream, re.I):
        found.add(f"{match.group(1).decode()}/{match.group(2).decode()}")
    return sorted(found)


def slug(key: str) -> str:
    text = key.replace(EFFECT_ROOT.lower(), "").replace(BACKSLASH, "_")
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")


def model_name(key: str) -> str:
    return slug(key).removeprefix("weapons_").removesuffix("_mesh").replace("_meshes_", "_")


def texture_name(key: str, additive: bool) -> str:
    name = slug(key).removeprefix("weapons_").replace("_materials_", "_").removesuffix("_tif")
    return name + ("_add" if additive else "")


def opaque(rgba: bytes) -> bytes:
    """Additive blending adds colour by SRC_ALPHA: its texels carry no alpha."""
    out = bytearray(rgba)
    out[3::4] = bytes([255]) * (len(out) // 4)
    return bytes(out)


def write_gltf(mesh: MeshAsset, texture_uri: str | None, target: Path) -> None:
    vertices = mesh.geometry.vertices
    positions = b"".join(struct.pack("<3f", v[0], v[1], -v[2]) for v in vertices)
    normals = b"".join(struct.pack("<3f", v[3], v[4], -v[5]) for v in vertices)
    uvs = b"".join(struct.pack("<2f", v[6], v[7]) for v in vertices)
    triangles = mesh.geometry.indices
    flipped = []
    for first in range(0, len(triangles) - 2, 3):
        flipped += [triangles[first], triangles[first + 2], triangles[first + 1]]
    index_bytes = struct.pack(f"<{len(flipped)}H", *flipped)
    index_bytes += b"\0" * (-len(index_bytes) % 4)
    blob = positions + normals + uvs + index_bytes
    xs, ys, zs = ([v[i] for v in vertices] for i in (0, 1, 2))
    material: dict[str, Any] = {
        "name": model_name(mesh.key),
        "pbrMetallicRoughness": {
            "baseColorFactor": [*(c / 255 for c in mesh.colour), min(mesh.opacity, 1.0)],
            "metallicFactor": 0.0,
            "roughnessFactor": 1.0,
        },
        "alphaMode": "BLEND",
        "doubleSided": mesh.two_sided,
        "extensions": {"KHR_materials_unlit": {}},
        "extras": {"phantomcrash": {"flags": ADDITIVE_FLAG if mesh.additive else 0}},
    }
    document: dict[str, Any] = {
        "asset": {"version": "2.0", "generator": "phantomcrash mechassault converter"},
        "extensionsUsed": ["KHR_materials_unlit"],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": model_name(mesh.key), "mesh": 0}],
        "meshes": [
            {
                "primitives": [
                    {
                        "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                        "indices": 3,
                        "material": 0,
                    }
                ]
            }
        ],
        "materials": [material],
        "accessors": [
            {
                "bufferView": 0,
                "componentType": 5126,
                "count": len(vertices),
                "type": "VEC3",
                "min": [min(xs), min(ys), -max(zs)],
                "max": [max(xs), max(ys), -min(zs)],
            },
            {"bufferView": 1, "componentType": 5126, "count": len(vertices), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(vertices), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": len(flipped), "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(positions), "target": 34962},
            {"buffer": 0, "byteOffset": len(positions), "byteLength": len(normals), "target": 34962},
            {
                "buffer": 0,
                "byteOffset": len(positions) + len(normals),
                "byteLength": len(uvs),
                "target": 34962,
            },
            {
                "buffer": 0,
                "byteOffset": len(positions) + len(normals) + len(uvs),
                "byteLength": len(index_bytes),
                "target": 34963,
            },
        ],
        "buffers": [
            {
                "byteLength": len(blob),
                "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode(),
            }
        ],
    }
    if texture_uri is not None:
        wrap = {"repeat": 10497, "clamp": 33071}
        document["images"] = [{"uri": texture_uri}]
        document["samplers"] = [
            {
                "magFilter": 9729,
                "minFilter": 9987,
                "wrapS": wrap[mesh.wrap[0]],
                "wrapT": wrap[mesh.wrap[1]],
            }
        ]
        document["textures"] = [{"source": 0, "sampler": 0}]
        material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0}
    target.write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")


def convert(image: Path, output: Path, report=None) -> dict[str, Any]:
    files = Collection(image, ARCHIVE_ORDER)
    loader = Loader(files, xbe_names(image))
    effects: dict[str, Any] = {}
    skipped: list[str] = []
    for name in discover_weapon_effects(files):
        directory, effect_name = name.split("/")
        built = loader.effect("Weapons" + BACKSLASH + directory, effect_name)
        if built is None:
            skipped.append(name)
            continue
        effects[name] = built
    textures_dir = output / MOD_TEXTURES
    models_dir = output / MOD_MODELS
    textures_dir.mkdir(parents=True, exist_ok=True)
    models_dir.mkdir(parents=True, exist_ok=True)
    models: dict[str, str] = {}
    written: set[str] = set()
    for key, mesh in loader.resources.meshes.items():
        uri = None
        if mesh.texture_key is not None:
            name = texture_name(mesh.texture_key, mesh.additive)
            if name not in written:
                written.add(name)
                picture = loader.resources.textures[mesh.texture_key]
                rgba = opaque(picture.rgba) if mesh.additive else picture.rgba
                png.write_rgba(textures_dir / f"{name}.png", picture.width, picture.height, rgba)
            uri = f"../../{MOD_TEXTURES}/{name}.png"
        write_gltf(mesh, uri, models_dir / f"{model_name(key)}.gltf")
        models[key] = f"{MOD_MODELS}/{model_name(key)}.gltf"
    for built in effects.values():
        for node in built["nodes"]:
            if "mesh" in node:
                node["model"] = models.get(node.pop("mesh") or "")
    document = {
        "enabled": True,
        "source": "Converted from the user's MechAssault disc; not for redistribution.",
        "unit_scale": 0.35,
        "weapons": DEFAULT_WEAPONS,
        "by_name": {},
        "effects": effects,
    }
    data_file = output / MOD_DATA
    data_file.parent.mkdir(parents=True, exist_ok=True)
    data_file.write_text(json.dumps(document, indent=1), encoding="utf-8")
    summary = {
        "effects": len(effects),
        "skipped": skipped,
        "meshes": len(models),
        "textures": len(written),
        "missing": sorted(set(loader.missing)),
    }
    if report:
        report(summary)
    return summary
