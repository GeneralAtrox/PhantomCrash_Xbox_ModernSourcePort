"""Battle chatter read from the European ``default.xbe``.

In battle, robots comment on what happens to them: a window mid-screen
(where Pepper's lesson messages show) with a face, a name and a line.
Game code raises a chatter event for a robot (``0x049470``, events 0-34);
the event passes when rand(0..1) is below its chance, from the table for
the robot's kind, and a robot keeps only its most urgent (lowest) event.
Every tick ``0x049552`` shows the lowest event pending over all robots,
unless a chatter window is open, from the speaker's line table and clears
the rest.

- Gates (``0x203848``, a short per event): 1 only for robots a player
  controls (robot +8 not -1), 2 only for computer robots on team 2
  (loadout +0x6c), 0 for any robot.
- Chances (floats per event): ``0x203890`` for a player's robot,
  ``0x203920`` for a computer robot on team 2, ``0x2039b0`` for the other
  computer robots.
- Line tables (``0x1b3520``, 67 tables of 35 script pointers, ``0x05faa9``):
  a player's robot speaks from its AI chip's table (chip +0x18, copied to
  robot +0x4508 by ``0x092c6c``), a computer robot from loadout +0x54. A
  null entry says nothing.

The scripts are the talk scenes' 13-int commands (:mod:`events`), run by
``0x0c57e0`` with four values: the text code 0xfff8 prints the robot's
name and sprite -1 is the robot's face (name entry +8), the code 0xfff9
prints the pilot's name and sprite -2 is the pilot's face. For a player's
robot the robot's name is its chip's (``0x06d663``: the chip's name entry,
or the chip's own name), its face the chip's name entry's, and the pilot's
name the player's; there is no pilot face. Event names are
researcher-assigned; the lines are the game's own.
"""

from __future__ import annotations

import json
import struct

from . import events
from .game_tables import GameTables, _Image

CHATTER_TABLES = 0x1B3520
CHATTER_TABLE_COUNT = 67
CHATTER_EVENTS = 0x23  # also the "nothing pending" value of robot +0x4594
CHATTER_GATES = 0x203848
CHATTER_CHANCES = {"player": 0x203890, "team_two": 0x203920, "computer": 0x2039B0}


def _floats(image: _Image, address: int, count: int) -> list[float]:
    values = struct.unpack_from(f"<{count}f", image.data, image.offset(address, 4 * count))
    return [round(value, 4) for value in values]


def chatter(data: bytes, tables: GameTables) -> dict:
    """``{"gates", "chances", "scripts", "tables"}``: each table lists, per
    event, an index into ``scripts`` or null."""
    image = _Image(data)
    gates = list(
        struct.unpack_from(
            f"<{CHATTER_EVENTS}h", image.data, image.offset(CHATTER_GATES, 2 * CHATTER_EVENTS)
        )
    )
    chances = {
        kind: _floats(image, address, CHATTER_EVENTS) for kind, address in CHATTER_CHANCES.items()
    }
    scripts: list[list[dict]] = []
    by_address: dict[int, int] = {}
    by_content: dict[str, int] = {}
    line_tables = []
    for table in range(CHATTER_TABLE_COUNT):
        entries: list[int | None] = []
        for event in range(CHATTER_EVENTS):
            address = image.u32(CHATTER_TABLES + (table * CHATTER_EVENTS + event) * 4)
            if not address:
                entries.append(None)
                continue
            if address not in by_address:
                commands = events.commands(image, tables, address)
                key = json.dumps(commands, sort_keys=True, ensure_ascii=False)
                if key not in by_content:
                    by_content[key] = len(scripts)
                    scripts.append(commands)
                by_address[address] = by_content[key]
            entries.append(by_address[address])
        line_tables.append(entries)
    return {"gates": gates, "chances": chances, "scripts": scripts, "tables": line_tables}
