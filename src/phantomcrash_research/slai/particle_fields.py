"""Verified PTK generator fields, separate from unresolved update behavior.

Offsets are relative to the 768-byte parameter block after a generator name.
The source loader at 0x19ddb0 builds runtime generators; 0x198d80 births a
particle, 0x199750 updates it, and 0x19c4f0 accumulates emissions. Times and
distances retain source units until the caller's timestep has been verified.
"""

import struct


def generator_fields(words):
    raw = struct.pack("<192I", *words)

    def value(offset):
        return struct.unpack_from("<f", raw, offset)[0]

    def vector(offset):
        return list(struct.unpack_from("<3f", raw, offset))

    def spread(offset):
        center, radius = struct.unpack_from("<2f", raw, offset)
        return {"center": center, "spread": radius,
                "bounds": [center - radius, center + radius]}

    blend = words[39]
    return {
        "flags": words[0],
        "enabled": bool(words[0] & 1),
        "billboard": bool(words[0] & 0x10000),
        "velocity_aligned": bool(words[0] & 0x100),
        "depth_test": bool(words[0] & 0x10),
        "alpha_test": "greater than zero; GS TEST at 0x19cba0",
        "maximum_live_particles": value(0x14),
        "emission_rate_source_units": value(0x18),
        "emission_start_source_time": value(0x1c),
        "emission_stop_source_time": value(0x20),
        "spawn_shape_code": words[9],
        "spawn_dimensions": vector(0x28),
        "initial_speed": spread(0x34),
        "direction_angle_degrees": spread(0x3c),
        "spawn_offset": vector(0x44),
        "rotation_degrees": vector(0x50),
        "rotation_birth_order": "X from second angle, then Y from first; 0x199388..1993e4",
        "third_rotation_used_at_birth": False,
        "spawn_offset_rule": "preflip Y/Z, then add after generator rotation; 0x19df4c..74 and 0x199448..78",
        "lifetime_source_units": spread(0x5c),
        "gravity": spread(0x64),
        "gravity_end": value(0xc4),
        "gravity_curve": words[50],
        "render_size": vector(0x70),
        "size_start": value(0x7c),
        "size_end": value(0x80),
        "size_curve": words[45],
        "colour_curve": words[46],
        "alpha_curve": words[47],
        "rotation_range_radians": value(0x90),
        "rotation_range_wrap": "fmod(authored range + pi, 2*pi) - pi; 0x19e570",
        "rotation_birth_distribution": "uniform from negative wrapped range to positive wrapped range; 0x198e60",
        "spin_degrees": spread(0x94),
        "spin_distribution": "uniform every update; 0x199e94..eac",
        "fade_delay_source_time": value(0x1d0),
        "velocity_multiplier_per_tick": value(0x1d4),
        "colour_a_bytes": list(raw[0x84:0x88]),
        "colour_b_bytes": list(raw[0x88:0x8c]),
        "atlas_frame_period_source_time": value(0x8c),
        "atlas_lifetime_curve": words[51],
        "texture_strip": bool(words[0] & 0x20000),
        "atlas_timed": bool(words[0] & 0x40000),
        "atlas_random": bool(words[0] & 0x80000),
        "textured_vertex_rgb_multiplier": .5,
        "vertex_alpha_multiplier": .5,
        "blend_code": blend,
        # GS ALPHA values emitted at 0x19cc30/50/70/90: 0x0a/44/48/42.
        "blend_equation": {0: "source", 1: "(source-destination)*source_alpha+destination",
                           2: "source*source_alpha+destination",
                           3: "destination-source*source_alpha"}.get(blend, "unresolved"),
        "texture_path": raw[0xd0:0x1d0].split(b"\0", 1)[0].decode("cp932", errors="replace"),
        "source_units_verified": True,
        "time_unit": "update tick; 0x19a518 sets the timestep to 1",
        "provenance": {"loader": "0x19ddb0", "birth": "0x198d80",
                       "emission": "0x19c4f0", "update": "0x199750",
                       "blend": "0x19cbf8", "texture_dimensions": "0x19b45c",
                       "atlas_birth": "0x198de0", "atlas_update": "0x199d58",
                       "atlas_uv": "0x19d2e8", "colour_submit": "0x19d1fc",
                       "generator_enable": "0x19c1dc", "quad_orientation": "0x19cf80",
                       "range_wrap": "0x19e570", "spin_update": "0x199e94",
                       "spawn_geometry": "0x198f10", "spawn_direction": "0x199208",
                       "spawn_basis": "0x199488", "quad_vertices": "0x2b9b10"},
    }
