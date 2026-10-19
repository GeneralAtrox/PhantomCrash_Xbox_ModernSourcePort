"""Battle data read from the European ``default.xbe`` and the disc.

- Pepper's in-battle lesson messages. The robot state table at ``0x242d38``
  (three handlers per state) runs the tutorial battle's lessons as robot
  states 0x18-0x2e; each state shows up to two message scripts, picked from
  ``LESSON_MESSAGES`` (``0x091f8f``: the second table when the robot's flag
  0x2740 bit 0 is set, which 0x027d5a sets for BUTTON CONFIG's CONTROL TYPE
  Standard; the first table is the Preferred type's). The scripts are the
  talk scenes' 13-int commands (:mod:`events`), run by the same interpreter.
- Enemy paths (``Media/enepath/*_enepath.bin``, magic ``ENPT``): a graph of
  polylines the drones walk. Segments of kind 0 start at the arena's edge;
  each segment lists the segments that may follow it.

State numbers and table names are researcher-assigned; the lesson text is
the game's own.
"""

from __future__ import annotations

import struct

from . import events
from .game_tables import GameTables, _Image

LESSON_MESSAGES = (0x243168, 0x243220)  # {script *first, *second}[state]: Preferred, Standard
LESSON_FIRST_STATE = 0x18
LESSON_LAST_STATE = 0x2E

ENEMY_PATH_MAGIC = b"ENPT"
ENEMY_PATH_TABLE = 0x0C  # u32 segment offsets, up to the first segment


def lesson_messages(data: bytes, tables: GameTables) -> dict:
    """Every lesson state's message scripts: ``{"states": {state: {...}}}``.

    Each state maps ``"first"``/``"second"`` to a command list, and the
    same under ``"standard"`` where the Standard control type's table
    differs.
    """
    image = _Image(data)
    states = {}
    for state in range(LESSON_FIRST_STATE, LESSON_LAST_STATE + 1):
        entry: dict = {}
        default = [image.u32(LESSON_MESSAGES[0] + state * 8 + index * 4) for index in range(2)]
        other = [image.u32(LESSON_MESSAGES[1] + state * 8 + index * 4) for index in range(2)]
        for name, address in zip(("first", "second"), default):
            if address:
                entry[name] = events.commands(image, tables, address)
        standard = {}
        for name, address, usual in zip(("first", "second"), other, default):
            if address and address != usual:
                standard[name] = events.commands(image, tables, address)
        if standard:
            entry["standard"] = standard
        states[str(state)] = entry
    return {"states": states}


def enemy_path(data: bytes) -> dict:
    """An ``ENPT`` file as ``{"segments": [{"kind", "points", "next"}]}``."""
    if data[:4] != ENEMY_PATH_MAGIC:
        raise ValueError("not an enemy path (no ENPT magic)")
    first = struct.unpack_from("<I", data, ENEMY_PATH_TABLE)[0]
    count = (first - ENEMY_PATH_TABLE) // 4
    offsets = list(struct.unpack_from(f"<{count}I", data, ENEMY_PATH_TABLE))
    index_of = {offset: index for index, offset in enumerate(offsets)}
    segments = []
    for offset in offsets:
        _, _, kind, links = struct.unpack_from("<4B", data, offset)
        points_offset = struct.unpack_from("<I", data, offset + 8)[0]
        following = struct.unpack_from(f"<{links}I", data, offset + 12)
        point_count = struct.unpack_from("<I", data, points_offset)[0]
        points = [
            [round(value, 4) for value in struct.unpack_from("<3f", data, points_offset + 4 + 12 * i)]
            for i in range(point_count)
        ]
        segments.append(
            {"kind": kind, "points": points, "next": [index_of[link] for link in following]}
        )
    return {"segments": segments}
