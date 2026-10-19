"""Write a :class:`model.Model` as glTF 2.0 (a ``.gltf`` file and its ``.bin``).

- The game is left-handed; glTF is right-handed. Z is negated on positions,
  normals, tangents and node transforms, which also turns the game's
  clockwise front faces into glTF's counter-clockwise ones.
- Each model node becomes a glTF node; its run of batches becomes one glTF
  mesh with a primitive per batch. Batches draw indexed triangle lists from
  ``first_triangle`` (the executor's index offset), with absolute indices.
- Materials follow the draw executor (``0x0b3240``): texture 0 is the base
  colour, texture 1 the normal map of a bump batch; a texture outside the
  pack is an empty slot, read as opaque black; the vertex colour is the
  diffuse colour; flag 0x100 is unlit (``KHR_materials_unlit``), 0x4000 an
  alpha test at 0x80 (``MASK``), texture alpha on stage 0 blends, 0x800 is
  two-sided; each stage's address flags pick the sampler's wrap modes.
- The original values stay in ``extras.phantomcrash`` so nothing is lost:
  batch flags, stage flags, material colour, specular power and draw pass.
"""

from __future__ import annotations

import json
import math
from pathlib import Path
import struct

from . import model as mdl

FLOAT = 5126
UNSIGNED_SHORT = 5123
UNSIGNED_BYTE = 5121
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963
TRIANGLES = 4
REPEAT = 10497
MIRRORED_REPEAT = 33648
CLAMP_TO_EDGE = 33071
LINEAR = 9729
LINEAR_MIPMAP_LINEAR = 9987
UNLIT = "KHR_materials_unlit"
ALPHA_TEST_CUTOFF = 0x80 / 255


def _mirror(vector):
    return (vector[0], vector[1], -vector[2])


def _quaternion(rotation):
    """Game rotation (applied X, then Y, then Z) mirrored in Z, as glTF [x, y, z, w]."""

    def axis(index, angle):
        half = angle / 2
        q = [0.0, 0.0, 0.0, math.cos(half)]
        q[index] = math.sin(half)
        return q

    def multiply(a, b):
        ax, ay, az, aw = a
        bx, by, bz, bw = b
        return [
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz,
        ]

    x, y, z = rotation
    return multiply(axis(2, z), multiply(axis(1, -y), axis(0, -x)))


def _tangents(mesh: mdl.Mesh) -> list[tuple[float, float, float, float]]:
    """Per-vertex tangents along +U, in game space, with glTF's bitangent sign."""
    count = len(mesh.vertices)
    tangent = [[0.0, 0.0, 0.0] for _ in range(count)]
    bitangent = [[0.0, 0.0, 0.0] for _ in range(count)]
    for first in range(0, len(mesh.indices), 3):
        a, b, c = mesh.indices[first : first + 3]
        pa, pb, pc = (mesh.vertices[i].position for i in (a, b, c))
        ua, ub, uc = (mesh.vertices[i].uv for i in (a, b, c))
        e1 = [pb[k] - pa[k] for k in range(3)]
        e2 = [pc[k] - pa[k] for k in range(3)]
        du1, dv1 = ub[0] - ua[0], ub[1] - ua[1]
        du2, dv2 = uc[0] - ua[0], uc[1] - ua[1]
        det = du1 * dv2 - du2 * dv1
        if abs(det) < 1e-12:
            continue
        r = 1.0 / det
        t = [(e1[k] * dv2 - e2[k] * dv1) * r for k in range(3)]
        s = [(e2[k] * du1 - e1[k] * du2) * r for k in range(3)]
        for i in (a, b, c):
            for k in range(3):
                tangent[i][k] += t[k]
                bitangent[i][k] += s[k]
    result = []
    for vertex, t, s in zip(mesh.vertices, tangent, bitangent):
        n = vertex.normal
        dot = sum(n[k] * t[k] for k in range(3))
        t = [t[k] - n[k] * dot for k in range(3)]
        length = math.sqrt(sum(value * value for value in t))
        if length < 1e-9:
            # No usable UV gradient: any unit vector perpendicular to the normal.
            t = [n[1], -n[0], 0.0] if abs(n[2]) < 0.9 else [0.0, n[2], -n[1]]
            length = math.sqrt(sum(value * value for value in t)) or 1.0
        t = [value / length for value in t]
        cross = (n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0])
        # glTF's bitangent points up the texture, against +V, and mirroring Z
        # flips the sign too; the two cancel, so the sign is that of +V here.
        sign = -1.0 if sum(cross[k] * s[k] for k in range(3)) < 0 else 1.0
        result.append((t[0], t[1], t[2], sign))
    return result


