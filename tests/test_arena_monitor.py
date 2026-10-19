from pathlib import Path

import pytest

from phantomcrash_research import arena_monitor, game_tables, model
from phantomcrash_research.robot_convert import _Packs

XBE = Path("artifacts/europe/files/default.xbe")


@pytest.mark.skipif(not XBE.is_file(), reason="needs the extracted default.xbe")
def test_ship_routes_models_and_monitor_panels_match_the_disc():
    executable = XBE.read_bytes()
    table = arena_monitor.ship_table(executable)
    assert [len(table["routes"][str(s)]) for s in range(8)] == [55, 55, 59, 59, 59, 59, 55, 55]
    assert table["routes"]["0"][0][0] == pytest.approx([-113.079, 293.192, -70.661])
    assert table["segment_per_second"] == pytest.approx(0.09)
    packs = _Packs(XBE.parent / "Media", game_tables.load(executable))
    for i, look in enumerate(table["models"]):
        parsed = model.parse(packs.read(210 + i))
        for screen in look["screens"]:
            panel = parsed.meshes[screen]
            assert len(panel.vertices) == 4
            assert len(panel.indices) == 6
            assert any(screen in h["meshes"] for h in look["hinges"])
        assert all(0 <= mesh < len(parsed.meshes) for h in look["hinges"] for mesh in h["meshes"])


@pytest.mark.skipif(not XBE.is_file(), reason="needs the extracted default.xbe")
def test_ship_route_segments_join_without_a_teleport():
    routes = arena_monitor.ship_table(XBE.read_bytes())["routes"]
    for route in routes.values():
        for current, following in zip(route, route[1:] + route[:1]):
            assert current[-1] == pytest.approx(following[0], abs=0.002)


@pytest.mark.skipif(not XBE.is_file(), reason="needs the extracted default.xbe")
def test_conversion_replaces_ship_atlas_uvs_with_the_complete_battle_picture(monkeypatch):
    executable = XBE.read_bytes()
    packs = _Packs(XBE.parent / "Media", game_tables.load(executable))
    converted = {}
    monkeypatch.setattr(arena_monitor.png, "write_rgba", lambda *args: None)
    monkeypatch.setattr(arena_monitor.gltf, "write",
                        lambda parsed, path, uris: converted.setdefault(path.stem, parsed))
    arena_monitor.convert_ship(executable, packs.read, Path("unused"), lambda *args: None)
    for resource, screens in (("210", [4, 8]), ("211", [8, 12])):
        for screen in screens:
            assert [v.uv for v in converted[resource].meshes[screen].vertices] == [
                (0, 0), (0, 1), (1, 0), (1, 1)]
