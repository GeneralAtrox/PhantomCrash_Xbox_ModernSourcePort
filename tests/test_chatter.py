from pathlib import Path

import pytest

from phantomcrash_research import chatter, game_tables, parts

EXTRACTED_XBE = Path("artifacts/europe/files/default.xbe")


@pytest.mark.skipif(not EXTRACTED_XBE.is_file(), reason="needs the extracted default.xbe")
def test_chatter_reads_the_gates_chances_and_line_tables():
    data = EXTRACTED_XBE.read_bytes()
    lines = chatter.chatter(data, game_tables.load(data))
    assert lines["gates"][:4] == [2, 1, 0, 0]
    assert lines["chances"]["player"][17] == 0.5  # a critical hit, half the time
    assert len(lines["tables"]) == chatter.CHATTER_TABLE_COUNT
    assert all(len(table) == chatter.CHATTER_EVENTS for table in lines["tables"])
    first_kill = lines["scripts"][lines["tables"][0][1]]
    assert [command["op"] for command in first_kill[:4]] == ["sprite", "sprite", "text", "text"]
    assert first_kill[1]["sprite"] == -1  # the robot's face, picked at run time
    assert first_kill[2]["text"] == "{code:fff8}"  # the robot's name


@pytest.mark.skipif(not EXTRACTED_XBE.is_file(), reason="needs the extracted default.xbe")
def test_chips_and_pilots_carry_their_chatter_and_faces():
    data = EXTRACTED_XBE.read_bytes()
    tables = parts.part_tables(data, game_tables.load(data))
    dog = tables["chips"][11]
    assert dog["chatter"] == 0
    assert dog["face"]["sprite"] == 770
    assert dog["face"]["rect"] == [0.0, 128.0, 64.0, 192.0]
    yuki = tables["pilots"][1]
    assert yuki["face"]["sprite"] == 811 and yuki["robot_face"]["sprite"] == 754