class _Writer:
    def __init__(self, texture_uris: list[str] | None):
        self.blob = bytearray()
        self.texture_uris = texture_uris or []
        self.doc: dict = {
            "asset": {"version": "2.0", "generator": "phantomcrash_research"},
            "buffers": [],
            "bufferViews": [],
            "accessors": [],
            "images": [],
            "samplers": [],
            "textures": [],
            "materials": [],
            "meshes": [],
            "nodes": [],
            "scenes": [{"nodes": []}],
            "scene": 0,
        }
        self.cache: dict[tuple, int] = {}

    def view(self, data: bytes, target: int) -> int:
        while len(self.blob) % 4:
            self.blob.append(0)
        self.doc["bufferViews"].append(
            {"buffer": 0, "byteOffset": len(self.blob), "byteLength": len(data), "target": target}
        )
        self.blob += data
        return len(self.doc["bufferViews"]) - 1

    def accessor(self, view: int, component: int, count: int, kind: str, **extra) -> int:
        entry = {"bufferView": view, "componentType": component, "count": count, "type": kind}
        entry.update(extra)
        self.doc["accessors"].append(entry)
        return len(self.doc["accessors"]) - 1

    def cached(self, key: tuple, section: str, entry: dict) -> int:
        if key not in self.cache:
            self.doc[section].append(entry)
            self.cache[key] = len(self.doc[section]) - 1
        return self.cache[key]

    def texture(self, index: int, stage_flags: int) -> int | None:
        if not 0 <= index < len(self.texture_uris):
            return None
        uri = self.texture_uris[index]
        image = self.cached(("image", uri), "images", {"uri": uri})

        def wrap(repeat: int, mirror: int) -> int:
            if stage_flags & repeat:
                return REPEAT
            return MIRRORED_REPEAT if stage_flags & mirror else CLAMP_TO_EDGE

        s = wrap(mdl.STAGE_WRAP_U, mdl.STAGE_MIRROR_U)
        t = wrap(mdl.STAGE_WRAP_V, mdl.STAGE_MIRROR_V)
        sampler = self.cached(
            ("sampler", s, t),
            "samplers",
            {"magFilter": LINEAR, "minFilter": LINEAR_MIPMAP_LINEAR, "wrapS": s, "wrapT": t},
        )
        return self.cached(("texture", image, sampler), "textures", {"source": image, "sampler": sampler})

    def material(self, batch: mdl.Batch) -> int:
        key = ("material", batch.flags, batch.stage_flags, batch.textures, batch.diffuse, batch.power)
        if key in self.cache:
            return self.cache[key]
        stage0 = batch.stage_flags[0]
        base = self.texture(batch.textures[0], stage0)
        pbr: dict = {"baseColorFactor": [1.0, 1.0, 1.0, 1.0], "metallicFactor": 0.0, "roughnessFactor": 1.0}
        if base is not None:
            pbr["baseColorTexture"] = {"index": base}
        elif batch.textures[0] >= 0 and self.texture_uris:
            # A texture the pack does not hold is an empty slot, which the
            # hardware reads as opaque black.
            pbr["baseColorFactor"] = [0.0, 0.0, 0.0, 1.0]
        entry: dict = {"name": f"batch_{len(self.doc['materials'])}", "pbrMetallicRoughness": pbr}
        if batch.bump:
            normal = self.texture(batch.textures[1], batch.stage_flags[1])
            if normal is not None:
                entry["normalTexture"] = {"index": normal}
        if batch.flags & mdl.BATCH_ALPHA_TEST:
            entry["alphaMode"] = "MASK"
            entry["alphaCutoff"] = round(ALPHA_TEST_CUTOFF, 4)
        elif (base is not None and stage0 & mdl.STAGE_TEXTURE_ALPHA) or batch.flags & mdl.BATCH_ADDITIVE:
            entry["alphaMode"] = "BLEND"
        if batch.flags & mdl.BATCH_TWO_SIDED:
            entry["doubleSided"] = True
        if batch.flags & mdl.BATCH_UNLIT:
            entry["extensions"] = {UNLIT: {}}
        entry["extras"] = {
            "phantomcrash": {
                "flags": batch.flags,
                "stage_flags": list(batch.stage_flags),
                "textures": list(batch.textures),
                "diffuse": f"#{batch.diffuse:08x}",
                "power": batch.power,
            }
        }
        self.doc["materials"].append(entry)
        self.cache[key] = len(self.doc["materials"]) - 1
        return self.cache[key]

    def mesh_attributes(self, mesh: mdl.Mesh) -> dict:
        vertices = mesh.vertices
        positions = [_mirror(v.position) for v in vertices]
        low = [min(p[k] for p in positions) for k in range(3)]
        high = [max(p[k] for p in positions) for k in range(3)]
        attributes = {
            "POSITION": self.accessor(
                self.view(b"".join(struct.pack("<3f", *p) for p in positions), ARRAY_BUFFER),
                FLOAT, len(vertices), "VEC3", min=low, max=high,
            ),
            "NORMAL": self.accessor(
                self.view(b"".join(struct.pack("<3f", *_mirror(v.normal)) for v in vertices), ARRAY_BUFFER),
                FLOAT, len(vertices), "VEC3",
            ),
        }
        if any(batch.bump for batch in mesh.batches):
            tangents = (struct.pack("<4f", t[0], t[1], -t[2], t[3]) for t in _tangents(mesh))
            attributes["TANGENT"] = self.accessor(
                self.view(b"".join(tangents), ARRAY_BUFFER), FLOAT, len(vertices), "VEC4"
            )
        colours = b"".join(
            bytes(((v.colour >> 16) & 0xFF, (v.colour >> 8) & 0xFF, v.colour & 0xFF, v.colour >> 24))
            for v in vertices
        )
        attributes["COLOR_0"] = self.accessor(
            self.view(colours, ARRAY_BUFFER), UNSIGNED_BYTE, len(vertices), "VEC4", normalized=True
        )
        attributes["TEXCOORD_0"] = self.accessor(
            self.view(b"".join(struct.pack("<2f", *v.uv) for v in vertices), ARRAY_BUFFER),
            FLOAT, len(vertices), "VEC2",
        )
        return attributes

    def add_mesh(self, mesh: mdl.Mesh, number: int) -> None:
        if not mesh.vertices:
            return
        attributes = self.mesh_attributes(mesh)
        indices = self.view(struct.pack(f"<{len(mesh.indices)}H", *mesh.indices), ELEMENT_ARRAY_BUFFER)
        first_node = len(self.doc["nodes"])
        for index, node in enumerate(mesh.nodes):
            entry: dict = {"name": f"mesh{number}_node{index}"}
            primitives = []
            for offset in range(node.batch_count):
                batch = mesh.batches[node.first_batch + offset]
                if offset < node.opaque_count:
                    draw_pass = 0
                elif offset < node.opaque_count + node.second_pass_count:
                    draw_pass = 1
                else:
                    draw_pass = 2
                primitives.append(
                    {
                        "attributes": attributes,
                        "indices": self.accessor(
                            indices, UNSIGNED_SHORT, batch.triangle_count * 3, "SCALAR",
                            byteOffset=batch.first_triangle * 6,
                        ),
                        "material": self.material(batch),
                        "mode": TRIANGLES,
                        "extras": {"phantomcrash": {"pass": draw_pass}},
                    }
                )
            if primitives:
                self.doc["meshes"].append({"name": entry["name"], "primitives": primitives})
                entry["mesh"] = len(self.doc["meshes"]) - 1
            entry["translation"] = list(_mirror(node.translation))
            entry["rotation"] = _quaternion(node.rotation)
            entry["scale"] = list(node.scale)
            self.doc["nodes"].append(entry)
        for index, node in enumerate(mesh.nodes):
            children = [first_node + i for i, other in enumerate(mesh.nodes) if other.parent == index]
            if children:
                self.doc["nodes"][first_node + index]["children"] = children
            if node.parent < 0:
                self.doc["scenes"][0]["nodes"].append(first_node + index)

    def finish(self, bin_name: str) -> dict:
        self.doc["buffers"].append({"uri": bin_name, "byteLength": len(self.blob)})
        if any(UNLIT in m.get("extensions", {}) for m in self.doc["materials"]):
            self.doc["extensionsUsed"] = [UNLIT]
        return {key: value for key, value in self.doc.items() if value != []}


