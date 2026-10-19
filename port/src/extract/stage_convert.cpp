// The city battle stages (stage_convert.py): Shinjuku, Shibuya and
// Umihotaru by day and night, their areas' enemy paths and the stage
// collision files (collision.py).
#include "gltf.hpp"
#include "model.hpp"
#include "parallel.hpp"
#include "robot_assets.hpp"
#include "robot_tables.hpp"
#include "stage_tables.hpp"

#include <format>
#include <map>
#include <optional>
#include <set>

namespace phantomcrash::extract {
namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t stage_models = 0x2212AC;  // u32 model resource per stage
constexpr std::uint32_t stage_objects = 0x2212EC; // u32 object list per stage
constexpr int city_stages = 6;
constexpr std::uint32_t stage_texture_base = 1558; // stage s's texture pack: 1558 + s
constexpr std::uint32_t object_end = 0xFFFFFFFF;
constexpr std::uint32_t object_sky = 1;           // view layer 1, the sky
constexpr std::uint32_t object_static_volume = 7; // stencil volumes, not colour geometry
// The areas' enemy paths: area a's by day and night.
constexpr std::uint32_t area_paths[]{0x697, 0x698, 0x699};

constexpr std::uint32_t grid_cells[]{32, 4, 32};
constexpr std::size_t floor_counts = 0x4028;
constexpr std::size_t grid_centre = 0x5038;
constexpr std::size_t collision_data = 0x5044;
constexpr std::size_t triangle_size = 0x34; // "<12fI"
constexpr int collision_decimals = 4;

std::vector<std::pair<std::uint32_t, std::uint32_t>> stage_object_list(const XbeImage &image, int stage) {
  const std::uint32_t address = image.u32(stage_objects + static_cast<std::uint32_t>(stage) * 4);
  std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
  for (;;) {
    const auto entry = image.u32s(address + static_cast<std::uint32_t>(out.size()) * 16, 4);
    if (entry[0] == object_end) {
      return out;
    }
    out.emplace_back(entry[0], entry[1]);
  }
}

Json collision_triangles(ByteView data, std::size_t offset, std::size_t count) {
  Json triangles = array();
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t at = offset + index * triangle_size;
    Json points = array();
    for (std::size_t p = 0; p < 3; ++p) {
      const std::size_t corner = at + p * 12;
      push(points, rounded_list({f32(data, corner), f32(data, corner + 4), f32(data, corner + 8)},
                                collision_decimals));
    }
    push(triangles, object({{"points", points}, {"surface", integer(u32(data, at + 48))}}));
  }
  return triangles;
}

} // namespace

Json parse_collision(ByteView data) {
  if (data.size() < collision_data) {
    throw FormatError("collision file is too short");
  }
  const std::uint32_t nx = u32(data, 0), ny = u32(data, 4), nz = u32(data, 8);
  if (nx != grid_cells[0] || ny != grid_cells[1] || nz != grid_cells[2]) {
    throw FormatError(std::format("unexpected collision grid {} x {} x {}", nx, ny, nz));
  }
  const std::size_t walls = u32(data, 24);
  const std::size_t wall_indices = u32(data, 28);
  const std::size_t floors = u32(data, floor_counts);
  const std::size_t floor_indices = u32(data, floor_counts + 4);
  const std::size_t wall_offset = collision_data;
  const std::size_t floor_offset = wall_offset + walls * triangle_size + wall_indices * 4;
  const std::size_t end = floor_offset + floors * triangle_size + floor_indices * 4;
  if (end != data.size()) {
    throw FormatError(std::format("collision file size {} does not match its counts ({})", data.size(), end));
  }
  Json grid = object(
      {{"cells", integers(std::vector<std::uint32_t>{nx, ny, nz})},
       {"cell_size", rounded_list({f32(data, 12), f32(data, 16), f32(data, 20)}, collision_decimals)},
       {"centre", rounded_list({f32(data, grid_centre), f32(data, grid_centre + 4), f32(data, grid_centre + 8)},
                               collision_decimals)}});
  return object({{"grid", grid},
                 {"walls", collision_triangles(data, wall_offset, walls)},
                 {"floor", collision_triangles(data, floor_offset, floors)}});
}

