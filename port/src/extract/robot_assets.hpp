// The binlink models and textures the robot conversion writes besides the
// robots: the effects (effect_models.py, effect_bank.py, effect_convert.py)
// and the arena's advertising ship (arena_monitor.py).
#pragma once

#include "binlink_packs.hpp"
#include "convert_steps.hpp"
#include "game_tables.hpp"
#include "gltf.hpp"
#include "py_json.hpp"
#include "xbe.hpp"

#include <filesystem>
#include <functional>
#include <set>

namespace phantomcrash::extract {

// Writes every texture of the pack `data` to target(index) as PNG, in
// parallel; returns the texture indices in pack order.
std::vector<int> write_pack_textures(ByteView data,
                                     const std::function<std::filesystem::path(int)> &target);

// "<id>.png" for texture 0, "<id>_<index>.png" after it.
[[nodiscard]] std::string binlink_texture_name(std::uint32_t resource, int index);

// Adds a glTF summary's meshes, vertices, triangles and materials to `record`.
void put_summary(Json &record, const GltfSummary &summary);

// Model and texture resources an effect table draws from.
struct EffectResources {
  std::set<std::uint32_t> models, packs;
};

// effect_models.effect_models: flashes, projectile looks, impacts, the
// fence and decals, as members to merge into the bank.
[[nodiscard]] Json effect_pictures(const XbeImage &image, const GameTables &tables,
                                   EffectResources &resources);

// effect_bank._bank: tables/effects.json.
[[nodiscard]] Json effect_bank(const XbeImage &image, const GameTables &tables,
                               EffectResources &resources);

// effect_bank.convert_effect_bank: writes the bank, its atlas and the
// effect models.
Json convert_effect_bank(const XbeImage &image, const GameTables &tables, BinlinkPacks &packs,
                         const std::filesystem::path &output, const Progress &report);

// effect_convert.convert_effects: the entry shield.
Json convert_shield(const std::filesystem::path &media, BinlinkPacks &packs,
                    const std::filesystem::path &output, const Progress &report);

// arena_monitor.convert_ship: the advertising ship and tables/arena_monitor.json.
Json convert_arena_ship(const XbeImage &image, BinlinkPacks &packs,
                        const std::filesystem::path &output);

} // namespace phantomcrash::extract
