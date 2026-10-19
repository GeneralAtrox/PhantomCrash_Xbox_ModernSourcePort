// The battle's tables the robot conversion writes beside the models: mounts,
// lessons, enemy paths, environments, parts, chatter, scenes and finale.
#pragma once

#include "binlink_packs.hpp"
#include "game_tables.hpp"
#include "py_json.hpp"
#include "xbe.hpp"

namespace phantomcrash::extract {

// Weapon muzzle points and model-node chains (weapon_mounts.py: 0x081863,
// 0x09a9d4). {"weapons": {"models/binlink/<id>.gltf": {...}}}.
[[nodiscard]] Json weapon_mounts(const XbeImage &image, const Json &catalogue,
                                 BinlinkPacks &packs);
// Exhaust points (movement_mounts.py: 0x259358, 0x084de1).
[[nodiscard]] Json movement_mounts(const XbeImage &image, ByteView executable,
                                   const Json &catalogue, BinlinkPacks &packs);

// Pepper's in-battle lesson messages (battle_data.lesson_messages).
[[nodiscard]] Json lesson_messages(const XbeImage &image, const GameTables &tables);
// An ENPT enemy path file (battle_data.enemy_path).
[[nodiscard]] Json enemy_path(ByteView data);
// Each stage's light direction and fog/light presets (environment.py).
[[nodiscard]] Json stage_environments(const XbeImage &image);
// The battle chatter (chatter.py).
[[nodiscard]] Json chatter_tables(const XbeImage &image, const GameTables &tables);
// AI chips, weapons, bodies, legs and pilots (parts.py).
[[nodiscard]] Json part_tables(const XbeImage &image, const GameTables &tables);
// Entrance/exit scenes, gates, doors, fixed cameras, supply routes
// (battle_scenes.py).
[[nodiscard]] Json battle_scenes(const XbeImage &image);
// The epilogue after the First Ranker's contest (finale.finale).
[[nodiscard]] Json finale_tables(const XbeImage &image, const GameTables &tables);
// The ending movie's subtitles (finale.ending_subtitles).
[[nodiscard]] Json ending_subtitles(const XbeImage &image);

// battle_scenes' supply route records, shared with the finale.
inline constexpr std::uint32_t supply_routes = 0x21C3C8;
inline constexpr std::uint32_t supply_route_size = 28;

// Cubic Bezier segments: `count` x four vec3 at `points`, rounded to 4.
[[nodiscard]] Json bezier_segments(const XbeImage &image, std::uint32_t points,
                                   std::uint32_t count);

} // namespace phantomcrash::extract
