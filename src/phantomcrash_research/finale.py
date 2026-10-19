"""The game's finale, read from the European ``default.xbe``.

Winning the First Ranker's contest (class 23) does not end on the RESULT
screen. The winner's victory state (``0x089861``) moves the player's robot
into robot state 0x17; leaving that state (``0x089d1b``) adds the prize,
copies the robot's armour and parts into the save (+0x10eac) and sets battle
exit code 6, which the exit dispatcher (``0x0a302a``) sends to scene 10.

Scene 10 (create ``0x01c18e``) is a scripted epilogue on stage 4, Umihotaru by
day, in battle mode 3 (save +0x618c): the robot starts in state 8 with the
carried armour (``0x080778``) and the states 8-18 play one after another
(enter ``0x08ac52``, tick ``0x08b017``, leave ``0x08b397``, which sets mode 6
after state 18). Every state has a row of 0x34 bytes in the table at
``0x242f78`` (by state - 8):

- +0x00 float: frames the robots stand frozen before the state's scene plays.
- +0x04 float: then the picture fades in over this many frames (``0x09f734``).
- +0x08 float: with this many frames of the scene left it fades out over the
  frames left (``0x09f6e0``).
- +0x0c float: on leaving, the state's last frame is captured into texture
  slot 549 (``0x0b29e0``) and dissolves over the next scene for this many
  frames, its alpha falling from 1 to 0 (``0x09fa85``).
- +0x10 int: the camera carrier's flight (``0x06789f``: the 28-byte supply
  route record ``0x21c3c8 + (route + 24) * 28``), or -1 for none.
- +0x14, +0x1c, +0x24 int and the bytes at +0x18, +0x20, +0x28: the carrier's
  three lights, each a point of the table at ``0x98cb34`` (or -1) and an on
  flag (save +0x10ef8 .. +0x10f0c); +0x29 the carrier's own flag. ``0x068db0``
  switches the carrier's effect groups (``0x0878d0``) with them: 0x41-0x44 at
  the carrier, and 0x49-0x4c, 0x45-0x48 and 0x4d-0x4e at the lights' points.
  In the port these draw as showers of paper.
- +0x2c: a script shown in the chatter window (``0x091f29``) when the scene
  reaches the frame at +0x30.

The scene's own camera and the robot's walk are the robot state's entries of
the scene table (:mod:`battle_scenes`). In state 18 the callers ring in at the
frames listed at ``0x1b5b14`` (battle sound 45 each, then every other frame
once the list ends) and in its last 60 frames sprites 3063 and 3074 blink in
the middle of the screen. Field names are researcher-assigned.
"""

from __future__ import annotations

import struct

from . import events
from .battle_scenes import SUPPLY_ROUTE, SUPPLY_ROUTES
from .game_tables import GameTables, _Image

PHASE_TABLE = 0x242F78
PHASE_SIZE = 0x34
FIRST_STATE = 8
LAST_STATE = 18
CALL_INS = 0x1B5B14
CALL_IN_LIMIT = 64
LIGHT_POINTS_INIT = 0x11E660  # pushes three points' x, y, z, last argument first
LIGHT_POINTS = 3
BANNERS = (3063, 3074)
CARRIER_FIRST_RECORD = 24  # 0x06789f: route r is supply route record 24 + r (0x21c3c8, 28 bytes)
CARRIER_ROUTES = 7
CARRIER_SPEEDS = 0x21C74C  # of a segment a frame, one float a route (0x0679e7's second argument)
SUBTITLES = 0x1DB4A0  # the ending movie's: start, end (60 Hz frames, int16) and a text pointer
SUBTITLE_SIZE = 8
SUBTITLE_LIMIT = 64


def _light_points(image: _Image) -> list[list[float]]:
    code = image.data[image.offset(LIGHT_POINTS_INIT, 96) :][:96]
    pushed = []
    index = 0
    while len(pushed) < LIGHT_POINTS * 3 and index < len(code) - 4:
        if code[index] == 0x68:  # push imm32
            pushed.append(struct.unpack_from("<f", code, index + 1)[0])
            index += 5
        else:
            index += 1
    points = []
    for point in range(LIGHT_POINTS):
        z, y, x = pushed[point * 3 : point * 3 + 3]
        points.append([round(x, 4), round(y, 4), round(z, 4)])
    return points


