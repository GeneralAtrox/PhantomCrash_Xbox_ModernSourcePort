"""Recover editable NPC decision data from the European XBE.

Addresses and formulas: docs/ORIGINAL_ROBOT_AI.md. Names are research labels.
Run with --xbe PATH --output PATH; no emulator or executable code is invoked.
"""

import argparse
import json
import math
import struct
from pathlib import Path

from .game_tables import _Image


def recover_robot_ai(data: bytes) -> dict:
    image = _Image(data)

    def unpack(fmt: str, address: int) -> tuple:
        return struct.unpack_from(fmt, data, image.offset(address, struct.calcsize(fmt)))

    profiles = [list(unpack("<25B", 0x206038 + i * 25)) for i in range(12)]
    rows = 1 + max(max(row) for row in profiles)
    levels = []
    for ability in range(100):
        # Duration records contain six shorts; state 6 reads the first short
        # of the following record in 0x058846. Preserve that actual lookup.
        durations = unpack("<7H", 0x2062E8 + (ability + 128) * 12)
        angle = (ability * 2.6179938316 + 51.8362808228) / 99
        levels.append({
            "durations": [v / 60 for v in durations],
            "memory_seconds": ((ability + 127) ** 3 // 100000) / 30,
            "control_scale": min(1, max(0.5, (ability + 128) / 99)),
            "sight_yaw": min(math.pi, max(math.pi / 6, angle)),
            "sight_pitch": min(math.pi / 2, max(math.pi / 6, angle)),
            "max_weapon_requests": (ability + 128) ** 2 // 10000 + 1,
            "fire_probability": int((ability + 45) * 256 / 125),
            "fire_interval": ((1 / 30 - 0.5) * ability + 0.5 * 99 - 45 / 30) / 54,
            "fire_delay": (70 - ability) / 70,
            "camouflage_interval": (ability + 127) / 20,
            "camouflage_probability": int((ability * 256 + 5120) / 80),
            "camouflage_duration": min(1, max(0, (ability + 128) / 256)),
            # 0x05cce9 sets no chances below ability 1.
            "movement_chances": (list(unpack("<3H", 0x206168 + (ability // 20) * 6))
                                 if ability >= 1 else [0, 0, 0]),
        })
    search = []
    for i in range(32):
        value, = unpack("<h", 0x206EE8 + i * 2)
        if value == -1:
            break
        search.append(value)
    return {
        "source": "European default.xbe: 0x057c71 / 0x05c680 / 0x05b1ed / 0x05c905",
        "profiles": profiles,
        "decisions": [list(unpack("<5B", 0x205E88 + i * 5)) for i in range(rows)],
        "state_order": list(unpack("<5I", 0x2062A0)),
        "search_weights": search,
        "levels": levels,
        "parameters": {
            "pickup_range": 25, "pickup_money_ability": 31,
            "pickup_money_last_kind": 3, "pickup_timeout": unpack("<f", 0x19B544)[0],
            "pickup_full_speed_angle": 0.261799395,
            "pickup_slow_distance": unpack("<f", 0x19B544)[0],
            "pickup_overhead_radius": 0.5, "pickup_overhead_height": unpack("<f", 0x17CCE0)[0],
            "tick_hz": 30, "acquire_range": 100, "cloak_proximity": 5,
            "transition_guard": 4, "idle_seconds": 2, "movement_interval": 3,
            "route_retry_seconds": 4, "route_walk": 0.69, "route_dash_walk": 0.7,
            "route_turn_radians": 0.7853981634, "route_pause_seconds": 1,
            "route_pause_cycle": 4, "blocked_reverse_seconds": 0.5,
            "blocked_pause_seconds": 1, "blocked_jump_seconds": 3,
            "approach_stop_distance": unpack("<f", 0x199FAC)[0], "threat_ticks_ahead": 8.5,
            "threat_margin": unpack("<f", 0x199FB4)[0], "dodge_ability_scale": 2, "defensive_dodge_chance": 245,
            "threat_angle_base": 15, "threat_angle_max": 60,
            "probability_scale": 255, "fire_probability_scale": 256,
            "cloak_detect_scale": 99, "cloak_detect_seconds": 1,
            # 0x05de0f writes rate x 8 for the route and rate x 4 for the
            # attack, but 0x05e494 and 0x05e660 rewrite rate x 8 as they start.
            "ranker_route_seconds": 8,
            "ranker_engage_seconds": 8, "ranker_close_distance": unpack("<f", 0x19CBC0)[0],
            "idle_route_chance": 128, "melee_submode_chance": 220,
            "ranker_full_health_chance": 40, "ranker_route_attack_chance": 10,
            "ranker_orbit_chance": 10, "ranker_first_rival_orbit_chance": 50,
            "ranker_orbit_range": unpack("<f", 0x17E330)[0],
            "ranker_defensive_seconds": 180, "ranker_blocked_seconds": 2,
            "ranker_no_random_cloak": 1, "cloak_minimum": 0.1,
            "withdraw_fire_share": unpack("<f", 0x17ACCC)[0], "search_seconds": 3,
            "trace_interval": 10,
            # 0x05b1ed's +0xf8 (immediate 0x3e4ccccd): holds and delays are
            # stretched by 1 + jitter x U.
            "fire_hold_jitter": struct.unpack("<f", (0x3E4CCCCD).to_bytes(4, "little"))[0],
            # 0x05d0be / 0x05cfcc: x1.5 below cos(pi/4) (0x052a40 of 0x3f490fdb).
            "facing_fire_scale": unpack("<f", 0x199FB4)[0],
            "facing_fire_angle": math.cos(struct.unpack("<f", (0x3F490FDB).to_bytes(4, "little"))[0]),
            # 0x05ed21: beam sustain (+0x168) and the cannon | machine gun
            # flags (+0x158) of the showcase and the champions.
            "beam_sustain_ranker_seconds": unpack("<f", 0x19B544)[0],
            "beam_sustain_champion_seconds": unpack("<f", 0x17ACCC)[0],
            "forced_weapon_mask": 12,
            # 0x04f6a0: the lock (+0x130) through which a computer rival is acquired.
            "lock_range": unpack("<f", 0x19B55C)[0],
            # 0x05949b / 0x059799: the jump at a target off the sight.
            "jump_reaction_chance": 15, "jump_reaction_near": unpack("<f", 0x19B544)[0],
            "jump_reaction_far": unpack("<f", 0x199F7C)[0],
            # Immediate 0x3e860a92 in 0x05949b: 15 degrees.
            "jump_reaction_angle": struct.unpack("<f", (0x3E860A92).to_bytes(4, "little"))[0],
            "jump_reaction_aim_seconds": unpack("<f", 0x17CCE0)[0],
            "jump_reaction_skipped_area": 1,
            # 0x05d2bd / 0x05d4b7 / 0x05ec7d: retaliation. Memory is the game
            # rate x 2 + ability x rate / 10 ticks.
            "ranker_class": 2, "retaliation_ranker_chance": 10,
            "retaliation_cloaked_floor": 10, "retaliation_seen_bonus": 127,
            "retaliation_memory_seconds": 2, "retaliation_memory_per_ability": 0.1,
            "retaliation_fetch_first_kind": 4, "retaliation_fetch_last_kind": 6,
            # +0x132 = rate x 8: how long a provoked ranker waits to choose again.
            "retaliation_ranker_hold_seconds": 8,
            # 0x05f445: quest +0x618c 1 (attract demo, RESULT backdrop).
            "showcase_ability": 99,
            # 0x06e331's zone 1 (area 1's floors), where the jump is not made.
            "high_ground_centre_x": unpack("<f", 0x1B5A60)[0],
            "high_ground_centre_z": unpack("<f", 0x1B5A64)[0],
            "high_ground_radius": unpack("<f", 0x1B5A5C)[0],
            "high_ground_outside_floor": unpack("<f", 0x1B59E8)[0],
            "high_ground_inside_floor": unpack("<f", 0x17BE84)[0],
        },
        "ranker_entrants": [0, 1, 10, 19],
        "ranker_weapon_masks": [0, 0, 12, 2, 1],
        "initialization_note": ("0x080778 runs 0x057863 (0x809da), whose setters cache sight, firing, "
                                "camouflage and movement chances, before 0x057820 assigns the ability "
                                "(0x80a1c): they hold ability 0's values, or the showcase's 99."),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = recover_robot_ai(args.xbe.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Recovered {len(result['profiles'])} profiles, {len(result['decisions'])} decision rows, 100 ability levels")


if __name__ == "__main__":
    main()
