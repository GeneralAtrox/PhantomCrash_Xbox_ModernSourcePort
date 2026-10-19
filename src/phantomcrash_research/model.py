"""Reader for the game's model files (``Media\\model\\*_mdl.bin`` and the model
resources inside the ``Media\\binlink`` packs).

The layout below is the one the loader at ``0x0af17d`` walks and the draw
executor at ``0x0b3240`` consumes. Little-endian throughout:

- ``u32 magic`` (``0x80000101`` or ``0x80000102``), ``u32 mesh_count``.
- ``mesh_count`` meshes, each:

  - a 14-word header: flags (bit 1 would select a 9-word strip header that no
    shipped model uses), vertex count, FVF (``0x152`` or ``0xffffffff``), a
    second vertex count, triangle count, batch count, node count, bounding
    centre (3 floats), bounding half-extents (3 floats) and one more word;
  - an XDK ``D3DVertexBuffer`` resource header (3 words) and, with triangles,
    a ``D3DIndexBuffer`` header (3 words);
  - the vertices, 36 bytes each: position, normal, D3DCOLOR diffuse, UV
    (FVF ``XYZ | NORMAL | DIFFUSE | TEX1``);
  - ``u16`` triangle-list indices, absolute within the mesh; magic
    ``0x80000102`` pads the triangle count to a multiple of four;
  - batches (92 bytes): what one draw call uses;
  - nodes (92 bytes): a transform hierarchy, each drawing a run of batches.

Coordinates are the game's left-handed world units, Y up.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct

MAGICS = (0x80000101, 0x80000102)
PADDED_MAGIC = 0x80000102
HEADER_WORDS = 14
RESOURCE_HEADER_SIZE = 12
VERTEX_SIZE = 36
RECORD_SIZE = 92
MESH_STRIP_HEADER = 0x2  # mesh flag for the unused strip layout
NO_TEXTURE = -1
TEXTURE_STAGES = 4

# Batch flags (word 0), as the draw executor tests them.
BATCH_INDEXED = 0x2  # indexed triangle list (every shipped model)
BATCH_LIST = 0x4  # plain triangle list
BATCH_SPECULAR = 0x40
BATCH_UNLIT = 0x100
BATCH_ADDITIVE = 0x200  # SRCALPHA, ONE
BATCH_TWO_SIDED = 0x800  # cull none
BATCH_ALPHA_TEST = 0x4000  # blend off, alpha test at 0x80

# Texture stage flags (words 1-4, one per stage).
STAGE_ADD = 0x1  # colour op ADD instead of MODULATE
STAGE_BUMP = 0x2  # on stage 0 with two textures: the second is a normal map
STAGE_MIRROR_U = 0x4
STAGE_MIRROR_V = 0x8
STAGE_WRAP_U = 0x10
STAGE_WRAP_V = 0x20
STAGE_TEXTURE_ALPHA = 0x40  # alpha from the texture, not the vertex


class ModelError(ValueError):
    """The data is not a model this reader understands."""


@dataclass(frozen=True)
class Vertex:
    position: tuple[float, float, float]
    normal: tuple[float, float, float]
    colour: int  # D3DCOLOR, 0xAARRGGBB
    uv: tuple[float, float]


@dataclass(frozen=True)
class Batch:
    flags: int
    stage_flags: tuple[int, int, int, int]
    textures: tuple[int, int, int, int]  # indices into the model's texture pack, -1 for none
    diffuse: int  # material colour, 0xAARRGGBB
    vertex_count: int
    triangle_count: int
    first_index: int
    first_vertex: int
    first_triangle: int
    power: float  # specular power
    centre: tuple[float, float, float]
    extent: tuple[float, float, float]

    @property
    def bump(self) -> bool:
        """Drawn with the bump program: texture 1 is a normal map for texture 0."""
        return bool(self.stage_flags[0] & STAGE_BUMP) and min(self.textures[:2]) >= 0


@dataclass(frozen=True)
class Node:
    batch_count: int
    opaque_count: int  # the first batches, drawn in the opaque pass
    second_pass_count: int
    first_batch: int
    parent: int
    child: int
    sibling: int
    translation: tuple[float, float, float]
    rotation: tuple[float, float, float]  # radians; local = S * Rx * Ry * Rz * T (0x0d2753)
    scale: tuple[float, float, float]
    centre: tuple[float, float, float]
    extent: tuple[float, float, float]


@dataclass(frozen=True)
class Mesh:
    flags: int
    fvf: int
    centre: tuple[float, float, float]
    extent: tuple[float, float, float]
    vertices: list[Vertex]
    indices: list[int]
    batches: list[Batch]
    nodes: list[Node]


@dataclass(frozen=True)
class Model:
    magic: int
    meshes: list[Mesh]


def _floats(words: tuple[int, ...]) -> tuple[float, ...]:
    return struct.unpack(f"<{len(words)}f", struct.pack(f"<{len(words)}I", *words))


def _signed(value: int) -> int:
    return value - (1 << 32) if value & 0x80000000 else value


def _batch(words: tuple[int, ...]) -> Batch:
    floats = _floats(words)
    return Batch(
        flags=words[0],
        stage_flags=tuple(words[1:5]),
        textures=tuple(_signed(word) for word in words[5:9]),
        diffuse=words[10],
        vertex_count=words[11],
        triangle_count=words[12],
        first_index=words[13],
        first_vertex=words[14],
        first_triangle=words[15],
        power=float(words[16]),
        centre=floats[17:20],
        extent=floats[20:23],
    )


def _node(words: tuple[int, ...]) -> Node:
    floats = _floats(words)
    return Node(
        batch_count=words[1],
        opaque_count=words[2],
        second_pass_count=words[3],
        first_batch=words[4],
        parent=_signed(words[5]),
        child=_signed(words[6]),
        sibling=_signed(words[7]),
        translation=floats[8:11],
        rotation=floats[11:14],
        scale=floats[14:17],
        centre=floats[17:20],
        extent=floats[20:23],
    )


def _need(data: bytes, offset: int, size: int, what: str) -> None:
    if offset + size > len(data):
        raise ModelError(f"{what} lies outside the file")


def parse(data: bytes) -> Model:
    _need(data, 0, 8, "Header")
    magic, count = struct.unpack_from("<II", data, 0)
    if magic not in MAGICS:
        raise ModelError(f"Unknown model magic 0x{magic:08x}")
    offset = 8
    meshes = []
    for index in range(count):
        _need(data, offset, HEADER_WORDS * 4, f"Mesh {index} header")
        header = struct.unpack_from(f"<{HEADER_WORDS}I", data, offset)
        flags, vertex_count, fvf, _, triangle_count, batch_count, node_count = header[:7]
        if flags & MESH_STRIP_HEADER:
            raise ModelError(f"Mesh {index} uses the strip layout, which this reader does not know")
        offset += HEADER_WORDS * 4 + RESOURCE_HEADER_SIZE * (2 if triangle_count else 1)
        _need(data, offset, vertex_count * VERTEX_SIZE, f"Mesh {index} vertices")
        vertices = []
        for values in struct.iter_unpack("<6fI2f", data[offset : offset + vertex_count * VERTEX_SIZE]):
            vertices.append(Vertex(values[0:3], values[3:6], values[6], values[7:9]))
        offset += vertex_count * VERTEX_SIZE
        index_bytes = triangle_count * 6
        _need(data, offset, index_bytes, f"Mesh {index} indices")
        indices = list(struct.unpack_from(f"<{triangle_count * 3}H", data, offset))
        if magic == PADDED_MAGIC and triangle_count % 4:
            index_bytes += (4 - triangle_count % 4) * 2
        offset += index_bytes
        records = []
        for _ in range(batch_count + node_count):
            _need(data, offset, RECORD_SIZE, f"Mesh {index} records")
            records.append(struct.unpack_from("<23I", data, offset))
            offset += RECORD_SIZE
        bounds = _floats(header[7:13])
        mesh = Mesh(
            flags=flags,
            fvf=fvf,
            centre=bounds[0:3],
            extent=bounds[3:6],
            vertices=vertices,
            indices=indices,
            batches=[_batch(words) for words in records[:batch_count]],
            nodes=[_node(words) for words in records[batch_count:]],
        )
        _check(mesh, index)
        meshes.append(mesh)
    if offset != len(data):
        raise ModelError(f"{len(data) - offset} bytes follow the last mesh")
    return Model(magic, meshes)


def _check(mesh: Mesh, index: int) -> None:
    if any(value >= len(mesh.vertices) for value in mesh.indices):
        raise ModelError(f"Mesh {index} has an index past its vertices")
    for batch in mesh.batches:
        if (batch.first_triangle + batch.triangle_count) * 3 > len(mesh.indices):
            raise ModelError(f"Mesh {index} has a batch past its indices")
    for node in mesh.nodes:
        if node.first_batch + node.batch_count > len(mesh.batches):
            raise ModelError(f"Mesh {index} has a node past its batches")
        for link in (node.parent, node.child, node.sibling):
            if link >= len(mesh.nodes):
                raise ModelError(f"Mesh {index} has a node link past its nodes")
