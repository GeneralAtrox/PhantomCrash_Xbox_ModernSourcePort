"""Motion banks (binlink resources of type 4): the robots' animations.

A bank holds up to 37 motions for one skeleton; every motion has the same
number of tracks, and a skeleton node names the track that drives it.

- Word 0: the track count.
- Then one file offset per motion (0: no motion), up to the first motion.
- A motion is ``tracks`` 16-byte records: a zero word, the track's flags,
  the file offset of its u16 stream and the file offset of its float stream.
- A zero stream offset means the track's channels all read 0.0.
- Flags 0x700 give the track a rotation, 0xe0 a translation and 0x3800 a
  scale; each present group is three channels (x, y, z), read in that order
  from the track's two streams (the reader at 0x0ae6c0).

A channel starts with a u16 header: ``count = h & 0x1fff``, ``kind = h &
0xe000``. Kind 0 is a constant (one float, or 0.0 with no float when the
count is 0). Otherwise ``count`` u16 key frames follow, and value 0 belongs to
frame 0. The interpolator at 0x0ab0ac reads, per kind:

========  ============  ======================================
Kind      Floats        Meaning (researcher-assigned names)
========  ============  ======================================
0x2000    count + 1     ``step``: holds each value
0x4000    count + 1     ``linear``
0x6000    count + 1     ``smooth``: smoothstep between values
0x8000    2 count + 1   ``hermite_out``: [value, tangent] pairs
0xa000    2 count + 1   ``hermite_in``: [value, tangent] pairs
0xc000    3 count + 1   ``hermite_split``: [value, out, in]
0xe000    2 count + 2   ``hermite``: [value, tangent] pairs
========  ============  ======================================

The values are written as the file has them. The port's evaluator applies
the game's axis conventions (see ``docs/ROBOTS.md``).
"""

from __future__ import annotations

import struct

TRACK_ROTATION = 0x700
TRACK_TRANSLATION = 0xE0
TRACK_SCALE = 0x3800

KINDS = {
    0x0000: "constant",
    0x2000: "step",
    0x4000: "linear",
    0x6000: "smooth",
    0x8000: "hermite_out",
    0xA000: "hermite_in",
    0xC000: "hermite_split",
    0xE000: "hermite",
}


class MotionError(ValueError):
    pass


def _float_count(kind: int, count: int) -> int:
    if kind in (0x2000, 0x4000, 0x6000):
        return count + 1
    if kind in (0x8000, 0xA000):
        return 2 * count + 1
    if kind == 0xC000:
        return 3 * count + 1
    return 2 * count + 2


def _channel(data: bytes, keys_at: int, values_at: int) -> tuple[dict, int, int]:
    header = struct.unpack_from("<H", data, keys_at)[0]
    count = header & 0x1FFF
    kind = header & 0xE000
    if kind == 0:
        if count == 0:
            return {"kind": "constant", "values": [0.0]}, keys_at + 2, values_at
        value = struct.unpack_from("<f", data, values_at)[0]
        return {"kind": "constant", "values": [value]}, keys_at + 2, values_at + 4
    keys = list(struct.unpack_from(f"<{count}H", data, keys_at + 2))
    floats = _float_count(kind, count)
    values = list(struct.unpack_from(f"<{floats}f", data, values_at))
    channel = {"kind": KINDS[kind], "keys": keys, "values": values}
    return channel, keys_at + 2 * (count + 1), values_at + 4 * floats


def _track(data: bytes, record: int) -> dict:
    _, flags, keys_at, values_at = struct.unpack_from("<4I", data, record)
    track: dict = {"flags": flags}
    for name, mask in (
        ("rotation", TRACK_ROTATION),
        ("translation", TRACK_TRANSLATION),
        ("scale", TRACK_SCALE),
    ):
        if flags & mask:
            channels = []
            if keys_at == 0 or values_at == 0:
                # No streams: the loader leaves the pointers null and every
                # channel reads 0.0.
                track[name] = [{"kind": "constant", "values": [0.0]} for _ in range(3)]
                continue
            for _ in range(3):
                channel, keys_at, values_at = _channel(data, keys_at, values_at)
                channels.append(channel)
            track[name] = channels
    return track


def _frames(motion: dict) -> int:
    last = 0
    for track in motion["tracks"]:
        for name in ("rotation", "translation", "scale"):
            for channel in track.get(name, []):
                last = max([last, *channel.get("keys", [])])
    return last


def parse(data: bytes) -> dict:
    """The bank as ``{"track_count", "motions": [None | {"frames", "tracks"}]}``."""
    if len(data) < 8:
        raise MotionError("Motion bank is too short")
    track_count = struct.unpack_from("<I", data, 0)[0]
    if not 0 < track_count < 64:
        raise MotionError(f"Unlikely track count {track_count}")
    offsets = []
    first = len(data)
    index = 1
    while 4 * index < first and 4 * index + 4 <= len(data):
        offset = struct.unpack_from("<I", data, 4 * index)[0]
        offsets.append(offset)
        if offset and offset < first:
            first = offset
        index += 1
    motions: list[dict | None] = []
    for offset in offsets:
        if offset == 0:
            motions.append(None)
            continue
        if offset + 16 * track_count > len(data):
            raise MotionError(f"Motion at {offset:#x} lies outside the bank")
        motion = {"tracks": [_track(data, offset + 16 * t) for t in range(track_count)]}
        motion["frames"] = _frames(motion)
        motions.append(motion)
    return {"track_count": track_count, "motions": motions}
