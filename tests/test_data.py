"""The default data files parse, and every file and rectangle they name exists."""

import json
from pathlib import Path
import re
import struct

import pytest

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
ASSETS = ROOT / "artifacts" / "europe" / "assets"
MEDIA = ROOT / "artifacts" / "europe" / "files" / "Media"
SEARCH_ROOTS = (ASSETS, MEDIA)


def data_files():
    return sorted(DATA.rglob("*.json"))


def references(node):
    """Yield (file, rect or None) for every ``image``/``file``/``files`` value."""
    if isinstance(node, dict):
        for key in ("image", "file"):
            if isinstance(node.get(key), str) and node[key]:
                yield node[key], node.get("rect") if key == "image" else None
        for value in node.get("files", []) if isinstance(node.get("files"), list) else []:
            yield value, None
        for value in node.values():
            yield from references(value)
    elif isinstance(node, list):
        for value in node:
            yield from references(value)


def png_size(path: Path) -> tuple[int, int]:
    return struct.unpack(">II", path.read_bytes()[16:24])


def resolve(relative: str) -> Path | None:
    for root in SEARCH_ROOTS:
        candidate = root / relative
        if candidate.is_file():
            return candidate
        # The disc's names are case-insensitive; the extraction keeps disc case.
        folder = candidate.parent
        if folder.is_dir():
            for entry in folder.iterdir():
                if entry.name.lower() == candidate.name.lower():
                    return entry
    return None


@pytest.mark.private_inputs(str(ASSETS / "manifest.json"), str(MEDIA))
@pytest.mark.parametrize("path", data_files(), ids=lambda path: path.relative_to(DATA).as_posix())
def test_data_references_resolve(path):
    document = json.loads(path.read_text(encoding="utf-8"))
    for relative, rect in references(document):
        found = resolve(relative)
        assert found is not None, f"{path.name}: {relative} not found"
        if rect is not None:
            width, height = png_size(found)
            left, top, right, bottom = rect
            assert 0 <= left < right <= width and 0 <= top < bottom <= height, (relative, rect)


@pytest.mark.private_inputs(str(ASSETS / "tables" / "text.json"))
def test_game_text_references_resolve():
    """Every {xbe:0x...} in the data names a string the conversion read."""
    table = json.loads((ASSETS / "tables" / "text.json").read_text(encoding="utf-8"))["strings"]
    known = {int(key, 16) for key in table}
    for path in data_files():
        text = path.read_text(encoding="utf-8")
        for address in re.findall(r"\{xbe:0x([0-9A-Fa-f]+)\}", text):
            assert int(address, 16) in known, f"{path.name}: {{xbe:0x{address}}}"
