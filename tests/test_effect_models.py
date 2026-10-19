from pathlib import Path

import pytest

from phantomcrash_research import effect_models

EXTRACTED_XBE = Path("artifacts/europe/files/default.xbe")


@pytest.fixture(scope="module")
def tables():
    if not EXTRACTED_XBE.is_file():
        pytest.skip("the extracted XBE is not present")
    return effect_models.effect_models(EXTRACTED_XBE.read_bytes())


def test_shots_and_impacts(tables):
    out, _, _ = tables
    bullet = out["projectiles"][0]
    assert bullet["model"] == {"model": "models/binlink/194.gltf", "mesh": 2}
    assert bullet["scale"] == 0.5 and not bullet["trail"]
    missile = out["projectiles"][10]
    assert missile["trail"] and missile["exhaust"] == {"offset": 0.3, "scale": [0.5, 0.5, 0.5]}
    assert out["exhaust"] == {"model": "models/binlink/194.gltf", "mesh": 94}
    assert out["impacts"][3] == {"id": 3, "group": 3, "flash_scale": 20.0, "decal": False}
    assert out["impacts"][0]["decal"]
    assert out["decals"]["textures"][0] == "textures/binlink/1566/0.png"


def test_fence_impact_uses_the_thirty_authored_meshes(tables):
    out, models, _ = tables
    fence = out["fence"]
    assert 197 in models
    assert fence["model"] == "models/binlink/197.gltf"
    assert fence["meshes"] == list(range(30))
    assert fence["frame_rate"] == 30 and fence["capacity"] == 20
    assert fence["scale"] == 0.1 and fence["growth"] == 0.05
    assert fence["depth"] == 0.2 and fence["offset"] == -0.5
