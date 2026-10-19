"""Export POD node hierarchies and rigid meshes into editable glTF assets."""

from __future__ import annotations

import re
import struct

import numpy as np

from ..gltf_edit import Builder, FLOAT
from .arm_weapon import _facing
from .pod import read_pod

AXES = np.array([1.0, -1.0, 1.0])
SCALE = AXES * 0.1


def texture_names(data: bytes) -> list[str]:
    count = struct.unpack_from("<I", data)[0]
    if count > 4096 or 4 + count * 4 > len(data):
        raise ValueError("Invalid TXI texture count")
    offsets = struct.unpack_from(f"<{count}I", data, 4)
    return [data[o:data.index(0, o)].decode("ascii").upper() for o in offsets]


def node_names(data: bytes) -> dict[int, str]:
    text = data.decode("cp932", errors="replace").split("[Removed", 1)[0]
    return {int(i): name for i, name in re.findall(r"(?:^|\s)(\d+):([^\s]+)", text)}


def local_matrix(matrix: list | tuple) -> np.ndarray:
    value = np.array(matrix).reshape(4, 4).copy()
    value[:3, :3] *= AXES[:, None] * AXES[None, :]
    value[3, :3] *= SCALE
    return value


def rotation_quaternion(matrix):
    """XYZW quaternion for an orthonormal row-vector matrix."""
    m = np.array(matrix)[:3, :3].T
    trace = np.trace(m)
    if trace > 0:
        s = np.sqrt(trace + 1) * 2
        return np.array([(m[2, 1]-m[1, 2])/s, (m[0, 2]-m[2, 0])/s,
                         (m[1, 0]-m[0, 1])/s, s/4])
    i = int(np.argmax(np.diag(m)))
    j, k = (i+1) % 3, (i+2) % 3
    s = np.sqrt(1 + m[i, i] - m[j, j] - m[k, k]) * 2
    q = np.zeros(4)
    q[i], q[j], q[k], q[3] = s/4, (m[j, i]+m[i, j])/s, (m[k, i]+m[i, k])/s, (m[k, j]-m[j, k])/s
    return q


def model_document(data: bytes, textures: list[str], names: dict[int, str],
                   provenance: dict, motions: list[tuple[str, dict]] = ()) -> dict:
    builder = Builder()
    source = read_pod(data)
    nodes, meshes, materials = [], [], []
    for i, node in enumerate(source):
        out = {"name": names.get(i, f"source_node_{i}"),
               "matrix": local_matrix(node.matrix).ravel().tolist(),
               "extras": {"source_node": i, "source_parent": node.parent}}
        children = [j for j, n in enumerate(source) if n.parent == i]
        if children:
            out["children"] = children
        primitives = []
        for material in node.materials:
            batch = material.batch
            if not batch.triangles:
                continue
            positions = np.array(batch.positions) * SCALE
            normals = np.array(batch.normals) * AXES
            indices = np.array(batch.triangles, np.uint32)[:, [0, 2, 1]]
            if material.flags & 1:
                # Canonical glTF has counter-clockwise front faces. The
                # native bind-pose adapters separately use their renderer's
                # clockwise convention.
                indices = _facing(indices, positions, normals)[:, [0, 2, 1]]
            colours = np.clip(np.array(batch.colours) * 255, 0, 255).round()
            pbr = {"baseColorFactor": [*(material.colour if material.flags & 1 else (1, 1, 1)), 1.0],
                   "metallicFactor": 0, "roughnessFactor": 1}
            extras = {"source_flags": material.flags, "source_texture_index": material.texture}
            if material.flags & 4 and material.texture < len(textures) and textures[material.texture]:
                pbr["baseColorTexture"] = {"index": material.texture}
            elif material.flags & 4 and material.texture >= len(textures):
                extras["texture_binding"] = "unresolved"
            material_doc = {"pbrMetallicRoughness": pbr, "doubleSided": True,
                            "alphaMode": "MASK", "alphaCutoff": 0.01, "extras": extras}
            if not material.flags & 1:
                material_doc["extensions"] = {"KHR_materials_unlit": {}}
            materials.append(material_doc)
            primitive = builder.primitive(positions, normals, batch.uvs,
                                          colours, indices, len(materials) - 1)
            if not material.flags & 1:
                primitive["attributes"].pop("NORMAL")
            primitives.append(primitive)
        if primitives:
            out["mesh"] = len(meshes)
            meshes.append({"name": out["name"], "primitives": primitives})
        nodes.append(out)
    document = {"asset": {"version": "2.0", "generator": "phantomcrash POD2 asset exporter"},
                "scene": 0, "scenes": [{"nodes": [i for i, n in enumerate(source) if n.parent < 0]}],
                "nodes": nodes, "meshes": meshes, "materials": materials, "extras": provenance}
    if any("extensions" in m for m in materials):
        document["extensionsUsed"] = ["KHR_materials_unlit"]
    used = sorted({m["pbrMetallicRoughness"]["baseColorTexture"]["index"] for m in materials
                   if "baseColorTexture" in m["pbrMetallicRoughness"]})
    if used:
        remap = {old: new for new, old in enumerate(used)}
        for material in materials:
            binding = material["pbrMetallicRoughness"].get("baseColorTexture")
            if binding:
                binding["index"] = remap[binding["index"]]
        document.update({"images": [{"uri": f"../textures/{textures[i].lower()}.png"} for i in used],
                         "textures": [{"source": i} for i in range(len(used))]})
    document["extras"]["source_texture_names"] = textures
    animations = [_animation(name, motion, source, nodes, builder) for name, motion in motions]
    animations = [a for a in animations if a["channels"]]
    if animations:
        document["animations"] = animations
    if builder.blob:
        document.update({"accessors": builder.accessors, "bufferViews": builder.views,
                         "buffers": [builder.buffer()]})
    for field in ("meshes", "materials"):
        if not document[field]:
            document.pop(field)
    return document


