from pathlib import Path

import pytest

from phantomcrash_research import game_tables

EXTRACTED_XBE = Path("artifacts/europe/files/default.xbe")


def test_rejects_other_executables():
    with pytest.raises(game_tables.GameTablesError):
        game_tables.load(b"XBEH" + b"\0" * 64)


@pytest.mark.private_inputs(str(EXTRACTED_XBE))
def test_europe_tables_used_by_the_boot_flow():
    root = Path(__file__).resolve().parents[1]
    tables = game_tables.load((root / EXTRACTED_XBE).read_bytes())
    assert tables.movies[7:9] == ["movie/dolby.sfd", "movie/phantagram.sfd"]
    assert tables.music[tables.scene_music[0x28]] == "AWAKETO1.WAV"
    assert tables.resources[0x675].path == "scroll/logo_tex.bin"
    assert tables.resources[0x689].path == "scroll/title_tex.bin"
    assert tables.resources[0x690].path == "scroll/scr_common_tex.bin"
    genki = tables.sprites[0x5F2]
    assert (genki.right, genki.bottom, genki.texture_width) == (192.0, 192.0, 256.0)
    resource, index = tables.texture_for_slot(genki.slot)
    assert (resource.path, index) == ("scroll/logo_tex.bin", 1)
    assert tables.texture_for_slot(tables.sprites[0x5F3].slot)[1] == 0
    assert tables.font_characters[:3] == "\u3000\u3001\u3002"
    assert tables.font_characters[140] == "\u266f"
    assert tables.font_metrics[0] == (0, 9)
    garage = next(credit for credit in tables.music_credits if credit.track == 12)
    assert (garage.title, garage.artist) == ("Sanctuary", "Dunkirk")
    volume = tables.volume_millibels
    assert (len(volume), volume[0], volume[128], volume[255]) == (256, -9600, -685, 0)
