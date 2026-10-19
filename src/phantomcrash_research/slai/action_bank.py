"""Compiled ASB action records, with lossless storage of unresolved words.

These are content records, not executable overlays. Each bank starts with a
u16 count and has 640-byte records. Runtime 0x50ef68..f1c0 walks the active
60-byte effect descriptors starting at +0x80. Unresolved words remain intact.
"""

from pathlib import Path
import hashlib
import json
import struct

RECORD_SIZE = 640
EFFECT_OFFSET = 0x80
EFFECT_SIZE = 60
MAX_EFFECTS = 8


def _effect(row, at):
    return {
        "part_selector": struct.unpack_from("<b", row, at)[0],
        "particle": struct.unpack_from("<b", row, at+1)[0],
        "alternate_particle": struct.unpack_from("<b", row, at+2)[0],
        "parent_node": struct.unpack_from("<b", row, at+4)[0],
        "scale": struct.unpack_from("<f", row, at+8)[0],
        "parent_flags": struct.unpack_from("<I", row, at+12)[0],
        "inertia_weight": struct.unpack_from("<f", row, at+28)[0],
        "inertia_decay": struct.unpack_from("<f", row, at+32)[0],
        "front_show": struct.unpack_from("<f", row, at+36)[0],
        "position_offset": list(struct.unpack_from("<3f", row, at+40)),
        "recycle_priority": struct.unpack_from("<b", row, at+56)[0],
        "recycle_secondary": struct.unpack_from("<b", row, at+57)[0],
    }


def _trigger(row):
    motions, gates = struct.unpack_from("<b", row, 0x14)[0], struct.unpack_from("<b", row, 0x34)[0]
    if not (0 <= motions <= 16 and 0 <= gates <= 4):
        raise ValueError("Invalid ASB trigger list extent")
    return {
        "flags": struct.unpack_from("<I", row)[0],
        "motion_ids": list(struct.unpack_from(f"<{motions}b", row, 4)),
        "frame": struct.unpack_from("<f", row, 0x18)[0],
        "weapon_gates": [{"mask": struct.unpack_from("<I", row, 0x1c+i*4)[0],
                          "delay": struct.unpack_from("<h", row, 0x2c+i*2)[0]}
                         for i in range(gates)],
        "speed_threshold": struct.unpack_from("<f", row, 0x38)[0],
        "condition_selector": struct.unpack_from("<b", row, 0x3e)[0],
        "weapon_state_slot": struct.unpack_from("<b", row, 0x3f)[0],
    }


def read_action_bank(data: bytes) -> dict:
    if len(data) < 2:
        raise ValueError("Truncated ASB header")
    count = struct.unpack_from("<H", data)[0]
    if len(data) != 2 + count * RECORD_SIZE:
        raise ValueError("Unsupported ASB record extent")
    records = []
    for i in range(count):
        row = data[2+i*RECORD_SIZE:2+(i+1)*RECORD_SIZE]
        effect_count = struct.unpack_from("<I", row, 0x260)[0]
        if effect_count > MAX_EFFECTS:
            raise ValueError("Invalid ASB effect list extent")
        records.append({
            "words": list(struct.unpack("<160I", row)),
            "trigger": _trigger(row),
            "effects": [_effect(row, EFFECT_OFFSET+i*EFFECT_SIZE) for i in range(effect_count)],
        })
    return {"format": "ASB", "record_size": RECORD_SIZE, "records": records,
            "provenance": {"effect_dispatch": "0x50ebe0", "particle_selector": "0x508210",
                           "effect_constructor": "0x508250", "inertia_birth": "0x19c6f4",
                           "effect_list": "0x50ef68..50f1c0", "owner_binding": "0x50cf58..50d03c"}}


def write_action_bank(bank: dict) -> bytes:
    records = bank["records"]
    if len(records) > 65535:
        raise ValueError("Too many ASB records")
    result = bytearray(struct.pack("<H", len(records)))
    for record in records:
        row = bytearray(struct.pack("<160I", *record["words"]))
        if "trigger" in record:
            _write_trigger(row, record["trigger"])
        effects = record.get("effects", [record["effect"]] if "effect" in record else [])
        if len(effects) > MAX_EFFECTS:
            raise ValueError("Too many ASB effects")
        if "effects" in record:
            struct.pack_into("<I", row, 0x260, len(effects))
        for i, e in enumerate(effects):
            at = EFFECT_OFFSET+i*EFFECT_SIZE
            struct.pack_into("<3b", row, at, e["part_selector"], e["particle"], e["alternate_particle"])
            struct.pack_into("<b", row, at+4, e["parent_node"])
            struct.pack_into("<fI", row, at+8, e["scale"], e["parent_flags"])
            struct.pack_into("<3f", row, at+28, e["inertia_weight"], e["inertia_decay"], e["front_show"])
            struct.pack_into("<3f", row, at+40, *e["position_offset"])
            if "recycle_priority" in e:
                struct.pack_into("<b", row, at+56, e["recycle_priority"])
            if "recycle_secondary" in e:
                struct.pack_into("<b", row, at+57, e["recycle_secondary"])
        result.extend(row)
    return bytes(result)


