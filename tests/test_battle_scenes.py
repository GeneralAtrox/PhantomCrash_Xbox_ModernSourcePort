from pathlib import Path

import pytest

from phantomcrash_research import battle_scenes

EXTRACTED_XBE = Path("artifacts/europe/files/default.xbe")


@pytest.mark.skipif(not EXTRACTED_XBE.is_file(), reason="needs the extracted default.xbe")
def test_battle_scenes_read_the_entrance_the_exit_and_the_gates():
    scenes = battle_scenes.battle_scenes(EXTRACTED_XBE.read_bytes())
    entrance = scenes["states"]["1"]
    assert entrance["frames"] == 540 and entrance["relative"]
    assert set(entrance["camera"]) == set(battle_scenes.CAMERA_CHANNELS)
    walk = entrance["walk"][2]
    assert walk["kind"] == "smooth" and walk["keys"] == [119, 359, 539]
    assert walk["values"] == [0.0, 0.0, -15.0, -15.0]  # 15 units forward (-z)
    leave = scenes["states"]["6"]
    assert leave["frames"] == 240 and leave["walk"] is None
    training = scenes["gates"]["6"]
    assert len(training) == 4
    assert training[0] == {"position": [-99.7172, 0.3, 99.7172], "heading": -0.8}


@pytest.mark.skipif(not EXTRACTED_XBE.is_file(), reason="needs the extracted default.xbe")
def test_battle_scenes_read_the_training_grounds_gate_doors():
    doors = battle_scenes.battle_scenes(EXTRACTED_XBE.read_bytes())["doors"]["6"]
    assert [door["gate"] for door in doors] == [0, 1, 2, 3]
    first = doors[0]
    assert first["position"] == [-99.7172, 0.3, 99.7172] and first["rotation"] == [0.0, 0.785, 0.0]
    closing, opening = first["leaves"]
    assert (closing["mesh"], closing["swing"]) == (5, -1)  # effect model meshes 4 and 5
    assert (opening["mesh"], opening["swing"]) == (4, 1)
    assert len(opening["collision"]) == 12
    assert opening["collision"][0] == [4.68, 10.0, -3.78, 4.68, 0.0, -3.78, 4.68, 10.0, 0.33]


@pytest.mark.skipif(not EXTRACTED_XBE.is_file(), reason="needs the extracted default.xbe")
def test_city_doors_use_city_effect_meshes_and_gate_slots():
    doors = battle_scenes.battle_scenes(EXTRACTED_XBE.read_bytes())["doors"]
    for stage in range(6):
        assert [door["gate"] for door in doors[str(stage)]] == list(range(3 if stage < 2 else 2))
        for door in doors[str(stage)]:
            assert [leaf["mesh"] for leaf in door["leaves"]] == ([26, 25] if stage < 2 else [15, 14])
            assert all(len(leaf["collision"]) == 12 for leaf in door["leaves"])