def _carrier_routes(image: _Image) -> list[dict]:
    """The camera carrier's routes: the supply carrier flies route r's cubic
    Bezier segments at its speed (0x06789f, 0x0679e7); their closing, drop
    and leave segments (100) are never reached."""
    routes = []
    for route in range(CARRIER_ROUTES):
        record = SUPPLY_ROUTES + (CARRIER_FIRST_RECORD + route) * SUPPLY_ROUTE
        points, segments = struct.unpack_from("<2I", image.data, image.offset(record, 8))
        (speed,) = struct.unpack_from("<f", image.data, image.offset(CARRIER_SPEEDS + route * 4, 4))
        routes.append(
            {
                "segments": [
                    [
                        [round(v, 4) for v in struct.unpack_from("<3f", image.data, image.offset(points + 48 * i + 12 * k, 12))]
                        for k in range(4)
                    ]
                    for i in range(segments)
                ],
                "speed": round(speed, 7),
            }
        )
    return routes


def _call_ins(image: _Image) -> list[int]:
    frames = []
    for index in range(CALL_IN_LIMIT):
        (frame,) = struct.unpack_from("<i", image.data, image.offset(CALL_INS + index * 4, 4))
        if frame < 0:
            break
        frames.append(frame)
    return frames


def finale(data: bytes, tables: GameTables) -> dict:
    """``{"phases", "scripts", "call_ins", "light_points", "banners", "carrier_routes"}``."""
    image = _Image(data)
    scripts: list[list[dict]] = []
    by_address: dict[int, int] = {}
    phases = []
    for state in range(FIRST_STATE, LAST_STATE + 1):
        row = image.data[image.offset(PHASE_TABLE + (state - FIRST_STATE) * PHASE_SIZE, PHASE_SIZE) :]
        wait, fade_in, fade_out, dissolve = struct.unpack_from("<4f", row, 0)
        (route,) = struct.unpack_from("<i", row, 0x10)
        lights = []
        for offset in (0x14, 0x1C, 0x24):
            (point,) = struct.unpack_from("<i", row, offset)
            lights.append({"point": point, "on": bool(row[offset + 4])})
        script_address, script_frame = struct.unpack_from("<If", row, 0x2C)
        script = None
        if script_address:
            if script_address not in by_address:
                by_address[script_address] = len(scripts)
                scripts.append(events.commands(image, tables, script_address))
            script = by_address[script_address]
        phases.append(
            {
                "state": state,
                "wait_frames": wait,
                "fade_in_frames": fade_in,
                "fade_out_frames": fade_out,
                "dissolve_frames": dissolve,
                "carrier_route": route,
                "lights": lights,
                "carrier_flag": bool(row[0x29]),
                "script": script,
                "script_frame": script_frame,
            }
        )
    return {
        "phases": phases,
        "scripts": scripts,
        "call_ins": _call_ins(image),
        "light_points": _light_points(image),
        "banners": [events.sprite_json(tables, sprite) for sprite in BANNERS],
        "carrier_routes": _carrier_routes(image),
    }


def ending_subtitles(data: bytes) -> dict:
    """The ending movie's subtitles (scene 11, 0x01c4fa): each shows while the
    movie's 60 Hz frame is in [start, end). The texts are Shift-JIS pairs
    (0x0bdd25 reads two bytes a glyph) and are written as Unicode."""
    image = _Image(data)
    entries = []
    for index in range(SUBTITLE_LIMIT):
        start, end, text = struct.unpack_from("<hhI", image.data, image.offset(SUBTITLES + index * SUBTITLE_SIZE, SUBTITLE_SIZE))
        if start < 0 or end < 0:
            break
        entries.append({"start": start, "end": end, "text": image.raw_string(text).decode("cp932", errors="replace")})
    return {"frame_rate": 60, "subtitles": entries}
