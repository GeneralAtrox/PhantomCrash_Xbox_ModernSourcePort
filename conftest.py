"""Use this checkout and declare private integration-test prerequisites explicitly."""

from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).resolve().parent
# An editable installation may belong to another checkout. Always test this one.
for source in (ROOT / "src", ROOT / "tests"):
    sys.path.insert(0, str(source))


def pytest_addoption(parser):
    parser.addoption(
        "--require-private-inputs", action="store_true",
        help="Fail rather than skip when explicitly declared private inputs are absent.",
    )


def pytest_configure(config):
    config.addinivalue_line(
        "markers", "private_inputs(*paths): required private files, relative to repository root"
    )


def pytest_runtest_setup(item):
    missing = []
    for marker in item.iter_markers("private_inputs"):
        for value in marker.args:
            path = Path(value)
            path = path if path.is_absolute() else ROOT / path
            if not path.exists():
                missing.append(str(path))
    if missing:
        reason = "Missing declared private inputs: " + ", ".join(sorted(set(missing)))
        if item.config.getoption("--require-private-inputs"):
            pytest.fail(reason)
        pytest.skip(reason)
