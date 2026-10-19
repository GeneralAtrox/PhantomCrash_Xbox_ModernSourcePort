"""Checks for calendar composition, source tables and song metadata."""
import copy
import struct

import pytest

from phantomcrash_research.slai.music import messages, stream_header
from phantomcrash_research.slai.progression import apply_deliveries, calendar_load, delivery_days, rotating_deliveries
from phantomcrash_research.slai.stats import _curve


def calendar():
    garage = {"module_lineups": [[{"id": 0, "level": 0}]]}
    town = {"calendar": [{"modules": [0] * 7, "venues": [0]} for _ in range(60)],
            "contests": [{"class": 1}]}
    return town, garage


def test_calendar_distinguishes_first_arrivals_from_repeated_stock():
    town, garage = calendar()
    rows = calendar_load(town, garage)
    assert rows[0]["first_seen"] == 7
    assert all(r["first_seen"] == 0 and r["changed"] == 0 for r in rows[1:])
    assert all(r["active_venues"] == 1 for r in rows)


def test_deliveries_preserve_base_and_do_not_mutate_shared_lineups():
    town, garage = calendar()
    before = copy.deepcopy((town, garage))
    deliveries = [{"category": 1, "id": 100, "days": [2, 2, 3]},
                  {"category": 1, "id": 101, "level": 50, "days": [3]}]
    updated, stock = apply_deliveries(town, garage, deliveries)
    assert (town, garage) == before
    assert len(stock["module_lineups"]) == 3
    assert updated["calendar"][0]["modules"][1] == 0
    assert stock["module_lineups"][1] == [{"id": 0, "level": 0}, {"id": 100, "level": 0}]
    assert stock["module_lineups"][2][-1] == {"id": 101, "level": 50}
    assert updated["calendar"][3]["modules"][1] == 0


@pytest.mark.parametrize("field,value", [("category", 7), ("id", -1), ("level", 100), ("days", [0])])
def test_invalid_deliveries_are_rejected(field, value):
    town, garage = calendar()
    row = {"category": 1, "id": 100, "days": [1]}
    row[field] = value
    with pytest.raises(ValueError):
        apply_deliveries(town, garage, [row])


def test_arrivals_spread_across_cycle_and_favour_quieter_days():
    town, garage = calendar()
    rows = calendar_load(town, garage)
    for row in rows:
        row["first_seen"] = 100
    for day in (3, 9, 15, 21, 27, 33, 39, 45, 51, 57):
        rows[day - 1]["first_seen"] = 0
    assert delivery_days(rows, 10) == [3, 9, 15, 21, 27, 33, 39, 45, 51, 57]


def test_rotations_cover_all_days_without_selling_later_parts_early():
    town, garage = calendar()
    parts = [{"category": 1, "id": i + 100} for i in range(15)]
    deliveries = rotating_deliveries(calendar_load(town, garage), parts)
    assert {d for part in deliveries for d in part["days"]} == set(range(1, 61))
    assert sum(1 in part["days"] for part in deliveries) == 1
    assert all(min(part["days"]) == part["first_delivery"] for part in deliveries)
    assert len(deliveries[-1]["days"]) <= 6


def test_msg_decodes_disc_ascii_without_treating_font_indices_as_unicode():
    data = struct.pack("<2I5H", 1, 8, ord("R") - 32, 0xB73, ord("X") - 32, 0xFFFF, 0)
    assert messages(data) == ["RX"]


def test_ild_header_reports_rate_duration_and_loop_samples():
    header = bytearray(128)
    header[:4] = b"ILD\0"
    struct.pack_into("<3I", header, 4, 2, 2048, 3200)
    struct.pack_into("<I", header, 32, 16)
    struct.pack_into("<3I", header, 40, 44100, 160, 1600)
    assert stream_header(header, 5248) == {"channels": 2, "sample_rate": 44100,
        "frames": 2800, "loop_start": 280, "loop_end": 2800}
    with pytest.raises(ValueError):
        stream_header(header, 5247)


def test_source_curves_preserve_level_100_and_reject_invalid_records():
    data = struct.pack("<3i2b", 27000, 10000, 20000, 100, -1)
    assert _curve(data, 0, "i")["knee"] == 100
    with pytest.raises(ValueError):
        _curve(struct.pack("<3i2b", 1, 2, 3, 101, -1), 0, "i")
