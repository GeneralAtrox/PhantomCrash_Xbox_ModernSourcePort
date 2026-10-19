// A battle's scripted entrance and exit, gates, doors, fixed cameras and
// supply routes (battle_scenes.py). State and field names are
// researcher-assigned.
#include "motion.hpp"
#include "robot_tables.hpp"
#include "robots.hpp"
#include "stage_tables.hpp"

#include <array>
#include <string>

namespace phantomcrash::extract {
namespace {

constexpr std::uint32_t scene_table = 0x258F50;
constexpr std::uint32_t scene_entry = 12;
constexpr std::array<const char *, 8> camera_channels{
    "roll", "fov", "eye_x", "eye_y", "eye_z", "target_x", "target_y", "target_z"};
constexpr std::uint32_t gate_table = 0x201598;
constexpr int gate_stages = 8;
constexpr std::uint32_t object_records = 0x22E638;
constexpr std::uint32_t object_types = 0x22E7E8;
constexpr std::uint32_t object_collision = 0x22E658;
constexpr std::uint32_t object_record = 52;
constexpr std::uint32_t object_template = 0x1B0; // 12 triangles of three vec3
constexpr std::int32_t door_type = 6;            // 0x0731c5 opens and 0x073430 closes it
// First mesh slot of each stage's effect model (resources 198-205).
constexpr std::array<std::uint32_t, 8> door_stages{1052, 1079, 1106, 1122, 1138, 1154, 0x492, 0x498};
constexpr std::uint32_t fixed_cameras = 0x232AE0;
constexpr std::uint32_t fixed_camera_areas = 0x232BC0;
constexpr std::size_t fixed_camera_area_count = 5;
constexpr std::uint32_t supply_route_counts = 0x21C72C;

std::vector<Json> channels(const XbeImage &image, std::uint32_t keys_address,
                           std::uint32_t values_address, std::size_t count) {
  std::size_t keys_at = image.offset(keys_address, 2);
  std::size_t values_at = values_address != 0 ? image.offset(values_address, 4) : 0;
  std::vector<Json> out;
  for (std::size_t k = 0; k < count; ++k) {
    out.push_back(motion_channel(image.data(), keys_at, values_at));
  }
  return out;
}

Json walk(const XbeImage &image, std::uint32_t address) {
  const auto words = image.u32s(address, 4);
  if ((words[1] & track_translation) == 0 || words[2] == 0) {
    return null();
  }
  const std::size_t skipped = (words[1] & track_rotation) != 0 ? 3 : 0;
  auto found = channels(image, words[2], words[3], skipped + 3);
  return array(std::vector<Json>(found.begin() + static_cast<std::ptrdiff_t>(skipped), found.end()));
}

Json vec3(const XbeImage &image, std::uint32_t address) {
  return rounded_list(image.f32s(address, 3), 4);
}

Json gates(const XbeImage &image, std::uint32_t stage) {
  const auto words = image.u32s(gate_table + stage * 12, 3);
  Json out = array();
  for (std::uint32_t index = 0; index < words[1]; ++index) {
    push(out, object({{"position", vec3(image, words[0] + index * 12)},
                      {"heading", rounded(image.f32(words[2] + index * 4), 4)}}));
  }
  return out;
}

struct DoorLeaf {
  std::int32_t kind{}, gate{};
  std::int64_t mesh{};
  std::vector<double> position, rotation;
  Json collision;
};

std::vector<double> rounded4(std::vector<double> values) {
  for (double &v : values) {
    v = python_round(v, 4);
  }
  return values;
}

DoorLeaf door_leaf(const XbeImage &image, std::uint32_t stage, std::uint32_t index,
                   std::uint32_t first_model) {
  const std::uint32_t record = image.u32(object_records + stage * 4) + index * object_record;
  DoorLeaf leaf;
  leaf.mesh = std::int64_t{image.u32(record)} - first_model;
  leaf.position = rounded4(image.f32s(record + 8, 3));
  leaf.rotation = rounded4(image.f32s(record + 0x14, 3));
  const std::int32_t template_index = image.i32(record + 0x2C);
  leaf.gate = image.i32(record + 0x30);
  leaf.kind = image.i32(image.u32(object_types + stage * 4) + static_cast<std::uint32_t>(template_index) * 4);
  const std::uint32_t triangles =
      image.u32(object_collision + stage * 4) + static_cast<std::uint32_t>(template_index) * object_template;
  leaf.collision = array();
  for (std::uint32_t at = triangles; at < triangles + object_template; at += 36) {
    push(leaf.collision, rounded_list(image.f32s(at, 9), 4));
  }
  return leaf;
}

Json doors(const XbeImage &image, std::uint32_t stage) {
  const std::uint32_t first_model = door_stages[stage];
  std::vector<DoorLeaf> leaves;
  const Json records = stage_object_records(image, static_cast<int>(stage));
  for (const Json &record : records.array()) {
    leaves.push_back(door_leaf(image, stage, static_cast<std::uint32_t>(json_int(field(record, "index"))),
                               first_model));
  }
  Json out = array();
  for (std::size_t index = 1; index < leaves.size(); ++index) {
    const DoorLeaf &leaf = leaves[index];
    if (leaf.kind != door_type || leaf.gate < 0) {
      continue;
    }
    const DoorLeaf &partner = leaves[index - 1];
    if (partner.kind != door_type || partner.position != leaf.position ||
        partner.rotation != leaf.rotation) {
      throw FormatError("Gate leaves do not share a transform on stage " + std::to_string(stage));
    }
    push(out, object({{"gate", integer(leaf.gate)},
                      {"position", reals(leaf.position)},
                      {"rotation", reals(leaf.rotation)},
                      {"leaves", array({object({{"mesh", integer(partner.mesh)},
                                                {"swing", integer(-1)},
                                                {"collision", partner.collision}}),
                                        object({{"mesh", integer(leaf.mesh)},
                                                {"swing", integer(1)},
                                                {"collision", leaf.collision}})})}}));
  }
  return out;
}

Json fixed_camera_set(const XbeImage &image, std::uint32_t stage) {
  const auto words = image.u32s(fixed_cameras + stage * 8, 2);
  const std::uint32_t areas = image.u32(fixed_camera_areas + stage * 4);
  const auto ranges = image.u32s(areas, 2 * fixed_camera_area_count);
  Json points = array();
  for (std::uint32_t i = 0; i < words[1]; ++i) {
    push(points, vec3(image, words[0] + i * 12));
  }
  Json area_list = array();
  for (std::size_t i = 0; i < ranges.size(); i += 2) {
    push(area_list, array({integer(ranges[i]), integer(ranges[i + 1])}));
  }
  return object({{"points", points}, {"areas", area_list}});
}

Json supply_route_list(const XbeImage &image, std::uint32_t stage) {
  const std::int32_t count = image.i32(supply_route_counts + stage * 4);
  Json routes = array();
  for (std::int32_t route = 0; route < count; ++route) {
    const std::uint32_t record =
        supply_routes + (stage + gate_stages * static_cast<std::uint32_t>(route)) * supply_route_size;
    const auto words = image.u32s(record, 7);
    push(routes, object({{"segments", bezier_segments(image, words[0], words[1])},
                         {"closing", integer(words[2])},
                         {"drop", integer(words[3])},
                         {"leave", integer(words[5])}}));
  }
  return routes;
}

} // namespace

Json bezier_segments(const XbeImage &image, std::uint32_t points, std::uint32_t count) {
  Json segments = array();
  for (std::uint32_t i = 0; i < count; ++i) {
    Json segment = array();
    for (std::uint32_t k = 0; k < 4; ++k) {
      push(segment, vec3(image, points + 48 * i + 12 * k));
    }
    push(segments, std::move(segment));
  }
  return segments;
}

Json battle_scenes(const XbeImage &image) {
  Json states = object();
  std::vector<std::uint32_t> scene_states;
  for (std::uint32_t s = 1; s < 7; ++s) {
    scene_states.push_back(s);
  }
  for (std::uint32_t s = 8; s < 19; ++s) {
    scene_states.push_back(s);
  }
  for (const std::uint32_t state : scene_states) {
    const std::uint32_t entry = scene_table + state * scene_entry;
    const auto words = image.u32s(entry, 2);
    const bool relative = image.u8(entry + 8) != 0;
    if (words[0] == 0) {
      continue;
    }
    const auto camera = image.u32s(words[0], 4);
    auto found = channels(image, camera[2], camera[3], camera_channels.size());
    Json scene = object();
    for (std::size_t k = 0; k < camera_channels.size(); ++k) {
      put(scene, camera_channels[k], std::move(found[k]));
    }
    put(states, std::to_string(state),
        object({{"frames", integer(camera[0])},
                {"relative", boolean(relative)},
                {"camera", std::move(scene)},
                {"walk", words[1] != 0 ? walk(image, words[1]) : null()}}));
  }
  Json gate_map = object();
  Json door_map = object();
  Json camera_map = object();
  Json supply_map = object();
  for (std::uint32_t stage = 0; stage < gate_stages; ++stage) {
    put(gate_map, std::to_string(stage), gates(image, stage));
  }
  for (std::uint32_t stage = 0; stage < door_stages.size(); ++stage) {
    put(door_map, std::to_string(stage), doors(image, stage));
  }
  for (std::uint32_t stage = 0; stage < gate_stages; ++stage) {
    put(camera_map, std::to_string(stage), fixed_camera_set(image, stage));
  }
  for (std::uint32_t stage = 0; stage < gate_stages; ++stage) {
    put(supply_map, std::to_string(stage), supply_route_list(image, stage));
  }
  return object({{"states", states},
                 {"gates", gate_map},
                 {"doors", door_map},
                 {"fixed_cameras", camera_map},
                 {"supply_routes", supply_map}});
}

} // namespace phantomcrash::extract
