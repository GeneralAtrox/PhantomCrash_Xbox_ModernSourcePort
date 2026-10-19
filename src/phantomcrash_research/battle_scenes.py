"""A battle's scripted entrance and exit, read from the European ``default.xbe``.

The robot state table at ``0x242d38`` (enter, tick and leave handlers per
state, run by ``0x088b69``) gives robot states 1-5 the entrance
(``0x088ffa``, ``0x08928c``, ``0x0897a2``) and state 6 the exit
(``0x089fa3``, ``0x08a161``, ``0x08a496``). A player's robot enters in one
of states 1-4, picked at random; state 5 is a computer robot's. States 8-18 are
the finale's epilogue (:mod:`finale`).

- Scene table (``0x258f50``, 12 bytes per robot state, read by
  ``0x0ae01d``, ``0x0ae267`` and ``0x0ae69e``): a pointer to the scene's
  camera, a pointer to the robot's walk (a motion track) or null, and a
  byte that makes the camera relative to where the scene starts.
- Camera: the scene's length in 60 Hz frames, a word (0xff8 in every
  scene) and the u16 and float streams of eight channels (the motion bank
  format, :mod:`motion`): roll, field of view in degrees, the eye's x, y
  and z, and the point looked at's x, y and z. ``0x0ae01d`` negates both
  z channels, and a relative camera is placed at the scene's start
  position and turned by its heading.
- Walk: a track record ``{0, flags, u16 stream, float stream}``; its
  translation moves the robot along its own frame (``0x090f8a`` keeps x
  and z, and x only for some leg types).
- Gates (``0x201598``, per stage ``{vec3 *points, u32 count, f32
  *headings}``): where robots enter (``0x0825df``) and where a robot that
  walks back in leaves the battle (``0x08224d``, ``0x044839``).
- Doors (``0x06f0ab``, ``0x070b80``): the city and training ground stage objects
  (per stage a pointer at ``0x22e638`` to 13-dword records, the object
  types at ``0x22e7e8`` and 12-triangle collision templates at
  ``0x22e658``). A record holds the effect model's global mesh slot, the
  position, the x, y and z rotation, a template index and a gate slot.
  Objects of type 6 are door leaves in pairs: the one with a gate slot
  opens that gate (``0x070667``) and turns +0.58 rad about its origin,
  its partner, the record before it, -0.58 (``0x0731c5``).
- Fixed cameras (``0x07f641``, the battle camera's view 5): per stage a
  ``{vec3 *points, u32 count}`` at ``0x232ae0``, and per stage the areas
  (``0x232bc0``: eight ``{start, end}`` point ranges, the area picked by a
  per-stage function at ``0x232be0``). The camera stands at whichever of
  the three points nearest the robot sees it.
- Supply routes (``0x066940`` picks one at random when a battle starts,
  ``0x06a2f6`` again each time the carrier has parked): per stage the route
  count at ``0x21c72c`` and, for route ``r``, a 28-byte record at
  ``0x21c3c8 + (stage + 8 r) * 28`` (``0x0679e7``): a pointer to cubic
  Bezier segments (four vec3 each, 48 bytes; ``0x067dd6``), the segment
  count, the segment where the carrier closes in (``0x067bf6``), where it
  opens its hatch and drops its load (``0x067f04``), an unused segment and
  where it turns away. The original also flies the segment just past the
  count, which is the next route's data; the converter stops at the count.

State and field names are researcher-assigned.
"""

from __future__ import annotations

import struct

from . import motion, stage_objects
from .game_tables import _Image

SCENE_TABLE = 0x258F50
# The entrance's states 1-5, the exit, 6, and the finale's states 8-18 (:mod:`finale`),
# whose cameras and walks are in world coordinates.
SCENE_STATES = (*range(1, 7), *range(8, 19))
SCENE_ENTRY = 12
CAMERA_CHANNELS = ("roll", "fov", "eye_x", "eye_y", "eye_z", "target_x", "target_y", "target_z")
GATE_TABLE = 0x201598
GATE_STAGES = 8
OBJECT_RECORDS = 0x22E638
OBJECT_TYPES = 0x22E7E8
OBJECT_COLLISION = 0x22E658
OBJECT_RECORD = 52
OBJECT_TEMPLATE = 0x1B0  # 12 triangles of three vec3
DOOR_TYPE = 6  # 0x0731c5 opens and 0x073430 closes it
# First mesh slot of each stage's effect model (resources 198-205).
DOOR_STAGES = {0: 1052, 1: 1079, 2: 1106, 3: 1122, 4: 1138, 5: 1154,
               6: 0x492, 7: 0x498}
