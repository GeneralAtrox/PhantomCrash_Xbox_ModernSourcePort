"""Stage collision (``Media/model/choco_col.bin`` and binlink resources).

The stage table lists each stage's collision resource at ``STAGE_COLLISION``
(0x2212cc); ``0x06e040`` loads it when a battle starts and ``0x052b90``
fixes up its pointers. A file holds two triangle sets, each with a uniform
grid over it:

- *walls*: a 3D grid (``cells`` x, y, z) searched by the sphere tests
  (``0x052c1f``: faces ``0x054c45``, then edges, then corners);
- *floor*: a 2D grid (x, z) searched for the ground height under a point
  (``0x053337``, -1000 where there is none).

Layout, little-endian (offsets in bytes):

====== ===================================================================
0x0000 u32 cells x, y, z; f32 cell size x, y, z; u32 wall triangles, wall
       index count
0x0020 u32 wall cells [x][y][z]: first entry of the cell's index list
0x4020 two run-time pointers (walls, wall indices)
0x4028 u32 floor triangles, floor index count
0x4030 u32 floor cells [x][z] (32 x 32)
0x5030 two run-time pointers (floor, floor indices)
0x5038 f32 grid centre x, y, z
0x5044 wall triangles, wall indices, floor triangles, floor indices
====== ===================================================================

A triangle is 0x34 bytes: three corners, the unit normal (the corners'
winding, ``cross(b - a, c - a)``) and a u32 surface; ``surface % 1000``
indexes the result flags at ``SURFACE_FLAGS`` (0x2057d8). Index lists end
with -1. A cell lists every triangle within about 2.4 units of it, so a
sphere tested against its centre's cell finds nearby triangles.

The grid layout is fixed by the loader: 32 x 4 x 32 wall cells and 32 x 32
floor cells. Surface meanings are researcher-assigned: 9 is a plain wall
(result 0x4000) and 8 the arena's fence (0x2000, which also starts a
30-frame contact effect, ``0x07027b``).
"""

from __future__ import annotations

import math
import struct

STAGE_MODELS = 0x2212AC  # u32 model resource per stage
STAGE_COLLISION = 0x2212CC  # u32 collision resource per stage
STAGE_COUNT = 8

GRID_CELLS = (32, 4, 32)
HEADER = struct.Struct("<3I3f2I")
WALL_CELLS = 0x20
FLOOR_COUNTS = 0x4028
FLOOR_CELLS = 0x4030
GRID_CENTRE = 0x5038
DATA = 0x5044
TRIANGLE = struct.Struct("<12fI")
END_OF_LIST = -1
# Padding around a triangle when listing it in cells (the shipped file pads
# by about 2.4, just over the largest robot radius).
CELL_MARGIN = 2.5
DECIMALS = 4


def _triangles(data: bytes, offset: int, count: int) -> list[dict]:
    triangles = []
    for index in range(count):
        values = TRIANGLE.unpack_from(data, offset + index * TRIANGLE.size)
        points = [[round(v, DECIMALS) for v in values[i : i + 3]] for i in (0, 3, 6)]
        triangles.append({"points": points, "surface": values[12]})
    return triangles


def parse(data: bytes) -> dict:
    """``{"grid": {...}, "walls": [...], "floor": [...]}``; each triangle is
    ``{"points": [[x, y, z] * 3], "surface": n}`` in game space."""
    if len(data) < DATA:
        raise ValueError("collision file is too short")
    nx, ny, nz, sx, sy, sz, walls, wall_indices = HEADER.unpack_from(data, 0)
    if (nx, ny, nz) != GRID_CELLS:
        raise ValueError(f"unexpected collision grid {nx} x {ny} x {nz}")
    floors, floor_indices = struct.unpack_from("<2I", data, FLOOR_COUNTS)
    wall_offset = DATA
    floor_offset = wall_offset + walls * TRIANGLE.size + wall_indices * 4
    end = floor_offset + floors * TRIANGLE.size + floor_indices * 4
    if end != len(data):
        raise ValueError(f"collision file size {len(data)} does not match its counts ({end})")
    centre = struct.unpack_from("<3f", data, GRID_CENTRE)
    return {
        "grid": {
            "cells": [nx, ny, nz],
            "cell_size": [round(v, DECIMALS) for v in (sx, sy, sz)],
            "centre": [round(v, DECIMALS) for v in centre],
        },
        "walls": _triangles(data, wall_offset, walls),
        "floor": _triangles(data, floor_offset, floors),
    }


