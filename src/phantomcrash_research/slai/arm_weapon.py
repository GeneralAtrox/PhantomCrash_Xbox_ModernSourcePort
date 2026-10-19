"""Adds an S.L.A.I. arm weapon to Phantom Crash as a new part (a mod).

S.L.A.I. and Phantom Crash build an arm the same way: the weapon hangs from
the elbow, pointing forward and slightly below it. Phantom Crash's arm
models (``models/binlink/<id>.gltf``) add the upper arm (node 2, bone 0) to
the forearm and weapon (node 4, bone 1). The new models keep a template
arm's nodes and upper arm and put every mesh of the S.L.A.I. model, taken
relative to its root (the elbow), on the forearm.

S.L.A.I.'s space is y down and -z forward with +x the stored (left) arm's
side, so (x, y, z) becomes (x, -y, -z) in glTF; the right arm mirrors x, as
S.L.A.I. does for it (``PARTS_REF_LR``). Its units are about ten times
Phantom Crash's: the Holy body is 15 units tall in S.L.A.I. against 1.9,
and the handgun's muzzle 16.4 units from the elbow against the Pistol's
1.65, so they scale by 0.1.

The new part is appended to copies of the base robot tables written under
the mod folder: an arm type (``robots/catalogue.json``) with the template's
skeleton (``robots/skeletons.json``), its muzzles
(``robots/weapon_mounts.json``) and a weapon record
(``tables/parts.json``), plus its place when shown alone
(``data/ui/robot_preview.json``).
"""

from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any

import numpy as np

from .. import png
from ..gltf_edit import Builder
from .archive import SlaiDisc
from .pig import read_pig
from .pod import read_pod, world_matrices

SCALE = 0.1
FOREARM_NODE = 4
MOD_TEXTURES = "textures/slai"
SPEC = Path(__file__).with_name("twin_missile.json")
_ROBOT_TABLES = ("robots/catalogue.json", "robots/skeletons.json", "robots/weapon_mounts.json",
                 "tables/parts.json")


def _side_axes(side: int) -> np.ndarray:
    """S.L.A.I. to glTF for arm side 0 (right, mirrored) or 1 (left)."""
    return np.array([-1.0 if side == 0 else 1.0, -1.0, -1.0])


def _meshes(nodes: list) -> list[tuple[Any, np.ndarray, np.ndarray]]:
    """(material, positions, normals) relative to the root node, S.L.A.I. space."""
    worlds = [np.array(w, float).reshape(4, 4) for w in world_matrices(nodes)]
    elbow = worlds[0][3, :3]
    out = []
    for index, node in enumerate(nodes):
        world = worlds[index]
        for material in node.materials:
            batch = material.batch
            positions = np.array(batch.positions, float) @ world[:3, :3] + world[3, :3] - elbow
            normals = np.array(batch.normals, float) @ world[:3, :3]
            normals /= np.linalg.norm(normals, axis=1, keepdims=True) + 1e-9
            out.append((material, positions, normals))
    return out


def _facing(triangles: np.ndarray, positions: np.ndarray, normals: np.ndarray) -> np.ndarray:
    """Triangles wound clockwise seen from the side their vertex normals face.

    The port's models keep the original's winding, which culls
    counter-clockwise faces, rather than glTF's counter-clockwise fronts.
    """
    a, b, c = (positions[triangles[:, k]] for k in range(3))
    face = np.cross(b - a, c - a)
    along = (normals[triangles].sum(axis=1) * face).sum(axis=1)
    out = triangles.copy()
    flip = along > 0
    out[flip] = out[flip][:, [0, 2, 1]]
    return out