def _animation(name: str, motion: dict, source: list, nodes: list, builder: Builder) -> dict:
    from .motion_sampling import motion_period, native_pmt_track
    channels, samplers = [], []
    last_frame = motion_period(motion)
    for track in motion["tracks"]:
        index = track["node"]
        if index >= len(source):
            raise ValueError(f"Motion {name} has more tracks than model nodes")
        keys = [dict(k) for k in track["keys"]]
        if not keys:
            keys = [{"frame": 0}, {"frame": max(last_frame, 1)}]
        # Alternate PMT bind translations are part of each clip, including
        # nodes whose animated channel is absent. Export constant channels
        # so changing clips restores that clip's own complete control pose.
        for key in keys:
            key.setdefault("translation", [0, 0, 0])
        # Animated glTF nodes use TRS, never a matrix. Preserve authored
        # scale too: S6S01H's second control node has a 0.91 scale.
        matrix = local_matrix(source[index].matrix)
        scale = np.linalg.norm(matrix[:3, :3], axis=1)
        rotation = matrix[:3, :3] / scale[:, None]
        if np.any(scale < 1e-6) or not np.allclose(rotation @ rotation.T, np.eye(3), atol=1e-5):
            raise ValueError(f"Motion {name} node {index} has a singular or sheared bind matrix")
        if np.linalg.det(rotation) < 0:
            scale[0] *= -1
            rotation[0] *= -1
        bind_rotation = rotation_quaternion(rotation)
        nodes[index].pop("matrix", None)
        nodes[index]["translation"] = matrix[3, :3].tolist()
        nodes[index]["rotation"] = bind_rotation.tolist()
        if not np.allclose(scale, 1, atol=1e-6):
            nodes[index]["scale"] = scale.tolist()
        for field, path, kind in [("translation", "translation", "VEC3"),
                                  ("quaternion_wxyz", "rotation", "VEC4")]:
            if field not in keys[0] and path != "rotation":
                continue
            frames, values = [], []
            for key in keys:
                if frames and key["frame"] <= frames[-1]:
                    raise ValueError(f"Motion {name} has non-increasing key times")
                frames.append(key["frame"])
                if path == "translation":
                    bind = track["bind_matrix"] if track["flags"] & 0x8000 else source[index].matrix
                    values.append((np.array(key[field]) + np.array(bind[12:15])) * SCALE)
                else:
                    if field in key:
                        w, x, y, z = key[field]
                        q = np.array([x, -y, z, w], dtype=float)
                    else:
                        q = bind_rotation.copy()
                    norm = np.linalg.norm(q)
                    if norm < 1e-6:
                        raise ValueError(f"Motion {name} has a zero quaternion")
                    q /= norm
                    if values and np.dot(values[-1], q) < 0:
                        q = -q
                    values.append(q)
            input_index = builder.add(np.array(frames, np.float32).reshape(-1, 1) / 60,
                                      "SCALAR", FLOAT, 0, bounds=True)
            output_index = builder.add(np.array(values, np.float32), kind, FLOAT, 0)
            samplers.append({"input": input_index, "output": output_index, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": index, "path": path}})
    extras = {"source_frames_per_export_second": 60}
    if all(not t['flags'] & 2 or t['flags'] & 1 for t in motion['tracks']):
        extras['source_pmt'] = [native_pmt_track(t, source[t['node']].matrix,
                                                baked_bind_scale=False)
                                for t in motion['tracks']]
        extras['source_period'] = last_frame
    return {"name": name, "samplers": samplers, "channels": channels, "extras": extras}
