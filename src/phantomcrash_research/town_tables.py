"""The town's game tables read from the European ``default.xbe``.

Written to ``tables/town.json``:

- ``classes``: the contest classes (``0x24d108``, 28 records of 0x2c
  bytes): name, the regulation text the schedule shows, the play fee in NY
  (+8), the robot's weight range (+0xc, +0x10, floats, -1 for no limit:
  ``0x075f93`` against the entrants, ``0x046411``), the maker a robot must
  be (+0x14; 7 for any), up to four leg types (+0x18; 8 or -1 for none) and
  one more regulation (+0x28: 0 rentals only, 1 no Optic Camouflage, -1
  none). The schedule prints the regulation text when a class has a maker,
  leg or other rule, and "No limits." otherwise (``0x01469d``).
- ``contests``: the contests (``0x24b388``, 56 records of 0x6c bytes): name,
  class (+4), a night contest (+8 is 1), the entrants' level range (+0xc,
  +0x10), how many team members are drawn before the extra team may be
  (``reserve_after``, +0x14) and the roster's size less the player
  (``roster_size``, +0x18; ``0x045270`` adds one and keeps it to 6), the
  extra team (+0x1c, -1 for none) and the eight teams entered (+0x20, -1
  for none: indices into ``teams``), the champion (+0x40, an entrant) and
  the champion's two portrait sprites (``0x05fa5e``: the entrant's first
  short names a pilot record, ``0x0c7a51``: 0x1c-byte records at
  ``0x1c9920`` whose first two shorts are name entries, ``0x0c7a09``:
  0x10-byte records at ``0x1c5af8`` with the sprite at +8; RECORDS shows
  them, ``0x033101`` and ``0x033132``) and the fields not understood yet.
- ``teams``: the teams (``0x24d5d8``, 272 records of 0x24 bytes, read by
  ``0x045270``): the leader (+0) and eight members (+4), entrants or -1.
- ``entrants``: the entrants (``0x1b1008``, 1187 records of 8 bytes,
  ``0x05fa5e``): a pilot record (+0, tables/parts.json ``pilots``), a
  robot (+2, tables/garage.json ``robots``), a word copied to the
  loadout's +0x5a (+4) and the level (+6); ``0x0456b7`` fills a roster
  slot from one.
- ``calendar``: the 60 days (``0x249fa8``, records of 0x3c bytes, read by
  ``0x06ce5d`` with the day mod 60): ``venues`` are the contests on the
  schedule's three rows (+0xc, +0x10, +0x14; -1 for none); ``dealers`` are
  the first three words, each dealer's lineup of SVs that day (American
  Stars, Kojima, Ventuno: the dealer index ``0x0393de`` sets;
  tables/garage.json ``dealer_lineups``). Wild Machines' stock that day
  comes from the rest (tables/garage.json): ``chips`` (+0x18, ``0x06d35f``)
  is the CHIP lineup, ``modules`` (+0x1c to +0x34, ``0x06d3a1``) the
  module lineup of each of its seven categories (body, legs, arm weapon,
  backpack weapon, option, colouring pattern, sticker) and ``used_svs``
  (+0x38, ``0x06d3e7``) the used SV lineup. ``0x06ce91`` accepts a venue
  up to 3, but the schedule shows only the three.
  ``0x06ce91`` shows contest 0x31 as 0x34 once the game is cleared.

Names are the game's own (Shift-JIS, normalised to plain Unicode); the
field names are researcher-assigned.
"""

from __future__ import annotations

import struct
import unicodedata

from . import events
from .game_tables import GameTables, _Image

CLASSES = 0x24D108
CLASS_SIZE = 0x2C
CLASS_COUNT = 28
CONTESTS = 0x24B388
CONTEST_SIZE = 0x6C
CONTEST_COUNT = 56
CALENDAR = 0x249FA8
DAY_SIZE = 0x3C
DAYS = 60
VENUE_WORD = 3  # +0xc: the first venue's contest
VENUE_COUNT = 3
MODULE_CATEGORIES = 7  # Wild Machines' module lineups, +0x1c
CONTEST_TEAMS = 8
ENTRANTS = 0x1B1008
ENTRANT_SIZE = 8
ENTRANT_COUNT = 0x4A3
TEAMS = 0x24D5D8
TEAM_SIZE = 0x24
TEAM_COUNT = 0x110
TEAM_MEMBERS = 8
CREWS = 0x1C9920
CREW_SIZE = 0x1C
CREW_COUNT = 0x424
PILOTS = 0x1C5AF8
PILOT_SIZE = 0x10
PILOT_COUNT = 0x3DE
PILOT_SPRITE = 8


