import json
from pathlib import Path

import pytest

from phantomcrash_research.extract import extract
from phantomcrash_research.cli import DEFAULT_DISC
from fixtures import build_xbe, build_xiso


def test_extracts_files_and_metadata(tmp_path):
    disc = tmp_path / "game.iso"
    disc.write_bytes(build_xiso(xbe_data=build_xbe()))
    output = tmp_path / "out"
    result = extract(disc, output)
    assert (output / "files" / "Media" / "a.bin").read_bytes() == b"hello world"
    metadata = json.loads((output / "metadata" / "disc.json").read_text())
    assert metadata["file_count"] == 2
    assert {record["path"] for record in metadata["files"]} == {"/default.xbe", "/Media/a.bin"}
    header = json.loads((output / "metadata" / "xbe.json").read_text())
    assert header["title_code"] == "PL-001"
    assert header["sha256"] == result["xbe"]["sha256"]


def test_refuses_non_empty_output(tmp_path):
    output = tmp_path / "out"
    output.mkdir()
    (output / "keep.txt").write_text("x")
    with pytest.raises(FileExistsError):
        extract(tmp_path / "missing.iso", output)


@pytest.mark.private_inputs(str(DEFAULT_DISC))
def test_real_disc_identity(tmp_path):
    from phantomcrash_research.xdvdfs import XdvdfsImage
    from phantomcrash_research import xbe

    with XdvdfsImage(DEFAULT_DISC) as image:
        files = image.files()
        header = xbe.parse(image.read(image.find("default.xbe")))
    assert len(files) == 309
    assert header.title_code == "PL-001"
    assert header.xdk_build == 4721
    assert header.debug_path == r"K:\Release\citydiver.exe"