def _normal(points: list[list[float]]) -> tuple[float, float, float]:
    a, b, c = points
    e1 = [b[i] - a[i] for i in range(3)]
    e2 = [c[i] - a[i] for i in range(3)]
    n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
    length = math.sqrt(sum(v * v for v in n))
    return (0.0, 1.0, 0.0) if length == 0.0 else tuple(v / length for v in n)


def _cell_range(low: float, high: float, axis: int, grid: dict) -> range:
    size = grid["cell_size"][axis]
    count = grid["cells"][axis]
    centre = grid["centre"][axis]
    first = math.floor((low - centre) / size + count / 2)
    last = math.floor((high - centre) / size + count / 2)
    return range(max(first, 0), min(last, count - 1) + 1)


def _cell_lists(triangles: list[dict], grid: dict, axes: tuple[int, ...]) -> dict[tuple, list[int]]:
    lists: dict[tuple, list[int]] = {}
    for index, triangle in enumerate(triangles):
        ranges = []
        for axis in axes:
            values = [p[axis] for p in triangle["points"]]
            margin = 0.0 if axis == 1 else CELL_MARGIN
            ranges.append(_cell_range(min(values) - margin, max(values) + margin, axis, grid))
        for cell in _product(ranges):
            lists.setdefault(cell, []).append(index)
    return lists


def _product(ranges: list[range]) -> list[tuple]:
    cells: list[tuple] = [()]
    for r in ranges:
        cells = [cell + (value,) for cell in cells for value in r]
    return cells


def _index_table(lists: dict[tuple, list[int]], cells: list[tuple]) -> tuple[list[int], list[int]]:
    """Each cell's first index, and the -1-terminated lists; empty cells share
    the leading terminator."""
    indices = [END_OF_LIST]
    starts = []
    for cell in cells:
        entries = lists.get(cell)
        if not entries:
            starts.append(0)
            continue
        starts.append(len(indices))
        indices.extend(entries)
        indices.append(END_OF_LIST)
    return starts, indices


def _pack_triangles(triangles: list[dict]) -> bytes:
    out = bytearray()
    for triangle in triangles:
        flat = [v for p in triangle["points"] for v in p]
        out += TRIANGLE.pack(*flat, *_normal(triangle["points"]), triangle["surface"])
    return bytes(out)


def write(collision: dict) -> bytes:
    """The game's collision file for ``collision`` (as :func:`parse` returns)."""
    grid = collision["grid"]
    nx, ny, nz = grid["cells"]
    if (nx, ny, nz) != GRID_CELLS:
        raise ValueError(f"the game's loader needs a {GRID_CELLS} grid")
    walls, floor = collision["walls"], collision["floor"]
    wall_cells = [(x, y, z) for x in range(nx) for y in range(ny) for z in range(nz)]
    wall_starts, wall_indices = _index_table(_cell_lists(walls, grid, (0, 1, 2)), wall_cells)
    floor_cells = [(x, z) for x in range(nx) for z in range(nz)]
    floor_starts, floor_indices = _index_table(_cell_lists(floor, grid, (0, 2)), floor_cells)

    header = bytearray(DATA)
    HEADER.pack_into(header, 0, nx, ny, nz, *grid["cell_size"], len(walls), len(wall_indices))
    struct.pack_into(f"<{len(wall_starts)}I", header, WALL_CELLS, *wall_starts)
    struct.pack_into("<2I", header, FLOOR_COUNTS, len(floor), len(floor_indices))
    struct.pack_into(f"<{len(floor_starts)}I", header, FLOOR_CELLS, *floor_starts)
    struct.pack_into("<3f", header, GRID_CENTRE, *grid["centre"])
    return b"".join(
        (
            bytes(header),
            _pack_triangles(walls),
            struct.pack(f"<{len(wall_indices)}i", *wall_indices),
            _pack_triangles(floor),
            struct.pack(f"<{len(floor_indices)}i", *floor_indices),
        )
    )
