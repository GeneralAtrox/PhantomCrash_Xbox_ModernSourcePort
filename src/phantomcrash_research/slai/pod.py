"""Reader for S.L.A.I.'s ``POD2`` models (``.PO2``).

Layout, found by inspection (names are researcher-assigned):

- Header: ``POD2``, u32 version (1), u32 node count, two u32 zero; at
  +0x14 u16 material count, u16 batch count, u16 mesh count, u16 vertex
  quadword count; at +0x1c u32 offsets of the nodes, node infos,
  materials, batches, bone lists and vertex data.
- Node (0x50 bytes): i16 parent, first child, next sibling, pad; u32 info
  offset; 8 bytes pad; a row-major 4x4 matrix (translation in row 3),
  relative to the parent.
- Node info (0x40 bytes): u32 material count, u32 (8), u32 triangle count,
  u32 offset of its first material; bounding box min and max (4 floats
  each); u32 bone count, u32 bone list offset. A node without geometry has
  0 materials and an empty box (+-1e10).
- Material (0x30 bytes): u32 (1), u32 flags (low byte 7; 0x80000000 and
  0x40000000 seen), u32 0, u32 batch offset; f32 128, u32, 2 pad; f32
  r, g, b (127.5 = full), pad.
- Batch (0x20 bytes): u32 vertex count, u32 (0x7c), 2 pad, then the
  offsets of four quadword arrays: positions (x, y, z, w flags), normals,
  colours (0..128 floats) and texture coordinates (s, t, q).
- Positions are triangle strips: the w word's bit 15 set draws a triangle
  ending at that vertex; clear starts a new strip.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import struct

MAGIC = b"POD2"
NODE_SIZE = 0x50
DRAW_BIT = 0x8000
EMPTY_BOX = 1e9


@dataclass
class Batch:
    positions: list[tuple[float, float, float]]
    normals: list[tuple[float, float, float]]
    colours: list[tuple[float, float, float, float]]  # 0..1
    uvs: list[tuple[float, float]]
    triangles: list[tuple[int, int, int]]


@dataclass
class Material:
    flags: int
    colour: tuple[float, float, float]  # 0..1
    batch: Batch
    texture: int = 0


@dataclass
class Node:
    parent: int
    matrix: tuple[float, ...]  # row-major 4x4, translation in row 3
    materials: list[Material] = field(default_factory=list)
    box: tuple[tuple[float, ...], tuple[float, ...]] | None = None


def _strip_triangles(flags: list[int]) -> list[tuple[int, int, int]]:
    triangles = []
    drawn = 0  # triangles drawn since the strip started
    for i, flag in enumerate(flags):
        if not flag & DRAW_BIT:
            drawn = 0
        elif i >= 2:
            # The winding alternates along the strip.
            triangles.append((i - 1, i - 2, i) if drawn % 2 else (i - 2, i - 1, i))
            drawn += 1
    return triangles


def _quads(data: bytes, offset: int, count: int) -> list[tuple[float, ...]]:
    return [struct.unpack_from("<4f", data, offset + 16 * i) for i in range(count)]


def _batch(data: bytes, offset: int) -> Batch:
    count = struct.unpack_from("<I", data, offset)[0]
    at_pos, at_normal, at_colour, at_uv = struct.unpack_from("<4I", data, offset + 16)
    flags = [struct.unpack_from("<I", data, at_pos + 16 * i + 12)[0] for i in range(count)]
    positions = [q[:3] for q in _quads(data, at_pos, count)]
    normals = [q[:3] for q in _quads(data, at_normal, count)] if at_normal else [(0, 0, 0)] * count
    colours = ([tuple(min(c / 128.0, 1.0) for c in q) for q in _quads(data, at_colour, count)]
               if at_colour else [(1, 1, 1, 1)] * count)
    uvs = ([(q[0] / (q[2] or 1.0), q[1] / (q[2] or 1.0)) for q in _quads(data, at_uv, count)]
           if at_uv else [(0, 0)] * count)
    return Batch(positions, normals, colours, uvs, _strip_triangles(flags))


def read_pod(data: bytes) -> list[Node]:
    if data[:4] != MAGIC:
        raise ValueError("Not a POD2 model")
    node_count = struct.unpack_from("<I", data, 8)[0]
    at_nodes = struct.unpack_from("<I", data, 0x1C)[0]
    nodes = []
    for index in range(node_count):
        at = at_nodes + NODE_SIZE * index
        parent, _, _, _, at_info = struct.unpack_from("<4hI", data, at)
        node = Node(parent, struct.unpack_from("<16f", data, at + 16))
        material_count, _, _, at_material = struct.unpack_from("<4I", data, at_info)
        low = struct.unpack_from("<3f", data, at_info + 16)
        high = struct.unpack_from("<3f", data, at_info + 32)
        if material_count and high[0] < EMPTY_BOX:
            node.box = (low, high)
        for m in range(material_count):
            at_m = at_material + 0x30 * m
            flags, texture, at_batch = struct.unpack_from("<3I", data, at_m + 4)
            colour = struct.unpack_from("<3f", data, at_m + 32)
            node.materials.append(
                Material(flags, tuple(min(c / 127.5, 1.0) for c in colour), _batch(data, at_batch), texture)
            )
        nodes.append(node)
    return nodes


def multiply(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, ...]:
    """Row-major a * b (a applied first, as for row vectors)."""
    return tuple(
        sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)
    )


def world_matrices(nodes: list[Node]) -> list[tuple[float, ...]]:
    worlds: list[tuple[float, ...]] = []
    for node in nodes:
        parent = worlds[node.parent] if 0 <= node.parent < len(worlds) else None
        worlds.append(multiply(node.matrix, parent) if parent else node.matrix)
    return worlds


def transform(matrix: tuple[float, ...], point: tuple[float, ...]) -> tuple[float, float, float]:
    x, y, z = point[:3]
    m = matrix
    return (
        x * m[0] + y * m[4] + z * m[8] + m[12],
        x * m[1] + y * m[5] + z * m[9] + m[13],
        x * m[2] + y * m[6] + z * m[10] + m[14],
    )
