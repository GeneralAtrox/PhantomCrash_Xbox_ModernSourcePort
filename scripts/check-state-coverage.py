"""Audit the explicit state owners; public aggregates use complete decomposition.

These private owners name all member storage with a trailing underscore. Fail
when a declared member is absent from its visitor and the reviewed exclusions.
This checks field coverage, not the semantics of each visitor or exclusion.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
OWNERS = {
    "Battle": ("battle", "battle", {"restore_generation_"}),
    "RumblingWorld": ("rumbling_world", "world", set()),
    "BattleScene": ("battle_scene", "scene", {"sounds_", "music_"}),
    "RobotMover": ("robot_mover", "robot", set()),
    "RobotCombat": ("robot_combat", "robot", set()),
    "RobotAnimator": ("robot_library", "robot", set()),
    "SourceLegPlayback": ("source_leg_playback", "robot", set()),
    "SourceRollState": ("source_roll", "robot", set()),
    "PathFollower": ("enemy_path", "battle", set()),
    "ShownArmour": ("shown_armour", "battle", set()),
    "Armory": ("battle_weapons", "battle", set()),
    "StageCollision": ("stage_collision", "arena", set()),
    "NavGraph": ("nav_graph", "arena", set()),
    "StageDoors": ("stage_doors", "arena", {"sounds_"}),
    "StageObjects": ("stage_objects", "arena", {"sounds_"}),
    "RobotWrecks": ("robot_wrecks", "arena", set()),
    "SupplyDrop": ("supply_drop", "arena", set()),
}


def clean(text):
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', "", text, flags=re.S)


def block(text, start):
    opening = text.index("{", start)
    depth = 1
    for i in range(opening + 1, len(text)):
        depth += (text[i] == "{") - (text[i] == "}")
        if depth == 0:
            return text[opening + 1:i]
    raise ValueError("Unterminated owner body")


failures = []
total = 0
for owner, (header, visitor, excluded) in OWNERS.items():
    source = clean((ROOT / f"port/include/phantomcrash/{header}.hpp").read_text())
    match = re.search(rf"class {owner}(?: final)?\s*\{{", source)
    body = block(source, match.start())
    fields = set(re.findall(r"\b[a-z][a-z0-9]*_\b", body))
    # Underscores within names also occur, e.g. quick_turn_cooldown_.
    fields |= set(re.findall(r"\b[a-z][a-z0-9_]*_\b", body))
    implementation = clean((ROOT / f"port/src/online/state_{visitor}.cpp").read_text())
    start = implementation.index(f"const {owner} &v)")
    visited = set(re.findall(r"\bv\.([a-z][a-z0-9_]*_)\b", block(implementation, start)))
    missing = fields - visited - excluded
    stale = excluded - fields
    if missing or stale:
        failures.append(f"{owner}: missing={sorted(missing)}, stale exclusions={sorted(stale)}")
    total += len(fields)

# Combatant has a constructor, so it cannot use aggregate decomposition. Its
# public data declarations all precede that constructor and are listed explicitly.
source = clean((ROOT / "port/include/phantomcrash/combatant.hpp").read_text())
data = source.split("struct Combatant {", 1)[1].split("explicit Combatant", 1)[0]
fields = set(re.findall(r"\b([a-z][a-z0-9_]*)\s*(?:\{[^{}]*\})?\s*;", data))
implementation = clean((ROOT / "port/src/online/state_battle.cpp").read_text())
visited = set(re.findall(r"\bv\.([a-z][a-z0-9_]*)\b",
                        block(implementation, implementation.index("const Combatant &v)"))))
if fields != visited:
    failures.append(f"Combatant: missing={sorted(fields - visited)}, unknown={sorted(visited - fields)}")
total += len(fields)

# CoopMatch is an aggregate, but its transport and resource owners must not be
# traversed. Audit the explicit desktop wrapper's top-level storage separately.
source = clean((ROOT / "port/src/desktop_coop_matches.hpp").read_text())
body = block(source, source.index("struct CoopMatch {"))
depth = 0
surface = []
for char in body:
    if char == "{":
        depth += 1
    elif char == "}":
        depth -= 1
    elif depth == 0:
        surface.append(char)
data = "".join(surface)
fields = set()
for declaration in data.split(";"):
    if "(" in declaration or "struct " in declaration:
        continue
    fields.update(re.findall(r"\b([a-z][a-z0-9_]*)\s*(?:,|$)", declaration.strip()))
implementation = clean((ROOT / "port/src/desktop_online_state.cpp").read_text())
visited = set(re.findall(r"\bmatch\.([a-z][a-z0-9_]*)\b", implementation))
excluded = {"online", "network_prepared", "resources", "presentations"}
if fields != visited | excluded:
    failures.append(f"CoopMatch: missing={sorted(fields - visited - excluded)}, "
                    f"unknown={sorted((visited | excluded) - fields)}")
total += len(fields)
if failures:
    raise SystemExit("\n".join(failures))
print(f"PASS: {len(OWNERS) + 2} explicit owners, {total} fields accounted for")
