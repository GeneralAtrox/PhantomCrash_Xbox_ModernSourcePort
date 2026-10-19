"""Robot part records read from the European ``default.xbe``.

- AI chips (``0x1b7fc8``, 96 records of 0x7c bytes, looked up by
  ``0x09271e``): the fire control. ``0x092c6c`` copies a chip into the
  robot at +0x4504; the HUD reads the inner and outer reticles' shapes and
  sizes (+0x450c to +0x451c), the tracking speed (+0x4514) and the lock
  time in frames (+0x4520) from there.
- Arm weapons (``0x1bae48``, 76 records of 0xc8 bytes, ``0x0926be``) and
  shoulder weapons (``0x1be9a8``, 69 records, ``0x0926ee``). ``0x09d709``
  builds a weapon from its record: the byte at +0x1c is the kind the HUD
  draws (0 cannon, 1 missile, 2 machine gun or shotgun, 3 grenade, 5 beam,
  6 melee) and ``0x092a07`` reads its stats (see docs/PART_RECORDS.md).
  Frame counts and speeds are in 1/60 s frames.
- Bodies (``0x1b5b88``, 16 records of 0x40 bytes, ``0x092661``) and legs
  (``0x1b5f88``, 43 records of 0xc0 bytes, ``0x09268e``): a robot's armour
  is the two parts' armour, each raised to a floor, plus its option parts
  (``0x076114``). The legs' speed, jump and turn curves are scaled by the
  robot's load (``0x075a36``, docs/PART_RECORDS.md); the speeds each legs
  model's motions cover at their own rate (``0x232558``) turn the speed
  into the motions' playback rate.
- Pilots (``0x1c9920``, 0x424 records of 0x1c bytes, ``0x0c7a51``) name a
  robot's pilot and robot through the name table (``0x1c5af8``, 16 bytes an
  entry, ``0x0c7a09``); a marker's name tag is the pilot entry's second
  string (``0x04de50``), and an entry's short at +8 is its face sprite in
  the battle chatter (``0x0c6ec5``, :mod:`chatter`). The short at +4 names
  the pilot's team (``0x05fa42``, the strings at ``0x20c2d0``) and +8 holds
  the bounty curve, the NY the pilot is worth by level (``0x092311``;
  WIRE HEADS DATA shows it).

Most values grow with the part's level (-99..99) along a curve
(:func:`level_value`: ``0x092160`` for 16-bit curves, ``0x0924fd`` for
float ones). The record names are the game's own (Shift-JIS, written
here as plain text); the field names are researcher-assigned.
"""

from __future__ import annotations

import struct

from . import events
from .game_tables import GameTables, _Image, _plain

CHIPS = 0x1B7FC8
CHIP_COUNT = 0x60
CHIP_SIZE = 0x7C
ARM_WEAPONS = 0x1BAE48
ARM_WEAPON_COUNT = 0x4C
SHOULDER_WEAPONS = 0x1BE9A8
SHOULDER_WEAPON_COUNT = 0x45
WEAPON_SIZE = 0xC8
BODIES = 0x1B5B88
BODY_COUNT = 0x10
BODY_SIZE = 0x40
LEGS = 0x1B5F88
LEGS_COUNT = 0x2B
LEGS_SIZE = 0xC0
PILOTS = 0x1C9920
PILOT_COUNT = 0x424
PILOT_SIZE = 0x1C
NAMES = 0x1C5AF8
NAME_COUNT = 0x3DE
NAME_SIZE = 0x10
NAME_FACE = 8  # a name entry's face sprite (0x0c6ec5)
TEAM_NAMES = 0x20C2D0  # a pilot's team name, by the short at +4 (0x05fa42)
TEAM_NAME_COUNT = 84
PILOT_BOUNTY = 8  # the NY a pilot is worth, by level (0x092311, 0x0456b7)

# The armour floors (0x076114). A robot with a player number uses its
# body class's floor (0x092d92) and 100 for its legs (0x092e0b); a computer
# robot uses 0 for its body (0x092f3e) and its legs model's floor (0x092fb7).
PLAYER_BODY_FLOORS = (6000, 4500, 7500, 9000, 9000, 10500, 100)  # body classes 0-6
FLOOR_DEFAULT = 100
PLAYER_LEGS_FLOOR = 100
COMPUTER_BODY_FLOOR = 0
COMPUTER_LEGS_FLOOR = 2000  # legs models 0-19
COMPUTER_LEGS_FLOORS = {20: 600}
COMPUTER_LEGS_MODELS = 21
LEG_MOTION_SPEEDS = 0x232558  # per legs model: walk, dash (floats; 0 without root motion)

