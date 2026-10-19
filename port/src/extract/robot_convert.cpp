// Converts the robots and everything else in the binlink packs
// (robot_convert.py): part tables, skeletons, models, paints, motions, the
// battle tables, effects, collision and the city stages.
#include "binlink_packs.hpp"
#include "convert_steps.hpp"
#include "model.hpp"
#include "motion.hpp"
#include "parallel.hpp"
#include "robot_assets.hpp"
#include "robot_tables.hpp"
#include "robots.hpp"
#include "stage_tables.hpp"
#include "text.hpp"
#include "xbox_texture.hpp"

#include <format>
#include <map>
#include <optional>

namespace phantomcrash::extract {
namespace {

namespace fs = std::filesystem;

// Robots' paint slots (0x20a..0x20f): a batch's texture past its own paint
// reads another robot's.
constexpr std::size_t robot_paint_slots = 6;
// Binlink models with their texture packs: the cockpit, the supply carrier
// with its pickups (day and night) and the robots' ground shadows.
constexpr std::pair<std::uint32_t, std::uint32_t> hud_models[]{
    {184, 1555}, {207, 1584}, {208, 1585}, {216, 1590}};
constexpr std::uint32_t stage_collision = 0x2212CC; // u32 collision resource per stage
constexpr std::size_t stage_count = 8;

std::string lower_ascii(std::string text) {
  for (char &c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return text;
}

// Pack -> the first paint of a legs type in it (a line's parts share one UV layout).
std::map<std::uint32_t, std::int64_t> default_paints(const Json &catalogue, const BinlinkPacks &packs) {
  std::map<std::uint32_t, std::int64_t> paints;
  for (const Json &legs : field(catalogue, "leg_types").array()) {
    const auto pack = packs.pack_of(static_cast<std::uint32_t>(json_int(field(legs, "model"))));
    const auto &list = field(legs, "paints").array();
    if (pack && !list.empty()) {
      paints.try_emplace(*pack, json_int(list.front()));
    }
  }
  return paints;
}

Json convert_models(const RobotResources &wanted, const Json &catalogue, BinlinkPacks &packs,
                    const fs::path &output, const Progress &report) {
  const auto paints = default_paints(catalogue, packs);
  std::set<std::int64_t> emblem_models;
  for (const Json &body : field(catalogue, "body_types").array()) {
    emblem_models.insert(json_int(field(body, "emblem_model")));
  }
  const std::int64_t first_emblem = json_int(field(catalogue, "emblems").array().at(0));
  const std::vector<std::uint32_t> ids(wanted.models.begin(), wanted.models.end());
  std::vector<std::optional<Json>> records(ids.size());
  parallel_for(ids.size(), [&](const std::size_t k) {
    const std::uint32_t id = ids[k];
    const auto data = packs.read(id);
    if (!data) {
      return;
    }
    std::optional<std::int64_t> texture;
    if (emblem_models.contains(id)) {
      texture = first_emblem;
    } else if (const auto pack = packs.pack_of(id)) {
      if (const auto found = paints.find(*pack); found != paints.end()) {
        texture = found->second;
      }
    }
    std::vector<std::string> uris;
    if (texture) {
      uris.assign(robot_paint_slots, std::format("../../textures/binlink/{}.png", *texture));
    }
    const auto summary =
        write_gltf(parse_model(*data), output / "models" / "binlink" / std::format("{}.gltf", id), uris, 1);
    Json record = object({{"resource", integer(id)}, {"paint", texture ? integer(*texture) : null()}});
    put_summary(record, summary);
    records[k] = std::move(record);
  });
  Json out = array();
  for (auto &record : records) {
    if (record) {
      push(out, std::move(*record));
    }
  }
  report(std::format("{:>4} robot models", out.array().size()));
  return out;
}

Json convert_hud_models(BinlinkPacks &packs, const fs::path &output, const Progress &report) {
  Json records = array();
  for (const auto &[id, pack_id] : hud_models) {
    const auto data = packs.read(id);
    const auto textures = packs.read(pack_id);
    if (!data || !textures) {
      continue;
    }
    std::vector<std::string> uris;
    for (const int index : write_pack_textures(*textures, [&](int i) {
           return output / "textures" / "binlink" / binlink_texture_name(pack_id, i);
         })) {
      uris.push_back("../../textures/binlink/" + binlink_texture_name(pack_id, index));
    }
    const auto summary =
        write_gltf(parse_model(*data), output / "models" / "binlink" / std::format("{}.gltf", id), uris);
    Json record = object({{"resource", integer(id)}, {"textures", integer(pack_id)}});
    put_summary(record, summary);
    push(records, std::move(record));
  }
  report(std::format("{:>4} HUD models", records.array().size()));
  return records;
}

// Writes each resource's pack with target(resource, index); counts textures.
template <typename Target>
std::size_t convert_packs(const std::vector<std::uint32_t> &ids, BinlinkPacks &packs, Target target) {
  std::atomic<std::size_t> written{0};
  parallel_for(ids.size(), [&](const std::size_t k) {
    if (const auto data = packs.read(ids[k])) {
      written += write_pack_textures(*data, [&](int index) { return target(ids[k], index); }).size();
    }
  });
  return written;
}

std::size_t convert_motions(const RobotResources &wanted, BinlinkPacks &packs, const fs::path &output,
                            const Progress &report) {
  const std::vector<std::uint32_t> ids(wanted.motions.begin(), wanted.motions.end());
  std::atomic<std::size_t> written{0};
  parallel_for(ids.size(), [&](const std::size_t k) {
    if (const auto data = packs.read(ids[k])) {
      write_compact_json(output / "motions" / std::format("{}.json", ids[k]), parse_motion_bank(*data));
      ++written;
    }
  });
  report(std::format("{:>4} motion banks", written.load()));
  return written;
}

Json convert_collision(const XbeImage &image, const GameTables &tables, const fs::path &media,
                       BinlinkPacks &packs, const fs::path &output, const Progress &report) {
  const auto stage_ids = image.u32s(stage_collision, stage_count);
  const std::set<std::uint32_t> unique(stage_ids.begin(), stage_ids.end());
  Json records = array();
  for (const std::uint32_t id : unique) {
    const Resource &resource = tables.resources.at(id);
    std::optional<Bytes> owned;
    std::optional<ByteView> data;
    std::string name;
    if (resource.path) {
      const fs::path source = media / utf8_path(*resource.path);
      if (fs::is_regular_file(source)) {
        owned = read_file(source);
        data = ByteView(*owned);
      }
      name = lower_ascii(utf8_name(utf8_path(*resource.path).stem()));
    } else {
      data = packs.read(id);
      name = std::format("binlink/{}", id);
    }
    if (!data) {
      continue;
    }
    const Json parsed = parse_collision(*data);
    write_compact_json(output / "collision" / utf8_path(name + ".json"), parsed);
    Json stages = array();
    for (std::size_t stage = 0; stage < stage_ids.size(); ++stage) {
      if (stage_ids[stage] == id) {
        push(stages, integer(stage));
      }
    }
    push(records, object({{"resource", integer(id)},
                          {"file", text("collision/" + name + ".json")},
                          {"stages", stages},
                          {"walls", integer(field(parsed, "walls").array().size())},
                          {"floor", integer(field(parsed, "floor").array().size())}}));
  }
  report(std::format("{:>4} stage collisions", records.array().size()));
  return records;
}

} // namespace

Json convert_robots(const fs::path &media, ByteView executable, const GameTables &tables,
                    const fs::path &output, const Progress &report) {
  const XbeImage image(executable);
  const Json catalogue = robot_catalogue(image);
  write_compact_json(output / "robots" / "catalogue.json", catalogue);
  write_compact_json(output / "robots" / "skeletons.json", robot_skeletons(image));
  const RobotResources wanted = referenced_resources(catalogue);
  BinlinkPacks packs(media, tables);
  write_compact_json(output / "robots" / "movement_mounts.json",
                     movement_mounts(image, executable, catalogue, packs));
  write_compact_json(output / "robots" / "weapon_mounts.json", weapon_mounts(image, catalogue, packs));
  write_compact_json(output / "tables" / "lessons.json", lesson_messages(image, tables));
  write_compact_json(output / "tables" / "environment.json", stage_environments(image));
  write_compact_json(output / "tables" / "parts.json", part_tables(image, tables));
  write_compact_json(output / "tables" / "chatter.json", chatter_tables(image, tables));
  write_compact_json(output / "tables" / "battle_scenes.json", battle_scenes(image));
  write_compact_json(output / "tables" / "finale.json", finale_tables(image, tables));
  write_compact_json(output / "tables" / "ending_subtitles.json", ending_subtitles(image));
  const auto paths = sorted_files(media / "enepath", "_enepath.bin");
  for (const fs::path &path : paths) {
    std::string name = lower_ascii(utf8_name(path));
    name.resize(name.size() - 4); // ".bin"
    write_compact_json(output / "enepath" / utf8_path(name + ".json"), enemy_path(read_file(path)));
  }

  Json result = object({{"enemy_paths", integer(paths.size())}});
  put(result, "models", convert_models(wanted, catalogue, packs, output, report));
  put(result, "hud_models", convert_hud_models(packs, output, report));
  put(result, "arena_monitor", convert_arena_ship(image, packs, output));
  put(result, "effect_models", convert_shield(media, packs, output, report));
  put(result, "effect_bank", convert_effect_bank(image, tables, packs, output, report));
  const std::size_t textures = convert_packs(
      std::vector<std::uint32_t>(wanted.textures.begin(), wanted.textures.end()), packs,
      [&](std::uint32_t id, int index) { return output / "textures" / "binlink" / binlink_texture_name(id, index); });
  report(std::format("{:>4} robot textures", textures));
  put(result, "textures", integer(textures));
  std::vector<std::uint32_t> slot_packs;
  for (const Resource *resource : sprite_binlink_packs(tables)) {
    slot_packs.push_back(static_cast<std::uint32_t>(resource->id));
  }
  const std::size_t slot_textures = convert_packs(slot_packs, packs, [&](std::uint32_t id, int index) {
    return output / "textures" / "binlink" / std::to_string(id) / std::format("{}.png", index);
  });
  report(std::format("{:>4} binlink sprite textures", slot_textures));
  put(result, "slot_textures", integer(slot_textures));
  put(result, "motion_banks", integer(convert_motions(wanted, packs, output, report)));
  put(result, "collision", convert_collision(image, tables, media, packs, output, report));
  put(result, "stages", convert_stages(image, tables, packs, output, report));
  return result;
}

} // namespace phantomcrash::extract
