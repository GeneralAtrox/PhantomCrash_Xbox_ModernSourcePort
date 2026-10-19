from pathlib import Path

import pytest

from phantomcrash_research import events, game_tables

EXTRACTED_XBE = Path("artifacts/europe/files/default.xbe")


@pytest.fixture(scope="module")
def europe():
    root = Path(__file__).resolve().parents[1]
    data = (root / EXTRACTED_XBE).read_bytes()
    return data, game_tables.load(data)


@pytest.mark.private_inputs(str(EXTRACTED_XBE))
def test_first_event_is_salsa_and_pepper_before_name_entry(europe):
    data, tables = europe
    story = events.events(data, tables)
    assert len(story) == 110
    assert len({event["script"] for event in story}) == 110
    first = story[0]
    assert (first["scene"], first["from_scene"], first["to_scene"]) == (48, 25, 22)
    assert (first["background"], first["cast"], first["music"]) == (0, [12], "AWAKETO1.WAV")
    window, portrait, text, marker, confirm, wait = first["commands"][:6]
    assert window["image"] == "textures/scroll/talk_tex/0.png"
    assert (window["rect"], window["position"], window["layer"]) == ([0, 152, 408, 304], [107, 111], 5)
    assert portrait["image"] == "textures/scroll/fight_chara00_tex/0.png"
    assert text["text"].startswith("Ｗｅｌｃｏｍｅ　ｔｏ　{#eeee66}Ｓｈｉｎｊｕｋｕ\nＷｉｒｅｄ{/}")
    assert (text["frames_per_character"], text["position"], text["layer"]) == (3, [132, 153], 257)
    assert (marker["text"], marker["animation"]) == ("▼", "fade")
    assert confirm == {"op": "wait_for_confirm"}
    assert wait == {"op": "wait", "frames": 8}
    pepper = next(command for command in first["commands"] if command["op"] == "character")
    assert (pepper["character"], pepper["pose"], pepper["fade"]) == (12, 6, "in")
    assert story[1]["music"] == "4TOWNMAP.WAV" and story[2]["music"] == "keep"
    assert (story[1]["from_scene"], story[1]["to_scene"], story[1]["requires_seen"]) == (22, 19, [])
    assert (story[2]["from_scene"], story[2]["to_scene"], story[2]["requires_seen"]) == (19, 41, [49])


@pytest.mark.private_inputs(str(EXTRACTED_XBE))
def test_talk_backgrounds_and_characters(europe):
    data, tables = europe
    talk = events.talk_tables(data, tables)
    club = talk["backgrounds"][0]
    assert club["back"]["image"] == "textures/scroll/talk_clubwired_tex/0.png"
    assert club["front"]["rect"] == [0, 480, 640, 664]
    assert club["front"]["position"] == [0, 296.0]
    pepper = talk["characters"][12]
    assert pepper["images"][0] == "textures/scroll/mht37_pepper_tex/0.png"
    pose = pepper["poses"][0]
    assert pose["body"] == {"page": 0, "rect": [528, 0, 792, 336]}
    assert pose["mouth"][0] is None
    assert pose["mouth"][2]["offset"] == [82, 49]
    assert pose["blink"]["page"] == 1
    assert talk["characters"][16]["poses"][3] is None