def _write_trigger(row, trigger):
    motions, gates = trigger["motion_ids"], trigger["weapon_gates"]
    if len(motions) > 16 or len(gates) > 4:
        raise ValueError("Invalid ASB trigger list extent")
    struct.pack_into("<I", row, 0, trigger["flags"])
    struct.pack_into(f"<{len(motions)}b", row, 4, *motions)
    struct.pack_into("<b", row, 0x14, len(motions))
    struct.pack_into("<f", row, 0x18, trigger["frame"])
    for i, gate in enumerate(gates):
        struct.pack_into("<I", row, 0x1c+i*4, gate["mask"])
        struct.pack_into("<h", row, 0x2c+i*2, gate["delay"])
    struct.pack_into("<b", row, 0x34, len(gates))
    struct.pack_into("<f", row, 0x38, trigger["speed_threshold"])
    struct.pack_into("<2b", row, 0x3e, trigger["condition_selector"], trigger["weapon_state_slot"])


def bind_weapon_action_bank(bank: dict, slot: int) -> dict:
    """Apply the source loader's per-owner fixups without changing its bank."""
    import copy
    if slot not in range(4):
        raise ValueError("Invalid source weapon slot")
    result = copy.deepcopy(bank)
    for record in result["records"]:
        for effect in record["effects"]:
            effect["part_selector"] = (3, 2, 5, 4)[slot]
        trigger = record["trigger"]
        for gate in trigger["weapon_gates"]:
            if gate["mask"] == 0xff:
                gate["mask"] = (0x40, 0x80, 0x100, 0x200)[slot]
        trigger["weapon_state_slot"] = slot
    return result


def weapon_action_route(program, entries: list[dict], kind: int) -> dict:
    """0x509aec..509b14 indexes the shipped bank-name table by 0x507cb0."""
    if not 0 <= kind < 95:
        raise ValueError("Invalid source action weapon kind")
    address = 0x59ce3c+kind*4
    name = program.string(struct.unpack("<I", program.read(address, 4))[0])
    matches = [entry for entry in entries if entry["source"] == name.upper()+".ASB"]
    if len(matches) != 1:
        raise ValueError(f"Missing or ambiguous source action bank {name}")
    return {"weapon_kind": kind, "bank": "slai/assets/"+matches[0]["file"],
            "source_name": name, "name_table_address": hex(address),
            "loader_address": "0x509aec", "owner_binding": "0x50cf58..50d03c"}


def weapon_state_contract(program):
    """The ASB gate reads the part-loss state, independently of fire counters."""
    guards = {0x50eacc: 0x30a30100, 0x50ead8: 0x8284003f,
              0x50eadc: 0x24030002, 0x50eae8: 0x8c84009c,
              0x50eaec: 0x148301b6, 0x50d03c: 0xa2c20000,
              0x4e11d4: 0xae04009c, 0x4e11ec: 0x2403000f,
              0x4e11fc: 0xae02009c, 0x4e42ec: 0x3063fffe,
              0x4e42f0: 0xa48300d8, 0x4e430c: 0x3063fffe,
              0x4e4310: 0xa48300d8}
    for address, word in guards.items():
        if program.read(address, 4) != struct.pack('<I', word):
            raise ValueError(f'Unsupported source weapon state gate at {address:#x}')
    return {'flags': 0x100, 'required_state': 2,
            'owner_binding': 'row +0x3f is replaced with the owning weapon slot',
            'gate': 'actor +0x9c +4*slot == 2; each matching bank update emits',
            'lifecycle': '0x4e1130 decrements state-1 timer, enters 2, then 3 with timer 15; state 3 later disables weapon drawing callbacks and enters 4',
            'native_adapter': 'a newly lost native weapon supplies one state-2 particle update; earlier countdown and later model visibility phases remain unconnected',
            'negative_parent': 'enabled descriptors with node -1 are preserved but deferred; 0x19bf5c..64 passes the negative node to unchecked control-array getter 0x1b3c80',
            'provenance': '0x50eac8..50eaf4;0x50cf58..50d03c;0x4e1130..4e13e8;0x4e42e0;0x4e4300'}


def export_action_banks(disc, root: Path) -> list[dict]:
    """Export all package-owned ASB files, including bank variants."""
    from .resources import resource_packages
    folders = {package: folder for folder, package in resource_packages(disc).items()}
    entries = []
    for package, files in disc.packages().items():
        for file in files:
            if not file.name.endswith(".ASB"):
                continue
            raw = disc.read(file)
            bank = read_action_bank(raw)
            if write_action_bank(bank) != raw:
                raise ValueError(f"ASB round trip failed: {file.name}")
            relative = f"actions/{package:08x}_{Path(file.name).stem.lower()}.json"
            identity = {"source": file.name, "package_offset": package,
                        "resource_folder": folders.get(package, ""),
                        "sha256": hashlib.sha256(raw).hexdigest()}
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(json.dumps({**identity, **bank}, indent=2, allow_nan=False)+"\n",
                              encoding="utf-8")
            entries.append({**identity, "file": relative, "records": len(bank["records"]),
                            "effects": sum(len(row["effects"]) for row in bank["records"])})
    return entries
