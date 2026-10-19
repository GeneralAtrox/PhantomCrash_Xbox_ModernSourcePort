import struct
from pathlib import Path

import pytest

from phantomcrash_research import collision

TRAINING_COLLISION = Path("artifacts/europe/files/Media/model/choco_col.bin")


def wall(x: float, surface: int = 9) -> dict:
    """A vertical wall across z at `x`, facing +x."""
    return {"points": [[x, 0.0, -5.0], [x, 10.0, -5.0], [x, 0.0, 5.0]], "surface": surface}


def flat_floor() -> dict:
    return {"points": [[-20.0, 0.0, -20.0], [-20.0, 0.0, 20.0], [20.0, 0.0, -20.0]], "surface": 9}


def sample() -> dict:
    return {
        "grid": {"cells": [32, 4, 32], "cell_size": [2.0, 5.0, 2.0], "centre": [0.0, 10.0, 0.0]},
        "walls": [wall(3.0), wall(-7.5, 8)],
        "floor": [flat_floor()],
    }


def cell_list(data: bytes, start: int, indices: int) -> list[int]:
    entries = []
    while (value := struct.unpack_from("<i", data, indices + 4 * start)[0]) != -1:
        entries.append(value)
        start += 1
    return entries


def test_written_collision_reads_back():
    data = collision.write(sample())
    parsed = collision.parse(data)
    assert parsed["walls"] == sample()["walls"]
    assert parsed["floor"] == sample()["floor"]
    assert parsed["grid"] == sample()["grid"]


def test_cells_list_the_triangles_near_them_and_normals_follow_the_winding():
    data = collision.write(sample())
    walls = struct.unpack_from("<I", data, 0x18)[0]
    indices = collision.DATA + walls * collision.TRIANGLE.size
    # x = 3 sits in cell 17 (cells of 2 around 0); a sphere in cell 18 still finds it.
    for x_cell in (17, 18):
        start = struct.unpack_from("<I", data, collision.WALL_CELLS + 4 * (x_cell * 128 + 2 * 32 + 16))[0]
        assert 0 in cell_list(data, start, indices)
    far = struct.unpack_from("<I", data, collision.WALL_CELLS + 4 * (31 * 128 + 2 * 32 + 16))[0]
    assert cell_list(data, far, indices) == []
    normal = struct.unpack_from("<3f", data, collision.DATA + 36)
    assert normal == pytest.approx((1.0, 0.0, 0.0))


def test_collision_rejects_a_truncated_file():
    with pytest.raises(ValueError):
        collision.parse(collision.write(sample())[:-4])


@pytest.mark.private_inputs(str(TRAINING_COLLISION))
def test_training_ground_collision():
    root = Path(__file__).resolve().parents[1]
    data = (root / TRAINING_COLLISION).read_bytes()
    parsed = collision.parse(data)
    assert len(parsed["walls"]) == 225 and len(parsed["floor"]) == 106
    assert parsed["grid"]["centre"] == [0.0, 55.0, 0.0]
    assert {t["surface"] for t in parsed["walls"]} == {8, 9}
    assert collision.parse(collision.write(parsed))["walls"] == parsed["walls"]
