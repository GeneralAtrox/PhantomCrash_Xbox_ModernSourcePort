import json
import struct

import pytest

from phantomcrash_research import convert, png
from phantomcrash_research.xbox_texture import A8R8G8B8
from test_audio import adpcm_wav
from test_textures import build_pack, format_word


def test_textures_and_banks_convert_to_open_formats(tmp_path):
    media = tmp_path / "Media"
    (media / "scroll").mkdir(parents=True)
    (media / "sound").mkdir()
    pack = build_pack([(format_word(A8R8G8B8, 2, 2), bytes(range(16)))])
    (media / "scroll" / "demo_tex.bin").write_bytes(pack)
    wav = adpcm_wav(struct.pack("<hBB", 0, 0, 0) + b"\0" * 32)
    bank = b"SE BIN  " + struct.pack("<I4I", 1, 0x10000, 0, 0, 28) + wav
    (media / "sound" / "sys.bin").write_bytes(bank)
    (media / "sound" / "music.wav").write_bytes(wav)  # not a bank: skipped

    output = tmp_path / "assets"
    textures = convert.convert_textures(media, output, lambda _message: None)
    effects = convert.convert_sound_banks(media, output, lambda _message: None)

    assert textures == [
        {
            "pack": "scroll/demo_tex.bin",
            "textures": [
                {"index": 0, "format": "A8R8G8B8", "width": 2, "height": 2, "levels": 1}
            ],
        }
    ]
    width, height, _ = png.decode_rgba((output / "textures/scroll/demo_tex/0.png").read_bytes())
    assert (width, height) == (2, 2)
    assert effects[0]["bank"] == "sound/sys.bin"
    assert (output / "sound/sfx/sys/00.wav").read_bytes()[:4] == b"RIFF"
    assert json.dumps(effects)  # records are plain JSON
    limits = json.loads((output / "tables/sound_effects.json").read_text())
    assert limits == {"effects": {"sound/sfx/sys/00.wav": {"voices": 1}}}


def test_replace_requires_an_earlier_conversion(tmp_path):
    output = tmp_path / "assets"
    output.mkdir()
    (output / "user-file.txt").write_text("keep")
    with pytest.raises(FileExistsError):
        convert._prepare_output(output, replace=False)
    with pytest.raises(FileExistsError):
        convert._prepare_output(output, replace=True)
    assert (output / "user-file.txt").exists()

    (output / convert.MANIFEST).write_text("{}")
    convert._prepare_output(output, replace=True)
    assert list(output.iterdir()) == []


def test_convert_requires_an_extraction(tmp_path):
    with pytest.raises(FileNotFoundError):
        convert.convert(tmp_path, tmp_path / "assets")
