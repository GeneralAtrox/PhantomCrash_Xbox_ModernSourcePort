"""Recover shipped cue routing; never execute original sound handlers."""

import struct
from .effect_routes import impact_route
from .fire_sound import fire_cycles, fire_gate, fire_sustain


def constant_case(program, address, default=-1):
    """A switch leaf branches to return, assigning V0 in its delay slot."""
    branch, delay = struct.unpack("<2I", program.read(address, 8))
    if branch == 0x03E00008 and delay == 0:  # JR RA, NOP
        return default
    if branch >> 16 == 0x2402 and delay == 0:
        return struct.unpack("<h", struct.pack("<H", branch & 65535))[0]
    if branch >> 26 != 4 or (branch >> 16) & 1023:
        raise ValueError(f"Unsupported sound switch leaf at {address:#x}")
    if delay == 0:
        return default
    if delay == 0x0000102D:  # DADDU V0,ZERO,ZERO
        return 0
    if delay >> 16 != 0x2402:  # ADDIU V0,ZERO,constant
        raise ValueError(f"Nonconstant sound switch leaf at {address:#x}")
    return struct.unpack("<h", struct.pack("<H", delay & 65535))[0]


def recover_cues(program, banks):
    cycles = fire_cycles(program)
    sustained = fire_sustain(program)
    cues = []
    for index in range(256):
        raw = program.read(0x3085F0 + index * 18, 18)
        bank, sequence, priority, kind, group, reserved = struct.unpack_from("<6B", raw)
        if bank >= len(banks) or sequence >= len(banks[bank]["sequences"]):
            raise ValueError(f"Cue {index} references a missing source sequence")
        cues.append({"id": index, "bank": bank, "sequence": sequence,
                     "priority": priority, "kind": kind, "group": group,
                     "reserved": reserved, "raw_words": list(struct.unpack("<9H", raw)),
                     "source_address": hex(0x3085F0 + index * 18)})
    weapon_cues = {}
    for category, table, count in (("arm", 0x5BAE10, 101), ("shoulder", 0x5BAC60, 106)):
        targets = struct.unpack(f"<{count}I", program.read(table, count * 4))
        rows = []
        for index, target in enumerate(targets):
            if not 0x507CE8 <= target <= 0x508008:
                raise ValueError("Unsupported weapon sound selector")
            kind = constant_case(program, target)
            cue = -1
            if 0 <= kind < 95:
                leaf = struct.unpack("<I", program.read(0x5BAFB0 + kind * 4, 4))[0]
                if not 0x50AC18 <= leaf <= 0x50AEE8:
                    raise ValueError("Unsupported fire cue selector")
                cue = constant_case(program, leaf)
            rows.append({"record": index, "weapon_kind": kind, "fire_cue": cue,
                         "fire_cycle": cycles.get(cue),
                         "fire_sustain": sustained.get(cue),
                         "impact": impact_route(program, category, index, kind),
                         "selector_address": hex(target)})
        weapon_cues[category] = rows
    return {"cues": cues, "weapons": weapon_cues, "fire_gate": fire_gate(program),
            "provenance": {"cue_table": "0x3085f0", "weapon_selector": "0x507cb0",
                           "fire_selector": "0x50abf0", "playback": "0x21f430",
                           "weapon_playback": "0x511050"}}