def _text(image: _Image, address: int) -> str:
    if address == 0:
        return ""
    raw = image.raw_string(address)
    return unicodedata.normalize("NFKC", raw.decode("cp932", errors="replace")).strip()


def _ints(image: _Image, address: int, count: int) -> list[int]:
    return list(struct.unpack_from(f"<{count}i", image.data, image.offset(address, 4 * count)))


def _floats(image: _Image, address: int, count: int) -> list[float]:
    values = struct.unpack_from(f"<{count}f", image.data, image.offset(address, 4 * count))
    return [round(value, 4) for value in values]


def _class(image: _Image, index: int) -> dict:
    address = CLASSES + index * CLASS_SIZE
    words = _ints(image, address, 11)
    return {
        "name": _text(image, words[0] & 0xFFFFFFFF),
        "regulation": _text(image, words[1] & 0xFFFFFFFF),
        "fee": words[2],
        "weight": _floats(image, address + 0xC, 2),
        "maker": words[5],
        "legs": [leg for leg in words[6:10] if 0 <= leg < 8],
        "other": words[10],
    }


def _short(image: _Image, address: int) -> int:
    return struct.unpack_from("<h", image.data, image.offset(address, 2))[0]


def _record(index: int, count: int) -> int:
    # The lookups clamp a bad index to the first record.
    return index if 0 <= index < count else 0


def _portraits(image: _Image, tables: GameTables | None, entrant: int) -> list:
    if entrant == -1:
        return []
    crew = _short(image, ENTRANTS + _record(entrant, ENTRANT_COUNT) * ENTRANT_SIZE)
    crew_address = CREWS + _record(crew, CREW_COUNT) * CREW_SIZE
    pilots = [_short(image, crew_address), _short(image, crew_address + 2)]
    sprites = [
        _short(image, PILOTS + _record(pilot, PILOT_COUNT) * PILOT_SIZE + PILOT_SPRITE)
        for pilot in pilots
    ]
    if tables is None:
        return sprites
    return [events.sprite_json(tables, sprite) for sprite in sprites if sprite >= 0]


def _contest(image: _Image, tables: GameTables | None, index: int) -> dict:
    address = CONTESTS + index * CONTEST_SIZE
    words = _ints(image, address, 17)
    return {
        "name": _text(image, words[0] & 0xFFFFFFFF),
        "class": words[1],
        "night": words[2] == 1,
        "levels": words[3:5],
        "reserve_after": words[5],
        "roster_size": words[6],
        "extra_team": words[7],
        "teams": [team for team in words[8 : 8 + CONTEST_TEAMS] if team >= 0],
        "champion": words[16],
        "champion_portraits": _portraits(image, tables, words[16]),
        "floats_44": _floats(image, address + 0x44, 10),
    }


def _team(image: _Image, index: int) -> dict:
    words = _ints(image, TEAMS + index * TEAM_SIZE, 1 + TEAM_MEMBERS)
    return {"leader": words[0], "members": words[1:]}


def _entrant(image: _Image, index: int) -> dict:
    pilot, robot, ai_profile, level = struct.unpack_from(
        "<4h", image.data, image.offset(ENTRANTS + index * ENTRANT_SIZE, ENTRANT_SIZE)
    )
    return {"pilot": pilot, "robot": robot, "ai_profile": ai_profile, "level": level}


def _day(image: _Image, day: int) -> dict:
    words = _ints(image, CALENDAR + day * DAY_SIZE, DAY_SIZE // 4)
    return {
        "venues": words[VENUE_WORD : VENUE_WORD + VENUE_COUNT],
        "dealers": words[:VENUE_WORD],
        "chips": words[6],
        "modules": words[7 : 7 + MODULE_CATEGORIES],
        "used_svs": words[14],
    }


def town_tables(data: bytes, tables: GameTables | None = None) -> dict:
    """The tables; with ``tables``, portraits are images and rectangles, not sprite ids."""
    image = _Image(data)
    return {
        "classes": [_class(image, index) for index in range(CLASS_COUNT)],
        "contests": [_contest(image, tables, index) for index in range(CONTEST_COUNT)],
        "calendar": [_day(image, day) for day in range(DAYS)],
        "teams": [_team(image, index) for index in range(TEAM_COUNT)],
        "entrants": [_entrant(image, index) for index in range(ENTRANT_COUNT)],
    }
