"""Robot exhaust points: resource table 0x259358, transformed by 0x084de1.

Skeleton flags 0x40000 << slot identify the animated attachment matrix.
The 0x20000 contact nodes are separate and are not exhaust mounts.
"""
from __future__ import annotations

import math
import struct

from . import game_tables


def extract(executable: bytes, catalogue: dict, packs) -> dict:
    image = game_tables._Image(executable)
    legs = []
    for index in range(len(catalogue["leg_types"])):
        resource = image.u32(0x259358 + index * 4)
        data = packs.read(resource)
        if data is None or len(data) != 14 * 28:
            raise ValueError(f"Invalid movement attachment resource {resource}")
        slots = []
        for slot in range(14):
            x, y, z, kind, dx, dy, dz = struct.unpack_from("<3fi3f", data, slot * 28)
            if not all(math.isfinite(v) for v in (x, y, z, dx, dy, dz)):
                raise ValueError(f"Non-finite movement attachment {resource}/{slot}")
            slots.append({"kind": kind, "position": [x, y, -z],
                          "direction": [dx, dy, -dz]})
        legs.append({"resource": resource, "slots": slots})
    return {"legs": legs, "groups": {"jump": 23, "descent": 24,
            "dodge": 24, "dash": [17, 18], "dash_stop": 25,
            "takeoff": 26, "takeoff_seconds": 30 / 30,
            "contact": [list(struct.unpack_from("<3i", executable,
                        image.offset(0x242BD8 + state * 12, 12))) for state in range(11)]}}