Json convert_stages(const XbeImage &image, const GameTables &tables, BinlinkPacks &packs,
                    const fs::path &output, const Progress &report) {
  const auto models = image.u32s(stage_models, city_stages);
  std::map<std::uint32_t, std::string> paths;
  for (const std::uint32_t resource : area_paths) {
    const auto data = packs.read(resource);
    const auto name = packs.pack_stem(resource);
    if (!data || !name) {
      continue;
    }
    paths[resource] = "enepath/" + *name + "_enepath.json";
    write_compact_json(output / utf8_path(paths[resource]), enemy_path(*data));
  }
  // The stages convert independently; their records keep the stage order.
  std::vector<std::optional<Json>> converted(city_stages);
  parallel_for(converted.size(), [&](const std::size_t job) {
    const int stage = static_cast<int>(job);
    const std::uint32_t model_id = models[job];
    const auto data = packs.read(model_id);
    const std::uint32_t pack_id = stage_texture_base + static_cast<std::uint32_t>(stage);
    const auto textures = packs.read(pack_id);
    if (!data || !textures) {
      return;
    }
    int count = 0;
    for (const int index : write_pack_textures(*textures, [&](int i) {
           return output / "textures" / "stages" / std::to_string(pack_id) / std::format("{}.png", i);
         })) {
      count = std::max(count, index + 1);
    }
    std::vector<std::string> uris;
    for (int index = 0; index < count; ++index) {
      uris.push_back(std::format("../../textures/stages/{}/{}.png", pack_id, index));
    }
    const auto objects = stage_object_list(image, stage);
    const Model parsed = parse_model(*data);
    const auto first_slot = tables.resources.at(model_id).first_mesh_slot;
    if (!first_slot || objects.empty()) {
      throw FormatError("Stage model has no mesh slots or objects");
    }
    // Resolve the loader's global slots, keeping stencil geometry out of colour draws.
    std::set<std::size_t> selected, volumes;
    Json sky = array();
    for (const auto &[slot, kind] : objects) {
      const std::int64_t index = std::int64_t{slot} - *first_slot;
      if (index < 0 || index >= static_cast<std::int64_t>(parsed.meshes.size())) {
        throw FormatError("Stage object slot is outside its model resource");
      }
      const auto mesh = static_cast<std::size_t>(index);
      (kind == object_static_volume ? volumes : selected).insert(mesh);
      if (kind == object_sky) {
        push(sky, text(std::format("mesh{}_node0", mesh)));
      }
    }
    Json shadow_nodes = array();
    for (const std::size_t index : volumes) {
      const auto &nodes = parsed.meshes[index].nodes;
      for (std::size_t node = 0; node < nodes.size(); ++node) {
        if (nodes[node].batch_count != 0) {
          push(shadow_nodes, text(std::format("mesh{}_node{}", index, node)));
        }
      }
    }
    std::set<std::size_t> drawn = selected;
    drawn.insert(volumes.begin(), volumes.end());
    const auto summary = write_gltf(parsed, output / "models" / "stages" / std::format("{}.gltf", model_id),
                                    uris, std::nullopt, &drawn);
    const auto path = paths.find(area_paths[stage / 2]);
    Json record = object({{"stage", integer(stage)},
                          {"model", text(std::format("models/stages/{}.gltf", model_id))},
                          {"textures", integer(pack_id)},
                          {"sky_nodes", sky},
                          {"shadow_nodes", shadow_nodes},
                          {"enemy_path", path != paths.end() ? text(path->second) : null()}});
    put_summary(record, summary);
    converted[job] = std::move(record);
  });
  Json records = array();
  for (auto &record : converted) {
    if (record) {
      push(records, std::move(*record));
    }
  }
  const Json scenery = convert_stage_objects(image, tables, packs, output);
  for (Json &record : std::get<Json::Array>(records.value)) {
    const Json &objects = scenery.array().at(static_cast<std::size_t>(field(record, "stage").number()));
    put(record, "props_model", field(objects, "props_model"));
    put(record, "door_model", field(objects, "door_model"));
  }
  write_compact_json(output / "tables" / "stages.json", records);
  report(std::format("{:>4} city stages, {} area paths", records.array().size(), paths.size()));
  return records;
}

} // namespace phantomcrash::extract
