"""Native part adapters: source geometry and locomotion, existing mixed-part mounts."""

from __future__ import annotations

import copy
import math
import numpy as np

from ..gltf_edit import Builder
from .arm_weapon import _facing
from .pod import world_matrices
from .motion import read_motion
from .motion_sampling import native_pmt_track, motion_period
from .motion_resources import native_clip_names, NATIVE_ACTIONS, ACTION_PROVENANCE

AXES = np.array([1., -1., -1.])
GLTF = np.array([.1, -.1, .1])


def euler(matrix):
    """XYZ row-vector angles, with a stable gimbal-lock branch."""
    m = np.array(matrix).reshape(4, 4)[:3, :3].copy()
    m /= np.linalg.norm(m, axis=1)[:, None]
    y = math.asin(float(np.clip(-m[0, 2], -1, 1)))
    if abs(math.cos(y)) > 1e-6:
        return [math.atan2(m[1, 2], m[2, 2]), y, math.atan2(m[0, 1], m[0, 0])]
    return [math.atan2(-m[2, 1], m[1, 1]), y, 0.]


def quaternion_matrix(q):
    w, x, y, z = np.array(q) / np.linalg.norm(q)
    m = np.eye(4)
    m[:3, :3] = np.array([
        [1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w)],
        [2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w)],
        [2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y)]])
    return m


def native_matrix(matrix):
    m = np.array(matrix).reshape(4, 4).copy()
    m[:3, :3] *= AXES[:, None] * AXES[None, :]
    m[3, :3] *= AXES * .1
    return m


def flat_model(nodes, texture, *, side=None, offset=None):
    builder = Builder()
    primitives = []
    worlds = [np.array(w).reshape(4, 4) for w in world_matrices(nodes)]
    origin = worlds[0][3, :3]
    axes = GLTF.copy()
    if side == 0:
        axes[0] *= -1
    for node, world in zip(nodes, worlds):
        for material in node.materials:
            b = material.batch
            if not b.triangles:
                continue
            positions = (np.array(b.positions) @ world[:3, :3] + world[3, :3] - origin) * axes
            if offset is not None:
                positions += offset
            normals = np.array(b.normals) @ world[:3, :3] * np.sign(axes)
            indices = _facing(np.array(b.triangles, np.uint32), positions, normals)
            colours = np.clip(np.array(b.colours) * np.append(material.colour, 1.) * 255, 0, 255)
            primitives.append(builder.primitive(positions, normals, np.array(b.uvs),
                                                 colours.round(), indices, 0))
    return document(builder, [{"name": "slai_bind_geometry", "mesh": 0}],
                    [{"primitives": primitives}], texture)


def document(builder, nodes, meshes, texture):
    result = {"asset": {"version": "2.0", "generator": "S.L.A.I. native part adapter"},
            "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes,
            "meshes": meshes, "materials": [{"pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0}, "metallicFactor": 0, "roughnessFactor": 1},
                "doubleSided": True, "alphaMode": "MASK", "alphaCutoff": .01,
                "extras": {"part_paint": True}}],
            "images": [{"uri": texture}], "textures": [{"source": 0}],
            "accessors": builder.accessors, "bufferViews": builder.views,
            "buffers": [builder.buffer()]}
    if not texture:
        result.pop("images")
        result.pop("textures")
        result["materials"][0]["pbrMetallicRoughness"].pop("baseColorTexture")
    return result


