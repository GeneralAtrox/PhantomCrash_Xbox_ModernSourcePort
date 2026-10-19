"""Source impact selectors and the shared PTK resource pool."""

import struct
from pathlib import Path


def impact_target_translation(particle):
    # Both impact constructors select parent flags 0x400 for these resource IDs
    # when a struck model is supplied (0x508a14..84 / 0x508cb0..d34).
    # 0x19c0b8..198 retains the world offset, follows translation and returns
    # an identity parent frame, rather than inheriting joint orientation.
    return particle in (18, 19, 20, 28, 30, 38, 40, 42, 44, 54)


def impact_attachment_contract(program):
    # The impact dispatch supplies the struck actor's first part (legs), and
    # both constructors use control zero and the same two attachment masks.
    expected = {
        0x501E20: 0x0C0890B0, 0x501E24: 0x8E640128, 0x501E28: 0x8C5E01A4,
        0x50FEF8: 0x0000482D, 0x508A7C: 0x24030400, 0x508A8C: 0x2403380C,
        0x508D5C: 0x2403380C, 0x1B3C9C: 0x24450050,
    }
    for address, instruction in expected.items():
        if struct.unpack('<I', program.read(address, 4, overlay='battledvd.bin'))[0] != instruction:
            raise ValueError('Unsupported source impact model attachment path')
    return {'target_part': 'legs', 'control': 0,
            'translation_flags': 0x400, 'model_flags': 0x380C,
            'provenance': {'target_model': '0x501e20..28', 'control': '0x50fef8',
                           'matrix': '0x1b3c80..9c', 'mask': '0x508a14..98/0x508cb0..d64'}}


def impact_route(program, category, record, kind):
    base = 0x3CF480 if category == "arm" else 0x3D70D0
    phase = program.read(base + record * 312 + 0x1D, 1)[0]
    # 0x501c80 passes record +0x1d to 0x50f9f0. The latter switches
    # through 0x5bb1b0 and starts the selected sound and PTK together.
    particles = [19, 19, 44, 45, 53, 28, 40, 42, 30, 38, 54, 55]
    if phase >= len(particles):
        raise ValueError("Unsupported source impact phase")
    cues = [None, None, 69, 69, 73, 73,
            251 if category == "shoulder" else 212,
            248 if category == "shoulder" else 229 if kind == 85 else 210,
            175 if kind == 16 else 176,
            183 if kind == 32 else 181 if kind == 64 else 178, 179, 252]
    translated = impact_target_translation(particles[phase])
    return {"phase": phase, "particle": particles[phase], "cue": cues[phase],
            "target_translation": impact_target_translation(particles[phase]),
            "target_flags": 0x400 if translated else 0x380C, "target_control": 0,
            "robot_cues": [cues[phase]] if cues[phase] else [72],
            "surface_cues": [cues[phase]] if cues[phase] else [57, 58, 59],
            "surface_weights": [1] if cues[phase] else [55, 15, 30],
            "provenance": {"record_field": hex(base + record * 312 + 0x1D),
                           "dispatch": "0x501c20", "selector": "0x50f9f0",
                           "target_frame": "0x19c0b8" if translated else "0x19bf10..c09c"}}


def particle_pool(program):
    pointers = struct.unpack("<60I", program.read(0x59BE30, 240))
    names = [program.string(pointer) for pointer in pointers]
    if any(not name or "/" not in name for name in names):
        raise ValueError("Unsupported source PTK resource pool")
    return [{"id": i, "source_path": name,
             "file": f"slai/assets/effects/{Path(name).name.lower()}.json"}
            for i, name in enumerate(names)]