LEVEL_MAX = 99  # 0x19cbfc
VALUE_CEILING = 1.0e8  # 0x1c3e50: a curve's value is clamped to 0..this


def _short_curve(image: _Image, address: int) -> dict:
    """``{int16 base, mid, top; int8 knee, low_scale}`` (``0x092160``)."""
    base, mid, top, knee, low = struct.unpack_from("<3h2b", image.data, image.offset(address, 8))
    return {"base": base, "mid": mid, "top": top, "knee": knee, "low_scale": low}


def _float_curve(image: _Image, address: int) -> dict:
    """``{float base, mid, top; int8 knee, low_scale}`` (``0x0924fd``)."""
    base, mid, top, knee, low = struct.unpack_from("<3f2b", image.data, image.offset(address, 14))
    return {
        "base": round(base, 5),
        "mid": round(mid, 5),
        "top": round(top, 5),
        "knee": knee,
        "low_scale": low,
    }


def _int_curve(image: _Image, address: int) -> dict:
    """``{int32 base, mid, top; int8 knee, low_scale}`` (``0x092311``)."""
    base, mid, top, knee, low = struct.unpack_from("<3i2b", image.data, image.offset(address, 14))
    return {"base": base, "mid": mid, "top": top, "knee": knee, "low_scale": low}


def level_value(curve: dict, level: int) -> float:
    """A curve's value at ``level``, as ``0x092160`` and ``0x0924fd`` work it out.

    Up to the knee the value climbs from ``base`` by ``mid``; from the knee
    to level 99 it climbs on to ``base + top``. A knee of 0 climbs straight
    to ``top`` and a knee of 99 to ``mid``. A negative level uses the same
    curve by its size, scaled by ``low_scale``.
    """
    amount = float(abs(level))
    scale = float(curve["low_scale"]) if level < 0 else 1.0
    base, mid, top, knee = curve["base"], curve["mid"], curve["top"], curve["knee"]
    if knee == 0:
        value = base + amount / LEVEL_MAX * top * scale
    elif knee == LEVEL_MAX:
        value = base + amount / LEVEL_MAX * mid * scale
    elif amount < knee:
        value = base + amount / knee * mid * scale
    else:
        value = base + (mid + (amount - knee) / (LEVEL_MAX - knee) * (top - mid)) * scale
    return min(max(value, 0.0), VALUE_CEILING)


def _name(image: _Image, record: int) -> str:
    return _plain(image.raw_string(image.u32(record)))


def _byte(image: _Image, address: int) -> int:
    return image.data[image.offset(address, 1)]


def _short(image: _Image, address: int) -> int:
    return struct.unpack_from("<h", image.data, image.offset(address, 2))[0]


def _ushort(image: _Image, address: int) -> int:
    return struct.unpack_from("<H", image.data, image.offset(address, 2))[0]


def _float(image: _Image, address: int) -> float:
    return round(struct.unpack_from("<f", image.data, image.offset(address))[0], 5)


def _floats(image: _Image, address: int, count: int) -> list[float]:
    values = struct.unpack_from(f"<{count}f", image.data, image.offset(address, 4 * count))
    return [round(value, 6) for value in values]


def _face(image: _Image, tables: GameTables, name_id: int) -> dict | None:
    """A name entry's face sprite, or None."""
    if not 0 <= name_id < NAME_COUNT:
        return None
    entry = NAMES + name_id * NAME_SIZE
    (sprite,) = struct.unpack_from("<h", image.data, image.offset(entry + NAME_FACE))
    return events.sprite_json(tables, sprite) if sprite >= 0 else None


