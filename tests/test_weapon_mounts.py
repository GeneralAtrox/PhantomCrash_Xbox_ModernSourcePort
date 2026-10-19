import struct

import pytest

from phantomcrash_research.weapon_mounts import muzzle_points


def resource(points):
    data = bytearray(64 + 16 * len(points))
    struct.pack_into("<2I", data, 0, 1, 16)
    struct.pack_into("<I", data, 20, len(points))
    struct.pack_into("<I", data, 32, 48)
    for index, point in enumerate(points):
        struct.pack_into("<3fi", data, 64 + 16 * index, *point)
    return bytes(data)


def test_muzzles_keep_barrel_order_parent_and_original_z_conversion():
    points = muzzle_points(resource([(1, 2, 3, 4), (-1, 5, -6, 7)]))
    assert points == [{"position": [1, 2, -3], "node": 4},
                      {"position": [-1, 5, 6], "node": 7}]


def test_empty_and_truncated_attachment_data():
    assert muzzle_points(resource([])) == []
    with pytest.raises(ValueError):
        muzzle_points(resource([(1, 2, 3, 0)])[:-1])
    with pytest.raises(ValueError):
        muzzle_points(b"\0" * 8)
