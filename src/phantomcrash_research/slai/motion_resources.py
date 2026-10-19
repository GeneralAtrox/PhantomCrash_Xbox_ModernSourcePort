"""Compiled model/motion descriptors consumed by the PAL model loader."""

import re
import struct

from .program import ProgramData

DESCRIPTOR_BYTES = 372
SOURCE_ACTIONS = 59
TABLE_POINTERS = 0x333220
ROLL_CALLBACKS = {0x4e5ad0: "0xc10", 0x4e5b60: "0xc14",
                  0x4e5bf0: "0xc18", 0x4e5c80: "0xc1c"}
SPHERICAL_CALLBACK = 0x4e5d10
# Native movement state -> original action. Other states retain the existing
# filename adapter until their original producers have been traced.
NATIVE_ACTIONS = {0: 0, 1: 2, 2: 1, 5: 4, 8: 44, 9: 45, 10: 20, 11: 22, 12: 46, 28: 47}
ACTION_PROVENANCE = {0: "0x4ea99c", 1: "0x4ea670", 2: "0x4ea754",
                     5: "0x4eac38", 8: "0x4ebc14..20", 9: "0x4ebbf0..bfc",
                     10: "0x4eb59c..5ac", 11: "0x4ef2cc",
                     12: "0x4e0b18", 28: "0x4ea1c4"}


def decode_motion_resources(program):
    # The loop bound and descriptor-index arithmetic are part of this
    # version's loader contract, rather than inferred from asset filenames.
    guards = {0x250f98: 0x2ae2003b, 0x250d24: 0x00041140,
              0x250d28: 0x00441023, 0x250d30: 0x00621023,
              0x2525e8: 0x8c460088, 0x2525e4: 0x0260282d,
              0x268364: 0x86660154, 0x2684a4: 0x86660156,
              0x2685e4: 0x86660158, 0x268724: 0x8666015a,
              0x4ef2cc: 0x24050016, 0x4e0b18: 0x2405002e,
              0x4eb59c: 0x24050014, 0x4eb5ac: 0x0c13a528,
              0x2689b4: 0x86060154,
              0x4e8fb8: 0xc60c0e18, 0x4e8ff4: 0xe6000e24,
              0x4ebbd8: 0xc6000e24, 0x4ebbe4: 0x460c0034,
              0x4ebbf0: 0x2405002d, 0x4ebbfc: 0x0c13a528,
              0x4ebc14: 0x2405002c, 0x4ebc20: 0x0c13a528,
              0x1b2a9c: 0x16420004, 0x1b2aa4: 0x0c043aca,
              0x1b56e8: 0x00c23026, 0x1b56ec: 0x0006302b,
              0x19f480: 0x14620005, 0x19f48c: 0x0c043aa0,
              0x19f4cc: 0x14620006}
    for address, word in guards.items():
        if program.read(address, 4) != struct.pack("<I", word):
            raise ValueError("Unsupported compiled S.L.A.I. motion descriptor loader")
    pointers = struct.unpack("<8I", program.read(TABLE_POINTERS, 32))
    tables = {}
    for kind, slot, letter in (("legs", 0, "L"), ("bodies", 1, "B"),
                              ("arms", 2, "A"), ("shoulders", 4, "S")):
        rows = []
        for index in range(128):
            address = pointers[slot] + DESCRIPTOR_BYTES * index
            raw = program.read(address, DESCRIPTOR_BYTES)
            if len(raw) != DESCRIPTOR_BYTES:
                break
            folder, model, prefix = [program.string(p) if p else ""
                                     for p in struct.unpack_from("<3I", raw)]
            model, prefix = model.upper(), prefix.upper()
            if not folder or not re.fullmatch(r"S[1-9A]" + letter + r"\d\dH", model):
                break
            numbers = list(struct.unpack_from("<59h", raw, 12))
            callbacks = struct.unpack_from("<51I", raw, 0x88)
            rows.append({"index": index, "address": hex(address), "folder": folder,
                         "model": model + ".PO2", "motion_prefix": prefix,
                         "motion_numbers": numbers,
                         "source_action_clips": [f"{prefix}{n:02d}" if n >= 0 else "" for n in numbers],
                         "node_callbacks": [hex(value) if value else "" for value in callbacks],
                         "attachment_nodes": list(struct.unpack_from("<16h", raw, 0x154))})
        if not rows:
            raise ValueError(f"No compiled S.L.A.I. {kind} descriptors")
        tables[kind] = rows
    return {"tables": tables, "provenance": {"table_pointers": hex(TABLE_POINTERS),
            "descriptor_stride": DESCRIPTOR_BYTES, "source_actions": SOURCE_ACTIONS,
            "filename_lookup": "0x250ed4..250f40", "loop_bound": "0x250f98",
            "node_callback_registration": "0x2524c8..252608",
            "callback_dispatch": "0x253520", "roll_update": "0x4e54b0",
            "body_attachment_lookup": ["0x268364", "0x2684a4", "0x2685e4", "0x268724"],
            "leg_body_attachment_lookup": "0x2689b4",
            "root_bind_reset": "0x1b2a9c..1b2aa8",
            "root_key_translation_exclusion": "0x1b56e8..f4;0x19f478..490",
            "attach_to_parent_node": "0x1b3590"}}


def read_motion_resources(disc):
    from .melee import melee_actions
    from .motion_transitions import decode_motion_transitions
    from .motion_sampling import decode_motion_sampling
    program = ProgramData(disc)
    result = decode_motion_resources(program)
    result['melee_actions'] = melee_actions(program)
    result['transitions'] = decode_motion_transitions(program)
    result['sampling'] = decode_motion_sampling(program)
    for row in result['tables']['bodies']:
        row['native_transition_frames'] = result['transitions']['native_transition_frames']
    return result


def native_clip_names(profile):
    prefix = profile["motion_prefix"]
    clips = [f"{prefix}{slot+1:02d}" for slot in range(37)]
    for slot, action in NATIVE_ACTIONS.items():
        clips[slot] = profile["source_action_clips"][action]
    clips.extend(profile["source_action_clips"][35:43])
    return clips


def native_motion_actions():
    actions = [-1] * 45
    for slot, action in NATIVE_ACTIONS.items():
        actions[slot] = action
    actions[37:45] = range(35, 43)
    return actions


def body_leg_profiles(resources):
    # Boss body and leg descriptor orders differ. Their shipped folder names
    # identify the common package; row ordinal alone is not a relationship.
    bodies = {row["folder"].upper(): row for row in resources["tables"]["bodies"]}
    return [(leg, bodies[leg["folder"].upper()]) for leg in resources["tables"]["legs"]]


def rolling_nodes(profile):
    """Nodes with original single-axis or spherical rolling callbacks."""
    return [node for node, callback in enumerate(profile["node_callbacks"])
            if callback and (int(callback, 0) in ROLL_CALLBACKS or
                             int(callback, 0) == SPHERICAL_CALLBACK)]


def body_attachment_nodes(profile):
    # Native right is negative X for a robot facing -Z. The source descriptor
    # lists the positive-X attachment before the negative-X attachment.
    nodes = profile["attachment_nodes"]
    return {"arm_mount_nodes": [nodes[1], nodes[0]],
            "shoulder_mount_nodes": [nodes[3], nodes[2]]}
