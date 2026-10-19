import pytest

from pathlib import Path

from phantomcrash_research import game_tables, stage_objects

from phantomcrash_research.stage_convert import colour_meshes


def test_city_slots_use_resource_base_and_exclude_static_volumes():
    # Shinjuku's object list begins at 91, but resource 186 begins at 31.
    meshes, sky = colour_meshes([(91, 9), (134, 7), (143, 1), (145, 2), (185, 8)], 31, 155)
    assert meshes == {60, 112, 114, 154}
    assert sky == ["mesh112_node0"]


@pytest.mark.parametrize("slot", [30, 186])
def test_stage_object_outside_resource_is_rejected(slot):
    with pytest.raises(ValueError, match="outside"):
        colour_meshes([(slot, 2)], 31, 155)


@pytest.mark.skipif(not Path("artifacts/europe/files/default.xbe").is_file(),
                    reason="needs the extracted default.xbe")
def test_city_scenery_slots_resolve_to_their_own_textured_effect_models():
    data = Path("artifacts/europe/files/default.xbe").read_bytes()
    image = game_tables._Image(data)
    resources = {r.id: r for r in game_tables.load(data).resources}
    for stage, count in enumerate((26, 26, 56, 56, 29, 29)):
        objects = stage_objects.records(image, stage)
        resource = resources[198 + stage]
        assert len(objects) == count
        assert resource.texture_resource == 1575 + stage
        for entry in objects:
            assert 0 <= entry["slot"] - resource.first_mesh_slot < resource.mesh_count
            if entry["wreck_slot"] >= 0:
                assert 0 <= entry["wreck_slot"] - resource.first_mesh_slot < resource.mesh_count
                assert entry["wreck_solid"]
            assert len(entry["collision"]) == 12
            assert all(len(triangle) == 9 for triangle in entry["collision"])
            if entry["kind"] == 0:
                assert entry["break_effect"] == 14
                assert entry["break_sound"] == "sound/sfx/battle/16.wav"
                assert entry["wreck_solid"]
                assert entry["wreck_break_effect"] == -1
            elif entry["kind"] == 1:
                assert entry["break_effect"] == 13
                assert entry["wreck_break_effect"] == 15
                assert entry["wreck_hit_delay"] == 6.0
        assert sum(entry["kind"] == 6 for entry in objects) == (6 if stage < 2 else 4)
    first = stage_objects.records(image, 0)[0]
    assert (first["slot"], first["wreck_slot"]) == (1066, 1067)
    assert first["position"] == pytest.approx([147.2109, -1.453093, -65.143509])
    assert first["collision"][0] == pytest.approx([-1.52, 0, 7.16, -1.52, 4.43, 7.16,
                                                   -1.52, 0, -7.21])