def walking_model(nodes, texture, torso, wheel_nodes=(), suspension=None, rolls=None,
                  body_node=1):
    builder = Builder()
    meshes, draw_nodes, bones, mesh_scales = [], [], {}, {}
    for i, n in enumerate(nodes):
        basis = np.array(n.matrix).reshape(4, 4)[:3, :3]
        scale = np.linalg.norm(basis, axis=1)
        mesh_scales[i] = scale.tolist()
        if np.any(scale < 1e-6):
            raise ValueError(f"S.L.A.I. leg node {i} has a singular bind transform")
        if not np.allclose(scale, 1, atol=1e-5) and any(child.parent == i for child in nodes):
            raise ValueError(f"S.L.A.I. leg control {i} needs hierarchical scale playback")
        primitives = []
        for material in n.materials:
            b = material.batch
            if not b.triangles:
                continue
            # The native skeleton carries rotation/translation. The three
            # scaled source leg nodes are leaf meshes, so retain their local
            # authored scale here, including the inverse scale for normals.
            p = np.array(b.positions) * scale * GLTF
            normals = np.array(b.normals) / scale * np.sign(GLTF)
            normals /= np.maximum(np.linalg.norm(normals, axis=1)[:, None], 1e-8)
            indices = _facing(np.array(b.triangles, np.uint32), p, normals)
            colours = np.clip(np.array(b.colours) * np.append(material.colour, 1.) * 255, 0, 255)
            primitives.append(builder.primitive(p, normals, np.array(b.uvs), colours.round(), indices, 0))
        if primitives:
            bones[i] = 1 + len(meshes)
            draw_nodes.append({"name": f"slai_source_node_{i}", "mesh": len(meshes)})
            meshes.append({"primitives": primitives})
    if len(meshes) >= 64:
        raise ValueError("S.L.A.I. leg exceeds native bone capacity")

    mount = copy.deepcopy(torso)
    # The body is attached to the compiled leg control, not a frozen copy of
    # its rest transform. Its clip-specific translation and tilt must carry
    # the torso and arms along with the adjacent leg geometry.
    def body_slots(n, root=False):
        n["bone"] = 0 if root else -1
        n["track"] = -1
        for child in n.get("children", []):
            body_slots(child)
    body_slots(mount, True)
    mount["translation"] = [0, 0, 0]
    mount["rotation"] = [0, 0, 0]
    if not 0 <= body_node < len(nodes):
        raise ValueError("S.L.A.I. leg body attachment is outside its hierarchy")

    def tree(i):
        m = native_matrix(nodes[i].matrix)
        if nodes[i].parent < 0:
            # The original constructor clears the model's root bind matrix
            # (0x1b2a9c..1b2aa8); PMT root travel belongs to the actor.
            m = np.eye(4)
        angles = euler(m)
        children = [tree(j) for j, n in enumerate(nodes) if n.parent == i]
        if i == body_node:
            children.insert(0, mount)
        out = {"type": 0, "flags": 512 if i in wheel_nodes else 1, "track": i,
                "translation": (m[3, :3] * [1, 1, -1]).tolist(),
                "rotation": (np.array(angles) * [-1, -1, 1]).tolist(),
                "bone": bones.get(i, -1),
                "children": children}
        if i == 0:
            out["source_model_root"] = True
        if suspension and i in suspension:
            if children:
                raise ValueError("S.L.A.I. suspension control must be a leaf")
            out["source_suspension"] = suspension[i]
        if rolls and i in rolls:
            if children:
                raise ValueError("S.L.A.I. rolling control must be a leaf")
            out["source_roll"] = rolls[i]
            if rolls[i].get('spherical'):
                # The spherical callback replaces its local basis. Keep the bind
                # scale at rest, then cancel this baked geometry scale at playback.
                out["source_roll"]["baked_mesh_scale"] = mesh_scales[i]
        return out

    roots = [tree(i) for i, n in enumerate(nodes) if n.parent < 0]
    skeleton = {"type": 0, "flags": 1, "track": -1, "bone": -1,
                "translation": [0, 0, 0], "rotation": [0, 0, 0], "children": roots}
    return document(builder, draw_nodes, meshes, texture), skeleton


def channels(times, values):
    return [{"kind": "linear" if len(values)>1 else "constant",
             "keys": times[1:], "values": [float(v[i]) for v in values]} for i in range(3)]


def motion_bank(disc, files, nodes, stem, profile=None):
    clips, timings = [], []
    names = native_clip_names(profile) if profile else [f"{stem}{i+1:02d}" for i in range(37)]
    for slot, clip_name in enumerate(names):
        name = clip_name + ".PMT"
        file = next((f for f in files if f.name == name), None)
        if not file:
            clips.append(None)
            timings.append([0, 0, 0, 0])
            continue
        source = read_motion(disc.read(file))
        if len(source["tracks"]) > len(nodes):
            raise ValueError(f"Motion track count exceeds the source rig: {name}")
        tracks = []
        for i, node in enumerate(nodes):
            track = source["tracks"][i] if i < len(source["tracks"]) else {"keys": [], "flags": 0}
            bind = track["bind_matrix"] if track["flags"] & 0x8000 else node.matrix
            keys = track["keys"] or [{"frame": 0}]
            times, positions, rotations = [], [], []
            for key in keys:
                times.append(key["frame"])
                delta = np.array(key.get("translation", [0, 0, 0]))
                positions.append((delta + np.array(bind[12:15])) * [.1, .1, -.1])
                q = key.get("quaternion_wxyz")
                # 0x1a05f0 writes the transpose of a row-vector quaternion
                # matrix. Body glTF channels already use this convention.
                matrix = quaternion_matrix(q).T if q is not None else np.array(bind).reshape(4, 4)
                rotations.append(np.array(euler(native_matrix(matrix))) * [-1, 1, -1])
            rotations = np.unwrap(np.array(rotations), axis=0)
            tracks.append({"flags": 0x7e0, "translation": channels(times, positions),
                           "rotation": channels(times, rotations),
                           "source_pmt": native_pmt_track(track, node.matrix)})
        end = motion_period(source)
        clips.append({"frames": end, "tracks": tracks, "source": name})
        timings.append([0, 0, end, end])
    result = {"track_count": len(nodes), "motions": clips,
              "adapter": "Original PMT quaternion keys and per-control cursors; Euler channels retained for native adapters",
              "source_sampling": "0x19fd30;0x19f520;0x19f900;0x19ee50;0x1a05f0"}
    if profile:
        result.update({"source_descriptor": profile["address"],
                       "source_node_callbacks": profile["node_callbacks"],
                       "source_action_clips": profile["source_action_clips"],
                       "source_body_attachment_node": profile["attachment_nodes"][0],
                       "verified_native_actions": {str(k): {"action": v, "producer": ACTION_PROVENANCE[k]}
                                                   for k, v in NATIVE_ACTIONS.items()},
                       "remaining_selection": "Untraced native states retain the numbered-filename adapter"})
    return result, timings