FIXED_CAMERAS = 0x232AE0
FIXED_CAMERA_AREAS = 0x232BC0
FIXED_CAMERA_AREA_COUNT = 5
SUPPLY_ROUTES = 0x21C3C8
SUPPLY_ROUTE = 28
SUPPLY_ROUTE_COUNTS = 0x21C72C


def _channels(image: _Image, keys_address: int, values_address: int, count: int) -> list[dict]:
    keys_at = image.offset(keys_address, 2)
    values_at = image.offset(values_address, 4) if values_address else 0
    channels = []
    for _ in range(count):
        channel, keys_at, values_at = motion._channel(image.data, keys_at, values_at)
        channels.append(channel)
    return channels


def _camera(image: _Image, address: int) -> dict:
    frames, _, keys, values = struct.unpack_from("<4I", image.data, image.offset(address, 16))
    channels = _channels(image, keys, values, len(CAMERA_CHANNELS))
    return {"frames": frames, **dict(zip(CAMERA_CHANNELS, channels))}


def _walk(image: _Image, address: int) -> list[dict] | None:
    _, flags, keys, values = struct.unpack_from("<4I", image.data, image.offset(address, 16))
    if not flags & motion.TRACK_TRANSLATION or not keys:
        return None
    skipped = 3 if flags & motion.TRACK_ROTATION else 0
    return _channels(image, keys, values, skipped + 3)[skipped:]


def _gates(image: _Image, stage: int) -> list[dict]:
    points, count, headings = struct.unpack_from(
        "<3I", image.data, image.offset(GATE_TABLE + stage * 12, 12)
    )
    gates = []
    for index in range(count):
        position = struct.unpack_from("<3f", image.data, image.offset(points + index * 12, 12))
        heading = struct.unpack_from("<f", image.data, image.offset(headings + index * 4))[0]
        gates.append({"position": [round(v, 4) for v in position], "heading": round(heading, 4)})
    return gates


def _door_leaf(image: _Image, stage: int, index: int, first_model: int) -> dict:
    record = image.u32(OBJECT_RECORDS + stage * 4) + index * OBJECT_RECORD
    model = image.u32(record)
    position = struct.unpack_from("<3f", image.data, image.offset(record + 8, 12))
    rotation = struct.unpack_from("<3f", image.data, image.offset(record + 0x14, 12))
    template, gate = struct.unpack_from("<2i", image.data, image.offset(record + 0x2C, 8))
    kind = struct.unpack_from(
        "<i", image.data, image.offset(image.u32(OBJECT_TYPES + stage * 4) + template * 4)
    )[0]
    triangles_at = image.u32(OBJECT_COLLISION + stage * 4) + template * OBJECT_TEMPLATE
    collision = [
        [round(v, 4) for v in struct.unpack_from("<9f", image.data, image.offset(at, 36))]
        for at in range(triangles_at, triangles_at + OBJECT_TEMPLATE, 36)
    ]
    return {
        "kind": kind,
        "gate": gate,
        "mesh": model - first_model,
        "position": [round(v, 4) for v in position],
        "rotation": [round(v, 4) for v in rotation],
        "collision": collision,
    }


