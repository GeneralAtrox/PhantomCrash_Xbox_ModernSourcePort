from pathlib import Path

import pytest

from phantomcrash_research import game_tables, parts

EXTRACTED_XBE = Path("artifacts/europe/files/default.xbe")


def curve(base, mid, top, knee, low_scale=-1):
    return {"base": base, "mid": mid, "top": top, "knee": knee, "low_scale": low_scale}


def test_level_value_climbs_to_the_knee_then_to_the_top():
    dog = curve(200, 560, 700, 60)
    assert parts.level_value(dog, 0) == 200
    assert parts.level_value(dog, 30) == pytest.approx(480)
    assert parts.level_value(dog, 60) == pytest.approx(760)
    assert parts.level_value(dog, 99) == pytest.approx(900)


def test_level_value_knees_at_the_ends_and_negative_levels():
    assert parts.level_value(curve(10, 0, 99, 0), 33) == pytest.approx(43)
    assert parts.level_value(curve(10, 99, 0, 99), 33) == pytest.approx(43)
    # A negative level runs the curve by its size, scaled by low_scale.
    pistol = curve(267, -133, -9732, 98)
    assert parts.level_value(pistol, -49) == pytest.approx(267 + 66.5)
    # Never below zero.
    assert parts.level_value(pistol, 99) == 0


@pytest.mark.skipif(not EXTRACTED_XBE.is_file(), reason="needs the extracted default.xbe")
def test_part_tables_read_the_chips_and_weapons():
    data = EXTRACTED_XBE.read_bytes()
    tables = parts.part_tables(data, game_tables.load(data))
    assert len(tables["chips"]) == parts.CHIP_COUNT
    dog = tables["chips"][11]
    assert dog["name"] == "Dog I"
    assert (dog["inner_shape"], dog["track"], dog["lock_frames"]) == (0, 3, 22)
    pistol = tables["arm_weapons"][0]
    assert (pistol["name"], pistol["kind"]) == ("Pistol/HM15", 0)
    assert parts.level_value(pistol["ammo"], 0) == 267
    aspide = tables["shoulder_weapons"][2]
    assert aspide["kind"] == 1 and aspide["homing_degrees"]["base"] > 0


@pytest.mark.skipif(not EXTRACTED_XBE.is_file(), reason="needs the extracted default.xbe")
def test_part_tables_read_the_weapon_stats_armour_and_pilots():
    data = EXTRACTED_XBE.read_bytes()
    tables = parts.part_tables(data, game_tables.load(data))
    pistol = tables["arm_weapons"][0]
    assert (pistol["fire_frames"], pistol["life_frames"], pistol["start_speed"]) == (18, 30, 3.7)
    assert parts.level_value(pistol["damage"], 0) == 450
    assert parts.level_value(pistol["price"], 0) == 1500
    slingshot = tables["shoulder_weapons"][0]
    assert (slingshot["flight"], slingshot["launch_degrees"], slingshot["blast_start_radius"]) == (
        1,
        30,
        1,
    )
    assert parts.level_value(slingshot["blast_radius"], 0) == 20
    assert parts.level_value(slingshot["blast_damage"], 0) == 120
    body = tables["bodies"][0]
    assert (body["name"], parts.level_value(body["armour"], 0)) == ("HLBody", 15000)
    drone_legs = tables["legs"][23]
    assert drone_legs["model"] == 20 and drone_legs["armour_floor"]["computer"] == 600
    yuki = tables["pilots"][1]
    assert (yuki["tag"], yuki["robot_name"]) == ("Yuki", "Kudryavka")
    assert tables["pilots"][0]["tag"] is None


@pytest.mark.skipif(not EXTRACTED_XBE.is_file(), reason="needs the extracted default.xbe")
def test_part_tables_read_the_combat_stats_and_weights():
    data = EXTRACTED_XBE.read_bytes()
    tables = parts.part_tables(data, game_tables.load(data))
    dog = tables["chips"][11]
    assert parts.level_value(dog["critical_percent"], 0) == pytest.approx(1.0)
    assert parts.level_value(dog["knockdown_resistance"], 0) == pytest.approx(3.0)
    legs = tables["legs"][0]
    assert parts.level_value(legs["knockback_resistance"], 0) == pytest.approx(70.0)
    assert parts.level_value(legs["capacity"], 0) == pytest.approx(1060.0)
    assert parts.level_value(legs["turn"], 0) == pytest.approx(2.35)
    assert parts.level_value(legs["jump"], 0) == pytest.approx(19.14)
    assert parts.level_value(legs["dodge"], 0) == pytest.approx(0.55)
    assert parts.level_value(legs["air_dodge"], 0) == pytest.approx(0.3)
    assert legs["stop_keep"] == pytest.approx(0.875)
    assert tables["legs"][4]["stop_keep"] == pytest.approx(0.96)
    assert parts.level_value(tables["arm_weapons"][0]["weight"], 0) == pytest.approx(40.0)
