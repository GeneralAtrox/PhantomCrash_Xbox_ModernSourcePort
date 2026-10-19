from pathlib import Path

import pytest

from phantomcrash_research import battle_scenes, finale, game_tables

EXTRACTED_XBE = Path("artifacts/europe/files/default.xbe")


@pytest.mark.skipif(not EXTRACTED_XBE.is_file(), reason="needs the extracted default.xbe")
def test_finale_reads_the_epilogues_phases_scripts_and_call_ins():
    data = EXTRACTED_XBE.read_bytes()
    tables = finale.finale(data, game_tables.load(data))
    phases = tables["phases"]
    assert [phase["state"] for phase in phases] == list(range(8, 19))
    first = phases[0]
    assert (first["wait_frames"], first["fade_in_frames"], first["fade_out_frames"]) == (45, 5, 60)
    assert phases[4]["carrier_route"] == 0 and phases[4]["dissolve_frames"] == 30
    assert phases[2]["script"] == 0 and phases[2]["script_frame"] == 30
    assert phases[10]["script"] == 3 and len(tables["scripts"]) == 4
    assert tables["scripts"][0][1]["sprite"] == 754  # Kudryavka's face
    assert tables["call_ins"][:3] == [31, 73, 104] and len(tables["call_ins"]) == 12
    assert tables["light_points"][1] == [106.7, 28.0, 28.6]
    assert [banner["sprite"] for banner in tables["banners"]] == [3063, 3074]


@pytest.mark.skipif(not EXTRACTED_XBE.is_file(), reason="needs the extracted default.xbe")
def test_battle_scenes_read_the_finales_world_cameras_and_walks():
    scenes = battle_scenes.battle_scenes(EXTRACTED_XBE.read_bytes())
    assert all(str(state) in scenes["states"] for state in range(8, 19))
    assert scenes["states"]["18"]["frames"] == 450
    standing = scenes["states"]["14"]
    assert not standing["relative"]
    assert [channel["values"][0] for channel in standing["walk"]] == pytest.approx(
        [104.2603, 0.0, 17.8765], abs=1e-3
    )