def chip(image: _Image, index: int, tables: GameTables) -> dict:
    record = CHIPS + index * CHIP_SIZE
    return {
        "index": index,
        "name": _name(image, record),
        # 0x092c6c copies these to robot +0x4508 and +0x450a: the battle
        # chatter's line table and the chip's name entry, whose face shows.
        "chatter": _short(image, record + 0x18),
        "face": _face(image, tables, _short(image, record + 0x1A)),
        "inner_shape": _byte(image, record + 0x1C),
        "inner_value": _short_curve(image, record + 0x1E),
        "track": _short(image, record + 0x26),
        "outer_shape": _byte(image, record + 0x28),
        "outer_value": _short_curve(image, record + 0x2A),
        "lock_frames": _short(image, record + 0x32),
        # Robot +0x4504 (0x092c6c); 0x075d4a copies these to +0x4538..+0x4548.
        "weight": _float_curve(image, record + 8),
        "critical_percent": _float_curve(image, record + 0x3C),  # rand(0..100) below: x2
        "dodge_percent": _float_curve(image, record + 0x4C),  # clamped 0..100
        "knockdown_resistance": _float_curve(image, record + 0x5C),
        # What the shops charge at a level (0x0932c7 case 4).
        "price": _int_curve(image, record + 0x6C),
    }


def weapon(image: _Image, table: int, index: int) -> dict:
    record = table + index * WEAPON_SIZE
    return {
        "index": index,
        "name": _name(image, record),
        "family": struct.unpack_from("<i", image.data, image.offset(record + 4))[0],
        "model": struct.unpack_from("<i", image.data, image.offset(record + 8))[0],
        "kind": _byte(image, record + 0x1C),
        "flight": _byte(image, record + 0x1D),  # 0 straight, 1 lobbed, 2 missile (0x2533f4)
        "aim_mode": _byte(image, record + 0x1E),
        # The sound it fires with: a handler of table 0x2537c0 (0x099c02).
        "fire_sound": _byte(image, record + 0x21),
        "effect": _byte(image, record + 0x22),  # the shot's look (effects.json projectiles, 0x09831e)
        # What a shot does where it lands (0x098eda): on a wall or the floor,
        # and on a robot. 1 bursts with its impact, 2 explodes with it, 3
        # explodes without, 4 bursts with its impact and a sound, 5 leaves its
        # impact and flies on; 0 is removed without one.
        "surface_hit": _byte(image, record + 0x1F),
        "robot_hit": _byte(image, record + 0x20),
        "impact": _byte(image, record + 0x23),  # effects.json impacts
        "weight": _float_curve(image, record + 0x0C),  # summed by 0x075f93
        "launch_degrees": _short(image, record + 0x24),
        "fire_frames": _ushort(image, record + 0x26),
        "life_frames": _ushort(image, record + 0x28),
        "start_speed": _float(image, record + 0x2C),
        "top_speed": _float(image, record + 0x30),
        "acceleration": _float(image, record + 0x34),
        "spread_degrees": _byte(image, record + 0x38),
        "homing_min_degrees": _float(image, record + 0x3C),
        "homing_degrees": _float_curve(image, record + 0x40),
        "rounds_min": _short(image, record + 0x50),
        "rounds": _short_curve(image, record + 0x52),
        "damage_min": _short(image, record + 0x5A),
        "damage": _short_curve(image, record + 0x5C),
        "blast_radius": _short_curve(image, record + 0x64),
        "blast_start_radius": _short(image, record + 0x6C),
        "blast_frames": _short_curve(image, record + 0x6E),
        "blast_damage": _short_curve(image, record + 0x76),
        "ammo_min": _short(image, record + 0x7E),
        "ammo": _short_curve(image, record + 0x80),
        "recharge_frames": _short_curve(image, record + 0x88),
        "stun": _short_curve(image, record + 0x90),
        "knockback": _float_curve(image, record + 0x98),
        "knockdown": _float_curve(image, record + 0xA8),
        "price": _int_curve(image, record + 0xB8),
    }


def body(image: _Image, index: int) -> dict:
    record = BODIES + index * BODY_SIZE
    kind = _byte(image, record + 4)
    player_floor = PLAYER_BODY_FLOORS[kind] if kind < len(PLAYER_BODY_FLOORS) else FLOOR_DEFAULT
    return {
        "index": index,
        "name": _name(image, record),
        "class": kind,
        "armour": _int_curve(image, record + 8),
        "weight": _float_curve(image, record + 0x18),
        "sockets": _short_curve(image, record + 0x28),  # socket holes for modules (0x0bb0c8)
        "armour_floor": {"player": player_floor, "computer": COMPUTER_BODY_FLOOR},
        "price": _int_curve(image, record + 0x30),
    }


