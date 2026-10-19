"""Editable rotating S.L.A.I. deliveries on Phantom Crash's 60-day calendar."""

from __future__ import annotations

import copy
import json
from pathlib import Path


def calendar_load(town: dict, garage: dict) -> list[dict]:
    rows = []
    stock = [{(category, p["id"], p["level"]) for category, lineup in enumerate(d["modules"])
              for p in garage["module_lineups"][lineup] if p["id"] >= 0}
             for d in town["calendar"]]
    seen = set()
    for i, day in enumerate(town["calendar"]):
        active = sum(town["contests"][c]["class"] != 0 for c in day["venues"])
        new = stock[i] - seen
        seen.update(stock[i])
        rows.append({"day": i + 1, "stock": len(stock[i]), "first_seen": len(new),
                     "changed": len(stock[i] - stock[i - 1]), "active_venues": active})
    return rows


def delivery_days(rows: list[dict], count: int) -> list[int]:
    """Spread arrivals through the whole cycle, filling quieter dates first.

    Later visits permit purchases again after each part's first arrival.
    The selected days are written to JSON and can be changed without code.
    """
    bookings = [0] * len(rows)
    choices = []
    for i in range(count):
        # Anchor each delivery in its section of the cycle; a five-day
        # neighbourhood avoids concentrating everything on one quiet date.
        anchor = int((i + 0.5) * len(rows) / count)
        nearby = range(max(0, anchor - 2), min(len(rows), anchor + 3))
        day = min(nearby, key=lambda j: (bookings[j], rows[j]["first_seen"],
                                        rows[j]["active_venues"], rows[j]["changed"], j))
        bookings[day] += 1
        choices.append(day + 1)
    return choices


def rotating_deliveries(rows: list[dict], parts: list[dict]) -> list[dict]:
    if not parts:
        return []
    days = delivery_days(rows, len(parts))
    days[0] = 1  # One starter part; later parts must not appear before arrival.
    deliveries = []
    for i, (part, day) in enumerate(zip(parts, days)):
        # Keep an introduction available until the next one when the quiet-day
        # placement leaves a gap. Subsequent visits last four days.
        next_day = days[i + 1] if i + 1 < len(days) else len(rows) + 1
        intro = max(4, next_day - day)
        available = {day + offset for offset in range(intro)}
        available.update(day + visit + offset for visit in (20, 40) for offset in range(4))
        deliveries.append({**part, "first_delivery": day, "intro_days": intro, "visit_days": 4,
                           "days": sorted(d for d in available if d <= len(rows))})
    return deliveries


def apply_deliveries(town: dict, garage: dict, deliveries: list[dict]) -> tuple[dict, dict]:
    """Append per-day copies; shared original lineups are never modified."""
    town, garage = copy.deepcopy(town), copy.deepcopy(garage)
    appended = {}
    for entry in deliveries:
        category, part, level = entry["category"], entry["id"], entry.get("level", 0)
        if category not in range(7) or part < 0 or not 0 <= level <= 99:
            raise ValueError("Invalid S.L.A.I. shop delivery")
        for day in entry["days"]:
            if day not in range(1, len(town["calendar"]) + 1):
                raise ValueError("Delivery day is outside the calendar")
            key = (day, category)
            if key not in appended:
                row = town["calendar"][day - 1]
                lineup = copy.deepcopy(garage["module_lineups"][row["modules"][category]])
                row["modules"][category] = len(garage["module_lineups"])
                garage["module_lineups"].append(lineup)
                appended[key] = lineup
            part_entry = {"id": part, "level": level}
            if part_entry not in appended[key]:
                appended[key].append(part_entry)
    return town, garage


def write_progression(assets: Path, mods: Path, parts: list[dict], schedule: Path | None = None) -> dict:
    town = json.loads((assets / "tables/town.json").read_text(encoding="utf-8"))
    garage = json.loads((assets / "tables/garage.json").read_text(encoding="utf-8"))
    rows = calendar_load(town, garage)
    if schedule and schedule.exists():
        deliveries = json.loads(schedule.read_text(encoding="utf-8"))["deliveries"]
        if {(p["category"], p["id"]) for p in deliveries} != {(p["category"], p["id"]) for p in parts}:
            raise ValueError("Delivery schedule does not cover the imported parts")
    else:
        deliveries = rotating_deliveries(rows, parts)
    town, garage = apply_deliveries(town, garage, deliveries)
    for file, value in (("tables/town.json", town), ("tables/garage.json", garage),
                        ("slai/deliveries.json", {"calendar_load": rows, "deliveries": deliveries})):
        output = mods / file
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return {"parts": len(deliveries), "shop_days": len({d for p in deliveries for d in p["days"]})}