def _arm_model(template: Path, parts: list, side: int, texture_uri: str) -> dict[str, Any]:
    document = json.loads(template.read_text(encoding="utf-8"))
    blob = (template.parent / document["buffers"][0]["uri"]).read_bytes()
    forearm = document["nodes"][FOREARM_NODE]["mesh"]
    builder = Builder()
    meshes = builder.copy_meshes(document, blob, forearm)

    # The template's first texture stays first: the robot's paint replaces
    # it on the upper arm. The weapon keeps its own picture.
    images = list(document.get("images", [])) + [{"uri": texture_uri}]
    samplers = list(document.get("samplers", [])) + [
        {"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}
    ]
    textures = list(document.get("textures", [])) + [
        {"source": len(images) - 1, "sampler": len(samplers) - 1}
    ]
    material = copy.deepcopy(document["materials"][0])
    material["name"] = "slai"
    material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": len(textures) - 1}
    materials = list(document["materials"]) + [material]

    axes = _side_axes(side)
    for source, positions, normals in parts:
        batch = source.batch
        if not batch.triangles:
            continue
        out_positions = positions * axes * SCALE
        out_normals = normals * axes
        triangles = _facing(np.array(batch.triangles, np.uint32), out_positions, out_normals)
        colours = np.clip(np.array(batch.colours) * np.append(source.colour, 1.0) * 255, 0, 255)
        meshes[forearm]["primitives"].append(builder.primitive(
            out_positions, out_normals, np.array(batch.uvs), colours.round(), triangles,
            len(materials) - 1,
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
    document["asset"]["generator"] = "phantomcrash slai arm weapon"
    return document


def _muzzles(template_mount: dict[str, Any], nodes: list, muzzle_nodes: list[int],
             side: int) -> dict[str, Any]:
    """The template's mount with a muzzle at each launch node (game space: z forward is -z)."""
    worlds = [np.array(w, float).reshape(4, 4) for w in world_matrices(nodes)]
    elbow = worlds[0][3, :3]
    first = template_mount["muzzles"][0]
    mount = {"resource": template_mount.get("resource"), "muzzles": []}
    for index in muzzle_nodes:
        gltf = (worlds[index][3, :3] - elbow) * _side_axes(side) * SCALE
        muzzle = copy.deepcopy(first)
        muzzle["position"] = [float(gltf[0]), float(gltf[1]), float(-gltf[2])]
        mount["muzzles"].append(muzzle)
    return mount


def _write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")


def add_arm_weapon(image: Path, assets: Path, data: Path, mods: Path,
                   spec_path: Path = SPEC) -> dict[str, Any]:
    """Writes the new part's models, texture and table overlays under `mods`."""
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    stem = spec["model"]
    with SlaiDisc(image) as disc:
        nodes = read_pod(disc.read(disc.find(f"{stem}.PO2")[0]))
        picture = read_pig(disc.read(disc.find(f"{stem}{spec['paint']:02d}.PIG")[0]))

    tables = {name: json.loads((assets / name).read_text(encoding="utf-8")) for name in _ROBOT_TABLES}
    catalogue = tables["robots/catalogue.json"]
    template_part = spec["template_part"]
    template_type = catalogue["parts"]["arm"][template_part]
    template_models = catalogue["arm_types"][template_type]["models"]

    texture_name = f"{stem.lower()}{spec['paint']:02d}.png"
    texture_file = mods / MOD_TEXTURES / texture_name
    texture_file.parent.mkdir(parents=True, exist_ok=True)
    png.write_rgba(texture_file, picture.width, picture.height, picture.tobytes())

    parts = _meshes(nodes)
    mounts = tables["robots/weapon_mounts.json"]["weapons"]
    for side, resource in enumerate(spec["resources"]):
        template = assets / "models" / "binlink" / f"{template_models[side]}.gltf"
        model = f"models/binlink/{resource}.gltf"
        _write_json(mods / model, _arm_model(template, parts, side, f"../../{MOD_TEXTURES}/{texture_name}"))
        template_mount = mounts[f"models/binlink/{template_models[side]}.gltf"]
        mounts[model] = _muzzles(template_mount, nodes, spec["muzzle_nodes"], side)

    new_type = len(catalogue["arm_types"])
    catalogue["arm_types"].append(
        {**catalogue["arm_types"][template_type], "models": list(spec["resources"])}
    )
    catalogue["parts"]["arm"].append(new_type)
    for side in tables["robots/skeletons.json"]["arms"]:
        side.append(copy.deepcopy(side[template_type]))

    weapons = tables["tables/parts.json"]["arm_weapons"]
    record = copy.deepcopy(weapons[template_part])
    record.update(copy.deepcopy(spec["record"]))
    record.update({"index": len(weapons), "name": spec["name"], "model": new_type})
    weapons.append(record)
    for name, value in tables.items():
        _write_json(mods / name, value)

    preview = json.loads((data / "ui" / "robot_preview.json").read_text(encoding="utf-8"))
    places = preview["lone_part"]["by_type"]["arms"]
    places += [places[template_type]] * (new_type + 1 - len(places))
    places[new_type] = spec.get("lone_place", places[template_type])
    _write_json(mods / "data" / "ui" / "robot_preview.json",
                {"lone_part": {"by_type": {"arms": places}}})
    return {"part": record["index"], "arm_type": new_type, "name": spec["name"],
            "models": [f"models/binlink/{r}.gltf" for r in spec["resources"]],
            "triangles": sum(len(p[0].batch.triangles) for p in parts)}
