// Weapon muzzle points (weapon_mounts.py) and exhaust mounts
// (movement_mounts.py). Positions mirror the source point's Z, as 0x09a9d4
// does; model node transforms already use game coordinates.
#include "model.hpp"
#include "robot_tables.hpp"
#include "robots.hpp"

#include <array>
#include <cmath>
#include <set>
#include <tuple>

namespace phantomcrash::extract {
namespace {

constexpr std::uint32_t movement_resources = 0x259358;
constexpr std::size_t movement_slots = 14;
constexpr std::size_t movement_slot_size = 28;
constexpr std::uint32_t contact_groups = 0x242BD8; // 3 ints per robot state
constexpr int contact_states = 11;

Json muzzle_points(ByteView data) {
  if (data.size() < 8) {
    throw FormatError("Truncated weapon attachment resource");
  }
  const std::uint32_t blocks = u32(data, 0);
  const std::size_t first = u32(data, 4);
  if (blocks == 0 || first + 20 > data.size()) {
    throw FormatError("Missing weapon attachment block");
  }
  const std::size_t count = u32(data, first + 4);
  const std::size_t offset = u32(data, first + 16);
  Json points = array();
  if (count == 0) {
    return points;
  }
  if (offset == 0 || first + offset + count * 16 > data.size()) {
    throw FormatError("Invalid weapon muzzle array");
  }
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t at = first + offset + index * 16;
    push(points, object({{"position", reals({f32(data, at), f32(data, at + 4), -f32(data, at + 8)})},
                         {"node", integer(i32(data, at + 12))}}));
  }
  return points;
}

Json vec(const Vec3d &v) {
  return reals({v[0], v[1], v[2]});
}

// The muzzle's drawing-node bone and its node chain up to the root.
void add_chain(Json &point, const std::vector<ModelNode> &nodes, const std::int64_t model_id) {
  const std::int64_t start = json_int(field(point, "node"));
  const std::string where = " in model " + std::to_string(model_id);
  if (start < 0 || start >= static_cast<std::int64_t>(nodes.size()) ||
      nodes[static_cast<std::size_t>(start)].batch_count == 0) {
    throw FormatError("Muzzle has no drawing node" + where);
  }
  // 0x0a8f5a assigns bones to drawing nodes, skipping hierarchy-only nodes.
  std::int64_t bone = 0;
  for (std::int64_t k = 0; k < start; ++k) {
    bone += nodes[static_cast<std::size_t>(k)].batch_count > 0 ? 1 : 0;
  }
  put(point, "bone", integer(bone));
  Json chain = array();
  std::set<std::int64_t> visited;
  for (std::int64_t node = start; node != -1;) {
    if (node < 0 || node >= static_cast<std::int64_t>(nodes.size()) || visited.contains(node)) {
      throw FormatError("Invalid muzzle parent chain" + where);
    }
    visited.insert(node);
    const ModelNode &transform = nodes[static_cast<std::size_t>(node)];
    push(chain, object({{"translation", vec(transform.translation)},
                        {"rotation", vec(transform.rotation)},
                        {"scale", vec(transform.scale)}}));
    node = transform.parent;
  }
  put(point, "chain", std::move(chain));
}

} // namespace

Json weapon_mounts(const XbeImage &image, const Json &catalogue, BinlinkPacks &packs) {
  Json weapons = object();
  const std::tuple<const char *, const char *, std::array<std::uint32_t, 2>> kinds[]{
      {"arm_types", "arm", {0x25AE10, 0x25AE98}},
      {"shoulder_types", "shoulder", {0x25B050, 0x25B0E8}}};
  for (const auto &[key, part_key, tables] : kinds) {
    std::set<std::int64_t> used;
    for (const Json &type : field(field(catalogue, "parts"), part_key).array()) {
      used.insert(json_int(type));
    }
    const auto &definitions = field(catalogue, key).array();
    for (std::size_t part = 0; part < definitions.size(); ++part) {
      // Unreferenced shoulder types include bare-body placeholder models;
      // their table cells are not an equipped weapon's attachment data.
      if (!used.contains(static_cast<std::int64_t>(part))) {
        continue;
      }
      for (std::size_t side = 0; side < tables.size(); ++side) {
        const auto model_id = json_int(field(definitions[part], "models").array().at(side));
        const std::uint32_t resource = image.u32(tables[side] + static_cast<std::uint32_t>(part) * 4);
        const auto source = packs.read(resource);
        const auto geometry = packs.read(static_cast<std::uint32_t>(model_id));
        if (!source || !geometry) {
          throw FormatError("Missing weapon resources " + std::to_string(resource) + "/" +
                            std::to_string(model_id));
        }
        Json points = muzzle_points(*source);
        const Model parsed = parse_model(*geometry);
        for (Json &point : std::get<Json::Array>(points.value)) {
          add_chain(point, parsed.meshes.at(0).nodes, model_id);
        }
        put(weapons, "models/binlink/" + std::to_string(model_id) + ".gltf",
            object({{"resource", integer(resource)}, {"muzzles", std::move(points)}}));
      }
    }
  }
  return object({{"weapons", weapons}});
}

Json movement_mounts(const XbeImage &image, ByteView executable, const Json &catalogue,
                     BinlinkPacks &packs) {
  Json legs = array();
  const std::size_t leg_count = field(catalogue, "leg_types").array().size();
  for (std::size_t index = 0; index < leg_count; ++index) {
    const std::uint32_t resource = image.u32(movement_resources + static_cast<std::uint32_t>(index) * 4);
    const auto data = packs.read(resource);
    if (!data || data->size() != movement_slots * movement_slot_size) {
      throw FormatError("Invalid movement attachment resource " + std::to_string(resource));
    }
    Json slots = array();
    for (std::size_t slot = 0; slot < movement_slots; ++slot) {
      const std::size_t at = slot * movement_slot_size;
      const double values[]{f32(*data, at),      f32(*data, at + 4),  f32(*data, at + 8),
                            f32(*data, at + 16), f32(*data, at + 20), f32(*data, at + 24)};
      for (const double v : values) {
        if (!std::isfinite(v)) {
          throw FormatError("Non-finite movement attachment " + std::to_string(resource));
        }
      }
      push(slots, object({{"kind", integer(i32(*data, at + 12))},
                          {"position", reals({values[0], values[1], -values[2]})},
                          {"direction", reals({values[3], values[4], -values[5]})}}));
    }
    push(legs, object({{"resource", integer(resource)}, {"slots", slots}}));
  }
  Json contact = array();
  for (int state = 0; state < contact_states; ++state) {
    const std::size_t at = image.offset(contact_groups + static_cast<std::uint32_t>(state) * 12, 12);
    push(contact, array({integer(i32(executable, at)), integer(i32(executable, at + 4)),
                         integer(i32(executable, at + 8))}));
  }
  Json groups = object({{"jump", integer(23)},
                        {"descent", integer(24)},
                        {"dodge", integer(24)},
                        {"dash", array({integer(17), integer(18)})},
                        {"dash_stop", integer(25)},
                        {"takeoff", integer(26)},
                        {"takeoff_seconds", real(30.0 / 30.0)},
                        {"contact", contact}});
  return object({{"legs", legs}, {"groups", groups}});
}

} // namespace phantomcrash::extract
