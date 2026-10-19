"""Assembles one MechAssault special effect from its files.

An effect ``\\SpecialEffects\\<dir>\\<name>\\`` holds:

- ``<name>.NODE``: an ini-style scene node tree (``NODE``, ``3DOBJECT``,
  ``facing`` and ``LIGHT`` nodes in pre-order, ``num_children`` each). A
  node's ``mesh`` is a ``.mesh`` file naming its ``.geomVert`` and
  ``.geomFace`` buffers and a ``.mat`` material (blending, two-sided
  flag, base ``.tif``, wrap modes). Rotation angles are radians.
- ``<name>.mtb``: a property stream of modifiers that act on a node by
  name, with times in milliseconds: ``Scale``, ``Rotate``, ``KillEntity``,
  ``Show``, ``Blend`` (alpha ramp), ``Blink``, ``Effect`` (a ``.cfx``
  particle emitter), ``DecalStamp`` and a few more.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import re
from typing import Any

from .mgf import BACKSLASH, Collection, parse_properties
from .texture import Geometry, Image, decode_geometry, decode_texture

EFFECT_ROOT = BACKSLASH + "SpecialEffects" + BACKSLASH
MODIFIER_PREFIX = "MAModSubObject"
# Bookkeeping that every entity carries; not part of a modifier's meaning.
_BOOKKEEPING = {
    "child",
    "parent",
    "sibling",
    "UID",
    "SetCount",
    "CreationTime",
    "TemplateID",
    "IsCollidable",
    "VisibleEntity",
    "OverrideTemplate",
    "Description",
    "ResourceType",
    "RespondState",
    "MinDamagePercent",
    "MaxDamagePercent",
    "MinHeating",
    "MaxHeating",
    "TimingFlags",
    "SetCount",
}


def join_path(base: str, relative: str) -> str:
    """`relative` (slashes either way, ``..`` allowed) against directory `base`."""
    parts = base.rstrip(BACKSLASH).split(BACKSLASH)
    for segment in re.split(r"[\\/]", relative):
        if segment == "..":
            parts.pop()
        elif segment and segment != ".":
            parts.append(segment)
    return BACKSLASH.join(parts)


def parse_ini(text: str) -> list[tuple[str, dict[str, str]]]:
    sections: list[tuple[str, dict[str, str]]] = []
    for line in text.replace("\r", "").split("\n"):
        line = line.split(";")[0].strip()
        if line.startswith("[") and line.endswith("]"):
            sections.append((line[1:-1], {}))
        elif "=" in line and sections:
            key, value = line.split("=", 1)
            sections[-1][1][key.strip()] = value.strip().strip('"')
    return sections


def _floats(text: str) -> list[float]:
    return [float(part) for part in text.split(",")]


@dataclass
class MeshAsset:
    key: str  # the .mesh path, lower case
    geometry: Geometry
    texture_key: str | None
    additive: bool
    two_sided: bool
    wrap: tuple[str, str]
    opacity: float
    colour: tuple[int, int, int]
    lit: bool
    animated: bool


@dataclass
class Resources:
    meshes: dict[str, MeshAsset] = field(default_factory=dict)
    textures: dict[str, Image] = field(default_factory=dict)


class Loader:
    def __init__(self, files: Collection, names: dict[int, str]):
        self.files = files
        self.names = names
        self.resources = Resources()
        self.missing: list[str] = []

    def _text(self, path: str) -> str | None:
        data = self.files.get(path)
        return None if data is None else data.decode("latin1")

    def mesh(self, directory: str, relative: str) -> MeshAsset | None:
        mesh_path = join_path(directory, relative)
        key = mesh_path.lower()
        if key in self.resources.meshes:
            return self.resources.meshes[key]
        text = self._text(mesh_path)
        if text is None:
            self.missing.append(mesh_path)
            return None
        fields = dict(parse_ini(text))["mesh"]
        mesh_dir = mesh_path.rsplit(BACKSLASH, 1)[0]
        vertices = self.files.get(join_path(mesh_dir, fields["vertices"]))
        faces = self.files.get(join_path(mesh_dir, fields["faces"]))
        if vertices is None or faces is None:
            self.missing.append(mesh_path + " (geometry)")
            return None
        material_path = join_path(mesh_dir, fields["material"])
        material = self._text(material_path)
        sections = [s for s in parse_ini(material or "") if s[0] != "fileinfo"]
        if not sections:
            self.missing.append(material_path)
            return None
        mat = sections[0][1]
        texture_key = self._texture(material_path.rsplit(BACKSLASH, 1)[0], mat.get("base", ""))
        tiling = [t.strip() for t in mat.get("base_tiling", "U_WRAP | V_WRAP").split("|")]
        diffuse = tuple(int(v) for v in mat.get("diff_color", "255,255,255").split(","))
        asset = MeshAsset(
            key=key,
            geometry=decode_geometry(vertices, faces),
            texture_key=texture_key,
            additive=mat.get("blending") == "additive",
            two_sided=mat.get("two_sided") == "true",
            wrap=("U_CLAMP" in tiling and "clamp" or "repeat", "V_CLAMP" in tiling and "clamp" or "repeat"),
            opacity=float(mat.get("opacity", "1")),
            colour=diffuse,  # type: ignore[arg-type]
            lit=mat.get("shading", "none") != "none",
            animated=mat.get("type") == "ANIMATED",
        )
        self.resources.meshes[key] = asset
        return asset

    def _texture(self, directory: str, relative: str) -> str | None:
        if not relative:
            return None
        path = join_path(directory, relative)
        key = path.lower()
        if key in self.resources.textures:
            return key
        data = self.files.get(path)
        if data is None:
            self.missing.append(path)
            return None
        self.resources.textures[key] = decode_texture(data)
        return key

    def effect(self, directory: str, name: str) -> dict[str, Any] | None:
        base = f"{EFFECT_ROOT}{directory}{BACKSLASH}{name}"
        text = self._text(f"{base}{BACKSLASH}{name}.NODE")
        if text is None:
            return None
        nodes = self._nodes(base, text)
        modifiers: list[dict[str, Any]] = []
        blob = self.files.get(f"{base}{BACKSLASH}{name}.mtb")
        if blob is not None:
            for class_name, properties in parse_properties(blob, self.names):
                if class_name.startswith(MODIFIER_PREFIX):
                    modifiers.append(_modifier(class_name[len(MODIFIER_PREFIX) :], properties))
        return {"nodes": nodes, "modifiers": modifiers}

    def tree(self, directory: str, name: str) -> list[dict[str, Any]] | None:
        """The node tree of ``<directory>\\<name>.NODE`` (a model, not an effect)."""
        text = self._text(f"{directory}{BACKSLASH}{name}.NODE")
        return None if text is None else self._nodes(directory, text)

    def _nodes(self, base: str, text: str) -> list[dict[str, Any]]:
        out: list[dict[str, Any]] = []
        pending: list[int] = []  # open parents' remaining child counts, as node indices
        remaining: list[int] = []
        for name, fields in parse_ini(text):
            if name == "fileinfo":
                continue
            while pending and remaining[-1] == 0:
                pending.pop()
                remaining.pop()
            parent = pending[-1] if pending else -1
            if remaining:
                remaining[-1] -= 1
            node: dict[str, Any] = {
                "name": name,
                "kind": fields.get("type", "NODE").lower(),
                "parent": parent,
                "position": _floats(fields.get("position", "0,0,0")),
                "axis": _floats(fields.get("rot_axis", "1,0,0")),
                "angle": float(fields.get("rot_angle", "0")),
                "scale": _floats(fields.get("scale", "1,1,1")),
            }
            if "mesh" in fields:
                mesh = self.mesh(base, fields["mesh"])
                node["mesh"] = mesh.key if mesh else None
            if node["kind"] == "light":
                node["light"] = {
                    k[6:]: v for k, v in fields.items() if k.startswith("light_") and "lightmap" not in k
                }
            out.append(node)
            count = int(fields.get("num_children", "0"))
            if count:
                pending.append(len(out) - 1)
                remaining.append(count)
        return out


def _modifier(kind: str, properties: dict[str, object]) -> dict[str, Any]:
    out: dict[str, Any] = {"type": kind}
    for key, value in properties.items():
        if key not in _BOOKKEEPING:
            out[key] = value
    return out
