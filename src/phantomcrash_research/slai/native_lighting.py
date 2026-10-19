"""Native lighting from VLIGHT's source sun vector and light-volume records.

0x51affc rotates (0,3,0) about X/Y/Z (0x1aaa70). 0x51b0a8 loads
point/line/rectangle volumes; 0x1b4b80 selects the nearest two. Their
colour fades by 1-distance²/radius² at 0x1caed8.
"""

import math
import struct

import numpy as np

NATIVE_AXES = np.array([1., -1., -1.])


def source_rotation(angles):
    x, y, z = angles[:3]
    sx, cx, sy, cy, sz, cz = math.sin(x), math.cos(x), math.sin(y), math.cos(y), math.sin(z), math.cos(z)
    return (np.array([[1, 0, 0], [0, cx, sx], [0, -sx, cx]]) @
            np.array([[cy, 0, -sy], [0, 1, 0], [sy, 0, cy]]) @
            np.array([[cz, sz, 0], [-sz, cz, 0], [0, 0, 1]]))


def native_lighting(light, scale=1.0):
    direction = np.array([0., 1., 0.]) @ source_rotation(light["sun_angles_source"])
    locals = []
    for record in light["records"]:
        radius, width, depth, bits = record["parameters_source"]
        shape = struct.unpack("<I", struct.pack("<f", bits))[0]
        if shape not in (0, 1, 2) or radius <= 0:
            raise ValueError("Unsupported VLIGHT volume")
        rotation = source_rotation(record["rotation_source"])
        locals.append({"position": (np.array(record["position_source"][:3]) * NATIVE_AXES * scale).tolist(),
                       "colour": record["colour_source"][:3], "radius": radius * scale,
                       "half_x": (rotation[0] * width * scale * NATIVE_AXES).tolist() if shape else [0, 0, 0],
                       "half_z": (rotation[2] * depth * scale * NATIVE_AXES).tolist() if shape == 2 else [0, 0, 0],
                       "source_shape": shape})
    return {"ambient": light["ambient"][:3], "sun_colour": light["sun_colour"][:3],
            "sun_direction": (direction * NATIVE_AXES).tolist(), "source_lights": locals}
