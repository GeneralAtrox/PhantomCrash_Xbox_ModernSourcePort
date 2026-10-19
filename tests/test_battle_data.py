import struct
from pathlib import Path

import pytest

from phantomcrash_research import battle_data, game_tables

EXTRACTED_XBE = Path("artifacts/europe/files/default.xbe")


def build_enemy_path() -> bytes:
    """Two segments: an entry (kind 0) leading to a loop that leads to itself."""
    table = battle_data.ENEMY_PATH_TABLE
    first = table + 8  # two u32 segment offsets
    header_size = 16  # kind/links, an unused word, the points offset, one link
    second = first + header_size
    points_a = second + header_size
    points_b = points_a + 4 + 2 * 12
    data = bytearray(points_b + 4 + 12)
    data[:4] = battle_data.ENEMY_PATH_MAGIC
    struct.pack_into("<2I", data, table, first, second)
    struct.pack_into("<4BIII", data, first, 0, 0, 0, 1, 0, points_a, second)
    struct.pack_into("<4BIII", data, second, 0, 0, 1, 1, 0, points_b, second)
    struct.pack_into("<I6f", data, points_a, 2, -90.0, 0.0, 88.5, -80.0, 0.0, 79.0)
    struct.pack_into("<I3f", data, points_b, 1, 0.0, 0.0, 0.0)
    return bytes(data)


def test_enemy_path_segments_points_and_links():
    path = battle_data.enemy_path(build_enemy_path())
    assert [segment["kind"] for segment in path["segments"]] == [0, 1]
    assert path["segments"][0]["points"] == [[-90.0, 0.0, 88.5], [-80.0, 0.0, 79.0]]
    assert path["segments"][0]["next"] == [1]
    assert path["segments"][1]["next"] == [1]


def test_enemy_path_needs_its_magic():
    with pytest.raises(ValueError):
        battle_data.enemy_path(b"NOPE" + bytes(32))


@pytest.fixture(scope="module")
def europe():
    root = Path(__file__).resolve().parents[1]
    data = (root / EXTRACTED_XBE).read_bytes()
    return data, game_tables.load(data)


@pytest.mark.private_inputs(str(EXTRACTED_XBE))
def test_lesson_messages_open_with_peppers_window(europe):
    data, tables = europe
    states = battle_data.lesson_messages(data, tables)["states"]
    assert sorted(int(state) for state in states) == list(
        range(battle_data.LESSON_FIRST_STATE, battle_data.LESSON_LAST_STATE + 1)
    )
    intro = states["24"]["first"]
    assert intro[0]["op"] == "sprite" and intro[0]["sprite"] == 1397
    assert intro[1]["op"] == "sprite" and intro[1]["sprite"] == 823
    assert intro[2]["op"] == "text" and intro[2]["colour"] == "#66eeee"
    # Each page holds with the window drawn, then waits for confirm.
    ops = [command["op"] for command in intro[:9]]
    assert ops[4:8] == ["hold", "hold", "text", "wait_for_confirm"]
    assert "second" in states["25"] and "first" not in states["43"]
    # The Standard control type's turn lesson is worded for turning in place.
    assert "standard" in states["26"] and "standard" not in states["24"]