def legs(image: _Image, index: int) -> dict:
    record = LEGS + index * LEGS_SIZE
    model = struct.unpack_from("<i", image.data, image.offset(record + 8))[0]
    if 0 <= model < COMPUTER_LEGS_MODELS:
        computer_floor = COMPUTER_LEGS_FLOORS.get(model, COMPUTER_LEGS_FLOOR)
    else:
        computer_floor = FLOOR_DEFAULT
    return {
        "index": index,
        "name": _name(image, record),
        "maker": _byte(image, record + 4),  # as a body's class (0x01ede8)
        "model": model,
        "armour": _int_curve(image, record + 0xC),
        "speed": _float_curve(image, record + 0x1C),  # the dash; the walk is half
        "jump": _float_curve(image, record + 0x2C),  # the jump's height
        "turn": _float_curve(image, record + 0x3C),
        "weight": _float_curve(image, record + 0x4C),
        "capacity": _float_curve(image, record + 0x5C),  # the load before speed suffers
        "knockback_resistance": _float_curve(image, record + 0x7C),  # percent (0x0988e1)
        "armour_floor": {"player": PLAYER_LEGS_FLOOR, "computer": computer_floor},
        "price": _int_curve(image, record + 0x8C),
        # The dodge's push on the ground and in the air, units a tick under the
        # load (0x07c34e to robot +0x80 and +0x84; 0x08d98c).
        "dodge": _float_curve(image, record + 0x9C),
        "air_dodge": _float_curve(image, record + 0xAC),
        "stop_keep": _float(image, record + 0xBC),  # 0x09281d -> robot +0x44f0; 0x090d21
    }


def _entry_name(image: _Image, name_id: int, field: int) -> str | None:
    if not 0 <= name_id < NAME_COUNT:
        return None
    pointer = image.u32(NAMES + name_id * NAME_SIZE + field)
    return _plain(image.raw_string(pointer)) if pointer else None


def _team_name(image: _Image, team: int) -> str | None:
    # WIRE HEADS DATA (0x013f5a) writes its own text for none, "None" or "".
    if not 0 <= team < TEAM_NAME_COUNT:
        return None
    pointer = image.u32(TEAM_NAMES + team * 4)
    name = _plain(image.raw_string(pointer)) if pointer else ""
    return name if name not in ("", "None") else None


def pilot(image: _Image, index: int, tables: GameTables) -> dict:
    """A pilot record; its first two shorts are name-table ids, the third
    its team, and +8 its bounty curve."""
    record = PILOTS + index * PILOT_SIZE
    pilot_name, robot_name = _short(image, record), _short(image, record + 2)
    return {
        "index": index,
        "name": _entry_name(image, pilot_name, 0),
        "tag": _entry_name(image, pilot_name, 4),  # the marker's name tag
        "robot_name": _entry_name(image, robot_name, 0),
        "face": _face(image, tables, pilot_name),  # chatter sprite -2
        "robot_face": _face(image, tables, robot_name),  # chatter sprite -1
        "team": _team_name(image, _short(image, record + 4)),
        "bounty": _int_curve(image, record + PILOT_BOUNTY),
    }


def part_tables(data: bytes, tables: GameTables) -> dict:
    """``{"chips", "arm_weapons", "shoulder_weapons", "bodies", "legs",
    "leg_motion_speeds", "pilots"}``."""
    image = _Image(data)
    return {
        "chips": [chip(image, index, tables) for index in range(CHIP_COUNT)],
        "arm_weapons": [weapon(image, ARM_WEAPONS, index) for index in range(ARM_WEAPON_COUNT)],
        "shoulder_weapons": [
            weapon(image, SHOULDER_WEAPONS, index) for index in range(SHOULDER_WEAPON_COUNT)
        ],
        "bodies": [body(image, index) for index in range(BODY_COUNT)],
        "legs": [legs(image, index) for index in range(LEGS_COUNT)],
        "leg_motion_speeds": [
            dict(zip(("walk", "dash"), _floats(image, LEG_MOTION_SPEEDS + model * 8, 2)))
            for model in range(COMPUTER_LEGS_MODELS)
        ],
        "pilots": [pilot(image, index, tables) for index in range(PILOT_COUNT)],
    }