def write(
    model: mdl.Model,
    target: Path,
    texture_uris: list[str] | None = None,
    mesh_limit: int | None = None,
    mesh_indices: set[int] | None = None,
) -> dict:
    """Write ``target`` (``.gltf``) and its ``.bin``; ``texture_uris[i]`` is the
    image, relative to ``target``, of texture ``i`` of the model's pack.
    ``mesh_limit`` keeps only the first meshes (a robot part's mesh 0 is the
    one drawn; the others are its shadow and distance versions).
    ``mesh_indices`` selects the meshes instantiated by a stage's object list,
    preserving their source indices in node names."""
    writer = _Writer(texture_uris)
    for number, mesh in enumerate(model.meshes[:mesh_limit]):
        if mesh_indices is None or number in mesh_indices:
            writer.add_mesh(mesh, number)
    bin_path = target.with_suffix(".bin")
    doc = writer.finish(bin_path.name)
    target.parent.mkdir(parents=True, exist_ok=True)
    bin_path.write_bytes(bytes(writer.blob))
    target.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
    return {
        "meshes": len(model.meshes),
        "vertices": sum(len(mesh.vertices) for mesh in model.meshes),
        "triangles": sum(len(mesh.indices) // 3 for mesh in model.meshes),
        "materials": len(doc.get("materials", [])),
    }


def write_instances(model: mdl.Model, target: Path, texture_uris: list[str],
                    instances: list[dict]) -> None:
    """Place selected meshes under object transforms, retaining each mesh's local nodes.

    Instances name a mesh, position and X/Y/Z rotation in game coordinates.
    Parent nodes keep those placements editable in ordinary glTF tools.
    """
    writer = _Writer(texture_uris)
    roots = writer.doc["scenes"][0]["nodes"]
    for instance in instances:
        mesh = instance["mesh"]
        if not 0 <= mesh < len(model.meshes):
            raise ValueError("Object mesh is outside its model resource")
        first_root = len(roots)
        writer.add_mesh(model.meshes[mesh], mesh)
        children = roots[first_root:]
        del roots[first_root:]
        roots.append(len(writer.doc["nodes"]))
        writer.doc["nodes"].append({
            "name": f"object{instance['index']}",
            "children": children,
            "translation": list(_mirror(instance["position"])),
            "rotation": _quaternion(instance["rotation"]),
            "extras": {"phantomcrash": instance},
        })
    document = writer.finish(target.with_suffix(".bin").name)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.with_suffix(".bin").write_bytes(bytes(writer.blob))
    target.write_text(json.dumps(document, indent=1) + "\n", encoding="utf-8")
