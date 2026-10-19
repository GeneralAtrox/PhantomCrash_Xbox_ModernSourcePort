"""P2MT motion assets, decoded independently of the native game's motion system.

SLES_529.40: 0x19fcb0 computes key stride in quadwords; 0x19fd00 reads
durations; 0x19fec0 and 0x19ff40 divide signed shorts by 1000. Quaternion
order WXYZ follows matrix conversion at 0x1a05f0 and 0x1a06b0. Names for
these functions are researcher descriptions, not original symbols.
"""

from __future__ import annotations

import struct


def read_motion(data: bytes) -> dict:
    if data[:4] != b"P2MT" or struct.unpack_from("<I", data, 4)[0] != 70:
        raise ValueError("Unsupported P2MT version")
    count = struct.unpack_from("<I", data, 8)[0]
    if 16 + count * 80 > len(data):
        raise ValueError("Truncated motion tracks")
    tracks = []
    for index in range(count):
        at = 16 + index * 80
        offset, keys, flags = struct.unpack_from("<3I", data, at)
        compressed = bool(flags & 16)
        stride = 16 if compressed else 16 * (
            1 + bool(flags & 3) + bool(flags & 4) + bool(flags & 8))
        if keys and (offset < 16 + count * 80 or offset + keys * stride > len(data)):
            raise ValueError(f"Track {index} has invalid key bounds")
        track = {"node": index, "flags": flags,
                 "bind_matrix": list(struct.unpack_from("<16f", data, at + 16)),
                 "keys": []}
        frame = 0
        for key in range(keys):
            pos = offset + key * stride
            duration = struct.unpack_from("<H" if compressed else "<I", data, pos)[0]
            value = {"frame": frame, "duration": duration}
            if compressed:
                if flags & 4:
                    value["translation"] = [x / 1000 for x in struct.unpack_from("<3h", data, pos + 2)]
                if flags & 1:
                    value["quaternion_wxyz"] = [x / 1000 for x in struct.unpack_from("<4h", data, pos + 8)]
            else:
                cursor = pos + 16
                if flags & 3:
                    field = "quaternion_wxyz" if flags & 1 else "rotation_euler"
                    value[field] = list(struct.unpack_from("<4f", data, cursor))
                    cursor += 16
                if flags & 4:
                    value["translation"] = list(struct.unpack_from("<3f", data, cursor))
                    cursor += 16
                if flags & 8:
                    value["scale"] = list(struct.unpack_from("<3f", data, cursor))
            track["keys"].append(value)
            frame += duration
        track["duration_frames"] = frame
        tracks.append(track)
    return {"format": "P2MT", "version": 70, "time_unit": "source_frame",
            "quaternion_order": "wxyz", "translation_units": "source_model_unit",
            "tracks": tracks}