def _doors(image: _Image, stage: int) -> list[dict]:
    first_model = DOOR_STAGES[stage]
    leaves = [_door_leaf(image, stage, record["index"], first_model)
              for record in stage_objects.records(image, stage)]
    doors = []
    for index, leaf in enumerate(leaves):
        if leaf["kind"] != DOOR_TYPE or leaf["gate"] < 0 or index == 0:
            continue
        partner = leaves[index - 1]
        if (partner["kind"] != DOOR_TYPE or partner["position"] != leaf["position"]
                or partner["rotation"] != leaf["rotation"]):
            raise ValueError(f"Gate leaves do not share a transform on stage {stage}")
        doors.append(
            {
                "gate": leaf["gate"],
                "position": leaf["position"],
                "rotation": leaf["rotation"],
                "leaves": [
                    {"mesh": partner["mesh"], "swing": -1, "collision": partner["collision"]},
                    {"mesh": leaf["mesh"], "swing": 1, "collision": leaf["collision"]},
                ],
            }
        )
    return doors


def _fixed_cameras(image: _Image, stage: int) -> dict:
    points, count = struct.unpack_from("<2I", image.data, image.offset(FIXED_CAMERAS + stage * 8, 8))
    areas = struct.unpack_from("<I", image.data, image.offset(FIXED_CAMERA_AREAS + stage * 4))[0]
    ranges = struct.unpack_from(
        f"<{2 * FIXED_CAMERA_AREA_COUNT}I", image.data, image.offset(areas, 8 * FIXED_CAMERA_AREA_COUNT)
    )
    return {
        "points": [
            [round(v, 4) for v in struct.unpack_from("<3f", image.data, image.offset(points + i * 12, 12))]
            for i in range(count)
        ],
        "areas": [list(ranges[i : i + 2]) for i in range(0, len(ranges), 2)],
    }


def _supply_routes(image: _Image, stage: int) -> list[dict]:
    count = struct.unpack_from("<i", image.data, image.offset(SUPPLY_ROUTE_COUNTS + stage * 4))[0]
    routes = []
    for route in range(count):
        record = SUPPLY_ROUTES + (stage + GATE_STAGES * route) * SUPPLY_ROUTE
        points, segments, closing, drop, _unused, leave, _ = struct.unpack_from(
            "<7I", image.data, image.offset(record, SUPPLY_ROUTE)
        )
        routes.append(
            {
                "segments": [
                    [
                        [round(v, 4) for v in struct.unpack_from("<3f", image.data, image.offset(points + 48 * i + 12 * k, 12))]
                        for k in range(4)
                    ]
                    for i in range(segments)
                ],
                "closing": closing,
                "drop": drop,
                "leave": leave,
            }
        )
    return routes


def battle_scenes(data: bytes) -> dict:
    """``{"states", "gates", "doors", "fixed_cameras", "supply_routes"}``, each but the states by stage.

    A state has ``frames``, ``relative``, ``camera`` (the eight channel
    names to channels) and ``walk`` (the translation's x, y and z channels,
    or null when the robot stands). A door has its ``gate``, ``position``
    and ``rotation`` and two ``leaves``, each an effect-model ``mesh``, the
    way it ``swing``s open and its local ``collision`` triangles. A stage's
    fixed cameras are its ``points`` and its ``areas`` (point ranges). A
    supply route has its ``segments`` (four points each) and the segments
    where the carrier is ``closing`` in, makes its ``drop`` and turns to
    ``leave``.
    """
    image = _Image(data)
    states = {}
    for state in SCENE_STATES:
        entry = SCENE_TABLE + state * SCENE_ENTRY
        camera, walk = struct.unpack_from("<2I", image.data, image.offset(entry, 8))
        relative = image.data[image.offset(entry + 8, 1)]
        if not camera:
            continue
        scene = _camera(image, camera)
        states[str(state)] = {
            "frames": scene.pop("frames"),
            "relative": bool(relative),
            "camera": scene,
            "walk": _walk(image, walk) if walk else None,
        }
    gates = {str(stage): _gates(image, stage) for stage in range(GATE_STAGES)}
    doors = {str(stage): _doors(image, stage) for stage in DOOR_STAGES}
    cameras = {str(stage): _fixed_cameras(image, stage) for stage in range(GATE_STAGES)}
    supply = {str(stage): _supply_routes(image, stage) for stage in range(GATE_STAGES)}
    return {"states": states, "gates": gates, "doors": doors, "fixed_cameras": cameras, "supply_routes": supply}
