// City scenery instances, separate from the static stage (stage_objects.py).
// 0x06f0ab walks 52-byte records at 0x22e638[stage] up to model slot -1;
// 0x070b80 uses the first slot for the intact mesh, the second for its
// wreck, then the position and rotation. The collision template at +44
// selects the object kind via 0x22e7e8[stage]; +48 is a gate. Names are
// researcher-assigned; resource IDs and transforms are shipped data.
#include "gltf.hpp"
#include "model.hpp"
#include "parallel.hpp"
#include "robot_assets.hpp"
#include "robots.hpp"
#include "stage_tables.hpp"

#include <algorithm>
#include <format>
#include <set>

namespace phantomcrash::extract {
namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t object_records = 0x22E638;
constexpr std::uint32_t object_types = 0x22E7E8;
constexpr std::uint32_t object_record = 52; // "<2i6f5i"
constexpr int city_stages = 6;
constexpr int effect_model_base = 198;
constexpr std::int64_t door_type = 6;
constexpr int max_objects = 100; // 0x06f0ab's city object limit
constexpr int other_stage_objects = 8;
constexpr std::uint32_t object_collision = 0x22E658;
constexpr std::uint32_t collision_bytes = 0x1B0; // twelve triangles, 0x070b80
// Hit-handler dispatch at 0x22e8e0; values passed to 0x057760.
constexpr std::pair<std::int64_t, int> break_effects[]{{0, 14}, {1, 13}, {2, 15}, {3, 15},
                                                         {4, 15}, {5, 58}, {7, 13}};
constexpr int break_sound = 0x10;       // bank 1
constexpr double wreck_hit_delay = 6.0; // kind 1 returns to its hit handler after six seconds

// The remaining rendered surfaces, not the intact object's tall box.
Json wreck_collision(const ModelMesh &mesh) {
  Json triangles = array();
  std::set<std::array<Vec3d, 3>> seen;
  for (const ModelNode &node : mesh.nodes) {
    if (node.translation != Vec3d{0.0, 0.0, 0.0} || node.rotation != Vec3d{0.0, 0.0, 0.0} ||
        node.scale != Vec3d{1.0, 1.0, 1.0}) {
      throw FormatError("Scenery wreck collision requires baked node transforms");
    }
    const std::size_t last = std::min<std::size_t>(node.first_batch + node.batch_count, mesh.batches.size());
    for (std::size_t b = node.first_batch; b < last; ++b) {
      const ModelBatch &batch = mesh.batches[b];
      const std::size_t start = std::size_t{batch.first_triangle} * 3;
      for (std::size_t offset = start; offset < start + std::size_t{batch.triangle_count} * 3; offset += 3) {
        std::array<Vec3d, 3> points;
        for (std::size_t k = 0; k < 3; ++k) {
          points[k] = mesh.vertices.at(mesh.indices.at(offset + k)).position;
        }
        std::array<Vec3d, 3> key = points;
        std::sort(key.begin(), key.end());
        const auto &[a, b2, c] = points;
        const Vec3d u{b2[0] - a[0], b2[1] - a[1], b2[2] - a[2]};
        const Vec3d v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const Vec3d normal{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        if (!seen.contains(key) &&
            python_sum({normal[0] * normal[0], normal[1] * normal[1], normal[2] * normal[2]}) > 1e-12) {
          std::vector<double> flat;
          for (const Vec3d &point : points) {
            flat.insert(flat.end(), point.begin(), point.end());
          }
          push(triangles, reals(flat));
          seen.insert(key);
        }
      }
    }
  }
  if (triangles.array().empty()) {
    throw FormatError("Scenery wreck has no collision surfaces");
  }
  return triangles;
}

} // namespace

Json stage_object_records(const XbeImage &image, const int stage) {
  const auto index_of_stage = static_cast<std::uint32_t>(stage);
  const std::uint32_t address = image.u32(object_records + index_of_stage * 4);
  const std::uint32_t types = image.u32(object_types + index_of_stage * 4);
  const int limit = stage < city_stages ? max_objects : other_stage_objects;
  Json result = array();
  for (int index = 0; index < limit; ++index) {
    const std::uint32_t at = address + static_cast<std::uint32_t>(index) * object_record;
    image.require(at, object_record);
    const std::int32_t slot = image.i32(at);
    if (slot == -1) {
      break;
    }
    const std::int32_t template_index = image.i32(at + 44);
    push(result, object({{"index", integer(index)},
                         {"slot", integer(slot)},
                         {"wreck_slot", integer(image.i32(at + 4))},
                         {"position", reals(image.f32s(at + 8, 3))},
                         {"rotation", reals(image.f32s(at + 20, 3))},
                         {"template", integer(template_index)},
                         {"kind", integer(image.u32(types + static_cast<std::uint32_t>(template_index) * 4))},
                         {"gate", integer(image.i32(at + 48))}}));
  }
  const std::uint32_t templates = image.u32(object_collision + index_of_stage * 4);
  for (Json &entry : std::get<Json::Array>(result.value)) {
    const std::uint32_t first =
        templates + static_cast<std::uint32_t>(json_int(field(entry, "template"))) * collision_bytes;
    Json collision = array();
    for (std::uint32_t at = first; at < first + collision_bytes; at += 36) {
      push(collision, reals(image.f32s(at, 9)));
    }
    put(entry, "collision", std::move(collision));
    const std::int64_t kind = json_int(field(entry, "kind"));
    int effect = -1;
    for (const auto &[known, value] : break_effects) {
      if (known == kind) {
        effect = value;
      }
    }
    put(entry, "break_effect", integer(effect));
    const bool sounds = (kind >= 0 && kind <= 4) || kind == 7;
    put(entry, "break_sound", text(sounds ? std::format("sound/sfx/battle/{:02d}.wav", break_sound) : ""));
    // Retained wrecks block movement and shots until the wreck is removed.
    put(entry, "wreck_solid", boolean(json_int(field(entry, "wreck_slot")) >= 0));
    put(entry, "wreck_break_effect", integer(kind == 1 ? 15 : -1));
    put(entry, "wreck_hit_delay", real(kind == 1 ? wreck_hit_delay : 0.0));
  }
  return result;
}

Json convert_stage_objects(const XbeImage &image, const GameTables &tables, BinlinkPacks &packs,
                           const fs::path &output) {
  std::vector<Json> converted(city_stages);
  parallel_for(converted.size(), [&](const std::size_t job) {
    const int stage = static_cast<int>(job);
    const Resource &resource = tables.resources.at(static_cast<std::size_t>(effect_model_base + stage));
    const auto data = packs.read(static_cast<std::uint32_t>(resource.id));
    const auto textures = resource.texture_resource ? packs.read(*resource.texture_resource) : std::nullopt;
    if (!data || !textures) {
      throw FormatError(std::format("Missing scenery model or textures for stage {}", stage));
    }
    const Model parsed = parse_model(*data);
    const std::uint32_t texture_resource = *resource.texture_resource;
    std::vector<std::string> uris;
    for (const int index : write_pack_textures(*textures, [&](int i) {
           return output / "textures" / "stages" / std::to_string(texture_resource) / std::format("{}.png", i);
         })) {
      uris.push_back(std::format("../../textures/stages/{}/{}.png", texture_resource, index));
    }
    const std::int64_t first_slot = resource.first_mesh_slot.value_or(0);
    const auto mesh_count = static_cast<std::int64_t>(parsed.meshes.size());
    Json objects = stage_object_records(image, stage);
    std::vector<Json> props;
    for (Json &entry : std::get<Json::Array>(objects.value)) {
      const std::int64_t mesh = json_int(field(entry, "slot")) - first_slot;
      const std::int64_t wreck_slot = json_int(field(entry, "wreck_slot"));
      const std::int64_t wreck_mesh = wreck_slot >= 0 ? wreck_slot - first_slot : -1;
      put(entry, "mesh", integer(mesh));
      put(entry, "wreck_mesh", integer(wreck_mesh));
      if (mesh < 0 || mesh >= mesh_count) {
        throw FormatError(std::format("Invalid scenery mesh on stage {}", stage));
      }
      if (wreck_mesh >= mesh_count) {
        throw FormatError(std::format("Invalid wreck mesh on stage {}", stage));
      }
      if (wreck_mesh >= 0) {
        put(entry, "wreck_collision", wreck_collision(parsed.meshes[static_cast<std::size_t>(wreck_mesh)]));
      }
      // Moving gate leaves use StageDoors, so they are not baked into scenery.
      if (json_int(field(entry, "kind")) != door_type) {
        props.push_back(entry);
      }
    }
    const std::string props_path = std::format("models/stages/{}_props.gltf", stage);
    const std::string doors_path = std::format("models/stages/{}_objects.gltf", stage);
    write_gltf_instances(parsed, output / utf8_path(props_path), uris, props);
    write_gltf(parsed, output / utf8_path(doors_path), uris);
    converted[job] = object({{"stage", integer(stage)},
                         {"model_resource", integer(resource.id)},
                         {"texture_resource", integer(texture_resource)},
                         {"props_model", text(props_path)},
                         {"door_model", text(doors_path)},
                         {"objects", std::move(objects)}});
  });
  const Json result = array(std::move(converted));
  write_compact_json(output / "tables" / "stage_objects.json", result);
  return result;
}

} // namespace phantomcrash::extract
