"""Separate the original robot mesh scale from source world-space lengths."""

import json
import struct
from pathlib import PurePosixPath

from .assets import save
from .model import SCALE
from .native_winding import port_winding


def world_units(program):
    """The part loader scales slot 0 (legs); attached slots inherit its scale.

    Native robot exports bake .1 into vertices and PMT translations. A source
    world length therefore converts by exported_mesh_scale / original_root_scale,
    rather than by the exported mesh scale itself. The other five switch entries
    bypass this root scaling call. Stage MODEL_SCALE directives remain separate.
    """
    branches = struct.unpack("<6I", program.read(0x41ECA0, 24))
    if branches != (0x2526C0, *([0x252750] * 5)):
        raise ValueError("Unsupported source part root-scale dispatch")
    if program.read(0x252704, 4) != struct.pack("<I", 0x0C072A34):
        raise ValueError("Unsupported source part root-scale setter")
    words = struct.unpack("<9I", program.read(0x2526DC, 36))
    if words != (0x3C043DCC, 0x3483CCCD, 0x44836000,
                 0x3C043DCC, 0x3483CCCD, 0x44836800,
                 0x3C043DCC, 0x3483CCCD, 0x44837000):
        raise ValueError("Unsupported source robot mesh scale")
    original = struct.unpack("<f", struct.pack("<I", (words[0] & 0xFFFF) << 16 | words[1] & 0xFFFF))[0]
    geometry = abs(float(SCALE[0]))
    world = geometry / original
    return {"part_root_scale": original, "canonical_geometry_scale": geometry,
            "world_scale": world, "canonical_to_world": world / geometry,
            "provenance": {"root_scale": "0x2526dc..252704",
                           "root_slot_switch": "0x41eca0", "part_loader": "0x2524c8"}}


def restore_world_units(model, units):
    """Compose one world conversion above all canonical model/prop nodes."""
    scene = model["scenes"][model.get("scene", 0)]
    index = len(model["nodes"])
    model["nodes"].append({"name": "slai_source_world_units",
                           "scale": [units["canonical_to_world"]] * 3,
                           "children": list(scene["nodes"])})
    scene["nodes"] = [index]
    model.setdefault("extras", {})["slai_world_units"] = units


def particle_world_units(program):
    """PTK distances are world lengths; full-frame attachments retain mesh scale."""
    guards = {0x19c040: 0x32024000, 0x19c04c: 0x0c043ab0,
              0x19c050: 0x27a50100, 0x19a668: 0xe48c0080,
              0x19a698: 0xc64d0084, 0x19d1bc: 0x0200202d,
              0x19d1c0: 0x0c043a0c}
    for address, word in guards.items():
        if program.read(address, 4) != struct.pack('<I', word):
            raise ValueError('Unsupported S.L.A.I. particle world-scale path')
    return world_units(program)


def export_world_model(mods, source, units):
    """Native static world copy; sibling folders preserve texture references."""
    model = json.loads((mods / source).read_text(encoding="utf-8"))
    port_winding(model, False)
    restore_world_units(model, units)
    destination = "slai/assets/native_world/" + PurePosixPath(source).name
    save(mods / destination, model)
    return destination
