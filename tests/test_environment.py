from pathlib import Path

import pytest

from phantomcrash_research import environment

EXTRACTED_XBE = Path("artifacts/europe/files/default.xbe")
TRAINING_GROUND = 6


@pytest.mark.private_inputs(str(EXTRACTED_XBE))
def test_training_ground_environment():
    root = Path(__file__).resolve().parents[1]
    stages = environment.stage_environments((root / EXTRACTED_XBE).read_bytes())
    assert len(stages) == environment.STAGE_COUNT
    assert all(stage["presets"] for stage in stages)
    training = stages[TRAINING_GROUND]
    assert training["sun_direction"] == [10.52, -16.43, 4.42]
    first = training["presets"][0]
    assert first["name"] == "Fix1"
    assert first["fog"] == {"mode": "linear", "colour": [140, 125, 112], "density": 0.0, "start": 0.0, "end": 350.0}
    assert first["light"] == {"colour": [255, 255, 255], "diffuse": 1.04, "ambient": 0.23}
    # Some presets use exponential fog, which has a density and no range.
    modes = {preset["fog"]["mode"] for stage in stages for preset in stage["presets"]}
    assert {"linear", "exp", "exp2"} <= modes
