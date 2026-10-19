"""Puts a MechAssault mech's gun arm on a Phantom Crash arm model (a mod).

A Phantom Crash arm model (``models/binlink/<id>.gltf``) has two drawing
nodes: the upper arm (bone 0) and the forearm with its weapon (bone 1),
whose muzzle sits in ``robots/weapon_mounts.json``. The swap keeps the
model's nodes and upper arm and replaces the forearm's geometry with every
mesh of the MechAssault arm tree, turned to point along the forearm (+Z in
glTF), scaled to the forearm's length and moved so the front barrel's tip
lands on the original muzzle. The muzzle data then needs no change.

MechAssault's space is left-handed with +Z forward, as the game's is, but
Phantom Crash's guns point along -Z (+Z once mirrored into glTF), so a
point (x, y, z) becomes (-x, y, z) in the glTF; the mirror flips winding.
"""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

import numpy as np

from .. import png
from ..gltf_edit import Builder, read_accessor
from .effects import Loader
from .mgf import BACKSLASH, Collection

FOREARM_NODE = 4  # the drawing node on bone 1 in the arm models
MOD_TEXTURES = "textures/mechassault/arms"


def _rotation(axis: list[float], angle: float) -> np.ndarray:
    a = np.array(axis, float)
    size = np.linalg.norm(a)
    if size < 1e-9 or abs(angle) < 1e-9:
        return np.eye(3)
    a /= size
    k = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    return np.eye(3) + math.sin(angle) * k + (1 - math.cos(angle)) * k @ k


def _arm_parts(loader: Loader, directory: str, name: str) -> list[tuple[str, Any, np.ndarray, np.ndarray]]:
    """(node name, mesh asset, positions, normals) in the arm tree's space."""
    nodes = loader.tree(directory, name)
    if nodes is None:
        raise ValueError(f"{directory}{BACKSLASH}{name}.NODE is not on the disc")
    worlds: list[np.ndarray] = []
    parts = []
    for node in nodes:
        local = np.eye(4)
        local[:3, :3] = _rotation(node["axis"], node["angle"]) @ np.diag(node["scale"])
        local[:3, 3] = node["position"]
        world = local if node["parent"] < 0 else worlds[node["parent"]] @ local
        worlds.append(world)
        key = node.get("mesh")
        if not key:
            continue
        mesh = loader.resources.meshes[key]
        vertices = np.array(mesh.geometry.vertices, float)
        positions = (world[:3, :3] @ vertices[:, :3].T).T + world[:3, 3]
        normals = (world[:3, :3] @ vertices[:, 3:6].T).T
        normals /= np.linalg.norm(normals, axis=1, keepdims=True) + 1e-9
        parts.append((node["name"], mesh, positions, normals))
    return parts


def swap_arm(image: Path, archive: str, directory: str, name: str, template: Path, mods: Path,
             model: str) -> dict[str, Any]:
    """Writes ``mods/<model>``: `template` with the forearm replaced by the arm tree."""
    files = Collection(image, [archive])
    loader = Loader(files, {})
    parts = _arm_parts(loader, directory, name)
    mounts = json.loads((template.parents[2] / "robots" / "weapon_mounts.json").read_text(encoding="utf-8"))
    muzzle = mounts["weapons"][model]["muzzles"][0]["position"]
    tip_target = np.array([muzzle[0], muzzle[1], -muzzle[2]])  # into glTF space

    document = json.loads(template.read_text(encoding="utf-8"))
    blob = (template.parent / document["buffers"][0]["uri"]).read_bytes()
    forearm = document["nodes"][FOREARM_NODE]["mesh"]
    old = document["meshes"][forearm]["primitives"][0]
    forearm_z = read_accessor(document, blob, old["attributes"]["POSITION"])[
        np.unique(read_accessor(document, blob, old["indices"]).ravel())
    ][:, 2]

    mirror = np.array([-1.0, 1.0, 1.0])
    every = np.concatenate([p[2] for p in parts]) * mirror
    scale = float(forearm_z.max() - forearm_z.min()) / float(np.ptp(every[:, 2]))
    front = next((p for p in parts if "fgun" in p[0].lower()), max(parts, key=lambda p: p[2][:, 2].max()))
    front_points = front[2] * mirror
    near_tip = front_points[front_points[:, 2] > front_points[:, 2].max() - 0.05 * np.ptp(every[:, 2])]
    tip = np.array([near_tip[:, 0].mean(), near_tip[:, 1].mean(), front_points[:, 2].max()])
    offset = tip_target - tip * scale

    builder = Builder()
    # The upper arm (and anything else) is copied as it was.
    meshes = builder.copy_meshes(document, blob, forearm)

    template_material = document["materials"][0]
    materials = [template_material]
    images = list(document.get("images", []))
    textures = list(document.get("textures", []))
    samplers = list(document.get("samplers", []))
    repeat = len(samplers)
    samplers.append({"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497})
    texture_slots: dict[str, int] = {}
    out_textures = mods / MOD_TEXTURES
    out_textures.mkdir(parents=True, exist_ok=True)
    stem = Path(model).stem
    for part_name, mesh, positions, normals in parts:
        key = mesh.texture_key or ""
        if key not in texture_slots:
            material = json.loads(json.dumps(template_material))
            material["name"] = f"mechassault_{len(materials)}"
            material["pbrMetallicRoughness"].pop("baseColorTexture", None)
            if key:
                picture = loader.resources.textures[key]
                file_name = f"{stem}_{len(images)}.png"
                png.write_rgba(out_textures / file_name, picture.width, picture.height, picture.rgba)
                images.append({"uri": f"../../{MOD_TEXTURES}/{file_name}"})
                textures.append({"source": len(images) - 1, "sampler": repeat})
                material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": len(textures) - 1}
            materials.append(material)
            texture_slots[key] = len(materials) - 1
        vertices = np.array(mesh.geometry.vertices, float)
        triangles = np.array(mesh.geometry.indices, np.uint32).reshape(-1, 3)[:, [0, 2, 1]]
        meshes[forearm]["primitives"].append(builder.primitive(
            positions * mirror * scale + offset,
            normals * mirror,
            vertices[:, 6:8],
            np.full((len(vertices), 4), 255),
            triangles,
            texture_slots[key],
        ))

    document.update({
        "meshes": meshes,
        "materials": materials,
        "images": images,
        "textures": textures,
        "samplers": samplers,
        "accessors": builder.accessors,
        "bufferViews": builder.views,
        "buffers": [builder.buffer()],
    })
    document["asset"]["generator"] = "phantomcrash mechassault arm swap"
    target = mods / model
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")
    return {"model": model, "parts": [p[0] for p in parts], "scale": round(scale, 4),
            "materials": len(materials) - 1}
