"""Export the converted game tables as spreadsheets: one CSV per kind of
record (bodies, legs, arm and shoulder weapons, AI chips, option modules,
paints, emblems, the preset robots, the pilots, the opponents and the
contests), every field of every record.

A stat that grows with its part's level (a level curve, ``LevelCurve`` in
``port/include/phantomcrash/robot_parts.hpp``) gets its value at levels 0,
25, 50, 75 and 99, then the curve's own numbers (``base``, ``mid``,
``top``, ``knee``, ``low_scale``). A weapon stat with a floor
(``damage_min``, ``ammo_min``, ``rounds_min``) is shown raised to it, as the
battle reads it. Names are as the game ships them. The files are UTF-8
with a byte order mark, so spreadsheet programs read the Japanese names.
"""

from __future__ import annotations

import csv
import json
from pathlib import Path

LEVELS = (0, 25, 50, 75, 99)
PART_LEVEL_MAX = 99
VALUE_CEILING = 1.0e8  # 0x1c3e50
CURVE_KEYS = ("base", "mid", "top", "knee", "low_scale")
FLOORS = {"damage": "damage_min", "ammo": "ammo_min", "rounds": "rounds_min"}
SKIPPED = {"face", "robot_face"}  # sprite references, not stats
# A module's bonuses to the robot's fight stats (0x075d4a adds them on):
# the field names garage_tables keeps are the robot fields they go to. Its
# +4 byte is the maker every part record has (0x017ed8; 7 none, and no
# module has one); +0x1c is read by nothing.
RENAMED = {
    "robot_4538": "critical_percent",
    "robot_4540": "knockdown_resistance",
    "word_04": "maker",
    "word_1c": "unused_1c",
}
# The pilot's bounty grows by level / 64 (0x0456b7, data/world/rumbling.json).
BOUNTY_LEVEL_SCALE = 64.0


def curve_value(curve: dict, level: int) -> float:
    """``LevelCurve::value``: up to the knee from base by mid, then on to top."""
    amount = abs(level)
    scale = curve["low_scale"] if level < 0 else 1
    knee = curve["knee"]
    if knee == 0:
        out = curve["base"] + amount / PART_LEVEL_MAX * curve["top"] * scale
    elif knee == PART_LEVEL_MAX:
        out = curve["base"] + amount / PART_LEVEL_MAX * curve["mid"] * scale
    elif amount < knee:
        out = curve["base"] + amount / knee * curve["mid"] * scale
    else:
        past = (amount - knee) / (PART_LEVEL_MAX - knee)
        out = curve["base"] + (curve["mid"] + past * (curve["top"] - curve["mid"])) * scale
    return min(max(out, 0.0), VALUE_CEILING)


def _is_curve(value: object) -> bool:
    return isinstance(value, dict) and all(key in value for key in CURVE_KEYS)


def _number(value: float) -> float | int:
    return int(value) if float(value).is_integer() else round(value, 3)


def _flatten(record: dict, prefix: str = "") -> dict:
    row: dict = {}
    for key, value in record.items():
        if key in SKIPPED:
            continue
        name = f"{prefix}{RENAMED.get(key, key)}"
        if _is_curve(value):
            floor = record.get(FLOORS.get(key, ""), 0) or 0
            for level in LEVELS:
                row[f"{name} @{level}"] = _number(max(curve_value(value, level), floor))
            for part in CURVE_KEYS:
                row[f"{name} {part}"] = value[part]
        elif isinstance(value, dict):
            row.update(_flatten(value, f"{name}."))
        elif isinstance(value, list):
            row[name] = ";".join(json.dumps(item, ensure_ascii=False) if isinstance(item, (dict, list)) else str(item) for item in value)
        else:
            row[name] = value
    return row


def _write(path: Path, rows: list[dict]) -> int:
    columns: list[str] = []
    for row in rows:
        columns.extend(key for key in row if key not in columns)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)
    return len(rows)


def _indexed(records: list[dict]) -> list[dict]:
    return [{"index": index, **_flatten({k: v for k, v in record.items() if k != "index"})} for index, record in enumerate(records)]


def _name(records: list[dict], index: int) -> str:
    return records[index]["name"] if 0 <= index < len(records) else ""


def _robots(garage: dict, parts: dict) -> list[dict]:
    tables = {
        "body": parts["bodies"], "legs": parts["legs"], "chip": parts["chips"],
        "arm_left": parts["arm_weapons"], "arm_right": parts["arm_weapons"],
        "shoulder_left": parts["shoulder_weapons"], "shoulder_right": parts["shoulder_weapons"],
    }
    rows = []
    for index, robot in enumerate(garage["robots"]):
        row = {"index": index, "name": robot["name"], "flags": robot.get("flags")}
        for slot, table in tables.items():
            part = (robot.get("parts") or {}).get(slot)
            row[f"{slot}"] = _name(table, part["id"]) if part else ""
            row[f"{slot} id"] = part["id"] if part else ""
            row[f"{slot} level"] = part["level"] if part else ""
        row["modules"] = ";".join(_name(garage["modules"], m) for m in robot.get("modules", []))
        row["paint"] = _name(garage["paints"], robot.get("paint", -1))
        row["emblem"] = _name(garage["emblems"], robot.get("emblem", -1))
        rows.append(row)
    return rows


def _entrants(town: dict, parts: dict, garage: dict) -> list[dict]:
    rows = []
    for index, entrant in enumerate(town["entrants"]):
        pilot = parts["pilots"][entrant["pilot"]] if 0 <= entrant["pilot"] < len(parts["pilots"]) else {}
        level = entrant["level"]
        bounty = int(curve_value(pilot["bounty"], level)) if pilot.get("bounty") else 0
        if level > 0:
            bounty = int(bounty * (level / BOUNTY_LEVEL_SCALE + 1.0))
        rows.append({
            "index": index, "pilot": pilot.get("name", ""), "pilot id": entrant["pilot"],
            "partner": pilot.get("robot_name", ""), "team name": pilot.get("team") or "",
            "robot": _name(garage["robots"], entrant["robot"]), "robot id": entrant["robot"],
            "level": level, "prize money": bounty, "word_4": entrant.get("word_4"),
        })
    return rows


def export(assets: Path, output: Path) -> dict[str, int]:
    """Writes ``output/<kind>.csv`` from the converted ``assets``; returns the rows per file."""
    tables = assets / "tables"
    parts = json.loads((tables / "parts.json").read_text(encoding="utf-8"))
    garage = json.loads((tables / "garage.json").read_text(encoding="utf-8"))
    town = json.loads((tables / "town.json").read_text(encoding="utf-8"))
    files = {
        "bodies": _indexed(parts["bodies"]),
        "legs": _indexed(parts["legs"]),
        "arm_weapons": _indexed(parts["arm_weapons"]),
        "shoulder_weapons": _indexed(parts["shoulder_weapons"]),
        "chips": _indexed(parts["chips"]),
        "modules": _indexed(garage["modules"]),
        "paints": _indexed(garage["paints"]),
        "emblems": _indexed(garage["emblems"]),
        "robots": _robots(garage, parts),
        "pilots": _indexed(parts["pilots"]),
        "opponents": _entrants(town, parts, garage),
        "contests": _indexed(town["contests"]),
    }
    return {name: _write(output / f"{name}.csv", rows) for name, rows in files.items()}
