from pathlib import Path

import pytest

from phantomcrash_research import garage_tables, parts

EXTRACTED_XBE = Path("artifacts/europe/files/default.xbe")


@pytest.mark.skipif(not EXTRACTED_XBE.is_file(), reason="needs the extracted default.xbe")
def test_garage_tables_read_the_robots_lineups_and_price_lists():
    garage = garage_tables.garage_tables(EXTRACTED_XBE.read_bytes())
    assert len(garage["robots"]) == garage_tables.ROBOT_COUNT
    holy = garage["robots"][40]
    assert holy["name"] == "HL-220H"
    assert holy["parts"]["chip"] == {"id": 10, "level": 99}
    assert holy["parts"]["legs"]["id"] == 2
    assert holy["emblem"] == garage_tables.EMBLEM_NONE
    assert garage["robots"][1]["parts"]["body"]["level"] == -parts.LEVEL_MAX
    assert garage["dealer_lineups"][0][0] == {"robot": 40, "picture": 4}
    assert len(garage["dealer_lineups"]) == garage_tables.LINEUP_COUNT
    assert (garage["modules"][0]["name"], garage["modules"][0]["price"]) == ("OCGIIIA", 2000)
    assert garage["modules"][0]["weight"] == 25.0
    assert garage["paints"][56] == {"name": "Rental Color", "index": 56, "price": 9999999}
    assert garage["emblems"][0] == {"name": "Bandage Boy", "index": 0, "price": 10}


@pytest.mark.skipif(not EXTRACTED_XBE.is_file(), reason="needs the extracted default.xbe")
def test_garage_tables_read_wild_machines_stock():
    garage = garage_tables.garage_tables(EXTRACTED_XBE.read_bytes())
    assert len(garage["chip_lineups"]) == garage_tables.CHIP_LINEUP_COUNT
    assert garage["chip_lineups"][0][:2] == [{"id": 11, "level": 0}, {"id": 20, "level": 0}]
    assert len(garage["chip_lineups"][0]) == 8
    assert len(garage["module_lineups"]) == garage_tables.MODULE_LINEUP_COUNT
    assert garage["module_lineups"][0][1] == {"id": 0, "level": 5}
    assert garage["module_lineups"][112][:2] == [{"id": 0, "level": 0}, {"id": 10, "level": 0}]
    assert garage["used_sv_lineups"][0] == [1063, 1065, 1067, 1042, 1047, 90]
    assert garage["used_sv_lineups"][1] == [1068, 1064, 1066, 1060, 1054]
