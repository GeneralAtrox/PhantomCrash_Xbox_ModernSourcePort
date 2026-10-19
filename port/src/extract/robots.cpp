#include "robots.hpp"

#include <array>

namespace phantomcrash::extract {
namespace {

struct PartTable {
  std::uint32_t base, size;
  int count;
};

constexpr PartTable body_parts{0x1B5B88, 0x40, 16};    // type: byte 4
constexpr PartTable leg_parts{0x1B5F88, 0xC0, 43};     // type: word 2
constexpr PartTable arm_parts{0x1BAE48, 0xC8, 76};     // type: word 2 (0x0926be)
constexpr PartTable shoulder_parts{0x1BE9A8, 0xC8, 69}; // type: word 2 (0x0926ee)
constexpr int body_types = 7;
constexpr int leg_types = 21;
constexpr int arm_types = 34;
constexpr int shoulder_types = 38;
constexpr std::size_t paints_per_leg_type = 57; // colour index limit checked at 0x081a3b
constexpr std::size_t emblems = 206;
constexpr std::uint32_t body_models = 0x25AC70;
constexpr std::uint32_t body_emblem_models = 0x25AC8C; // the emblem decal per body type
constexpr std::uint32_t leg_models = 0x25ACA8;
constexpr std::array<std::uint32_t, 2> arm_models{0x25AD88, 0x25AD00}; // sides 0 and 1
constexpr std::array<std::uint32_t, 2> shoulder_models{0x25AF20, 0x25AFB8}; // sides 2, 3
constexpr std::uint32_t leg_skeletons = 0x2416B8;
constexpr std::array<std::uint32_t, 2> arm_skeletons{0x241798, 0x241710};
constexpr std::uint32_t arm_classes = 0x22F398; // picks one of the leg type's arm banks
constexpr std::array<std::uint32_t, 2> arm_motions{0x259190, 0x259238}; // [leg][class]
constexpr std::uint32_t leg_motions = 0x259300;
constexpr std::uint32_t body_motions = 0x2592E0;
// The pose builder (0x0aa499) turns the forearm nodes of a robot whose body
// is of this type by minus the float here (pi/2) about x.
constexpr int forearm_turn_body_type = 1;
constexpr std::uint32_t forearm_turn = 0x19F870;
constexpr std::uint32_t motion_timings = 0x22F4C8; // [leg type][motion] 4 floats (0x08ddf0)
constexpr std::uint32_t motions_per_bank = 37;
constexpr std::uint32_t leg_radii = 0x22F420; // {f32 ?, f32 radius} per leg type (0x080e41)
constexpr std::uint32_t leg_shadows = 0x242750; // per leg type: its shadow mesh (0x082e60)
constexpr std::uint32_t paints = 0x25ABC8;      // {int *textures, int} per leg type
constexpr std::uint32_t emblem_textures = 0x25A890;
constexpr int max_depth = 32;
constexpr int decimals = 6;

std::uint32_t at(const std::uint32_t base, const int index, const std::uint32_t size) {
  return base + static_cast<std::uint32_t>(index) * size;
}

Json words(const XbeImage &image, const std::uint32_t address, const std::size_t count) {
  return integers(image.u32s(address, count));
}

Json skeleton_node(const XbeImage &image, const std::uint32_t address, const int depth) {
  if (depth > max_depth) {
    throw FormatError("Skeleton deeper than 32 at " + std::to_string(address));
  }
  Json node = object({{"type", integer(image.i32(address))},
                      {"flags", integer(image.u32(address + 4))},
                      {"track", integer(image.i32(address + 8))},
                      {"translation", rounded_list(image.f32s(address + 12, 3), decimals)},
                      {"rotation", rounded_list(image.f32s(address + 24, 3), decimals)},
                      {"bone", integer(image.i32(address + 40))}});
  Json children = array();
  // Lists outside the file image are filled at run time and start empty.
  for (std::uint32_t list = image.u32(address + 36); list != 0 && image.mapped(list, 4); list += 4) {
    const std::uint32_t child = image.u32(list);
    if (child == 0) {
      break;
    }
    if (child != 0xFFFFFFFF) { // the arm node's placeholders
      push(children, skeleton_node(image, child, depth + 1));
    }
  }
  if (!children.array().empty()) {
    put(node, "children", std::move(children));
  }
  return node;
}

Json skeleton_list(const XbeImage &image, const std::uint32_t table, const int count) {
  Json trees = array();
  for (int index = 0; index < count; ++index) {
    const std::uint32_t root = image.u32(at(table, index, 4));
    push(trees, root != 0 ? skeleton_node(image, root, 0) : null());
  }
  return trees;
}

Json part_types(const XbeImage &image, const PartTable &table, const bool byte) {
  Json out = array();
  for (int i = 0; i < table.count; ++i) {
    const std::uint32_t record = at(table.base, i, table.size);
    push(out, byte ? integer(image.u32(record + 4) & 0xFF) : integer(image.i32(record + 8)));
  }
  return out;
}

// Per motion: first frame, loop start, loop end and last frame.
Json timings(const XbeImage &image, const int leg_type) {
  const std::uint32_t base = motion_timings + static_cast<std::uint32_t>(leg_type) * motions_per_bank * 16;
  Json out = array();
  for (std::uint32_t motion = 0; motion < motions_per_bank; ++motion) {
    push(out, rounded_list(image.f32s(base + 16 * motion, 4), decimals));
  }
  return out;
}

} // namespace

std::int64_t json_int(const Json &value) {
  if (const auto *signed_value = std::get_if<std::int64_t>(&value.value)) {
    return *signed_value;
  }
  if (const auto *unsigned_value = std::get_if<std::uint64_t>(&value.value)) {
    return static_cast<std::int64_t>(*unsigned_value);
  }
  throw FormatError("Expected an integer");
}

Json robot_catalogue(const XbeImage &image) {
  Json bodies = array();
  for (int t = 0; t < body_types; ++t) {
    push(bodies, object({{"model", integer(image.u32(at(body_models, t, 4)))},
                         {"emblem_model", integer(image.u32(at(body_emblem_models, t, 4)))},
                         {"motions", integer(image.u32(at(body_motions, t, 4)))},
                         {"forearm_turn", t == forearm_turn_body_type
                                              ? real(-python_round(image.f32(forearm_turn), decimals))
                                              : real(0.0)}}));
  }
  Json legs = array();
  for (int t = 0; t < leg_types; ++t) {
    const std::uint32_t textures = image.u32(at(paints, t, 8));
    Json motions_by_side = array();
    for (const std::uint32_t table : arm_motions) {
      push(motions_by_side, words(image, at(table, t, 8), 2));
    }
    push(legs, object({{"model", integer(image.u32(at(leg_models, t, 4)))},
                       {"motions", integer(image.u32(at(leg_motions, t, 4)))},
                       {"arm_motions", motions_by_side},
                       {"paints", textures != 0 ? words(image, textures, paints_per_leg_type) : array()},
                       {"timings", timings(image, t)},
                       {"radius", rounded(image.f32(at(leg_radii, t, 8) + 4), decimals)},
                       {"shadow_mesh", integer(image.u32(at(leg_shadows, t, 4)))}}));
  }
  Json arms = array();
  for (int t = 0; t < arm_types; ++t) {
    push(arms, object({{"models", array({integer(image.u32(at(arm_models[0], t, 4))),
                                         integer(image.u32(at(arm_models[1], t, 4)))})},
                       {"class", integer(image.u32(at(arm_classes, t, 4)))}}));
  }
  Json shoulders = array();
  for (int t = 0; t < shoulder_types; ++t) {
    push(shoulders, object({{"models", array({integer(image.u32(at(shoulder_models[0], t, 4))),
                                              integer(image.u32(at(shoulder_models[1], t, 4)))})}}));
  }
  return object({{"parts", object({{"body", part_types(image, body_parts, true)},
                                   {"legs", part_types(image, leg_parts, false)},
                                   {"arm", part_types(image, arm_parts, false)},
                                   {"shoulder", part_types(image, shoulder_parts, false)}})},
                 {"body_types", bodies},
                 {"leg_types", legs},
                 {"arm_types", arms},
                 {"shoulder_types", shoulders},
                 {"emblems", words(image, emblem_textures, emblems)}});
}

Json robot_skeletons(const XbeImage &image) {
  return object({{"legs", skeleton_list(image, leg_skeletons, leg_types)},
                 {"arms", array({skeleton_list(image, arm_skeletons[0], arm_types),
                                 skeleton_list(image, arm_skeletons[1], arm_types)})}});
}

RobotResources referenced_resources(const Json &catalogue) {
  RobotResources out;
  const auto add = [](std::set<std::uint32_t> &into, const Json &value) {
    into.insert(static_cast<std::uint32_t>(json_int(value)));
  };
  for (const Json &emblem : field(catalogue, "emblems").array()) {
    add(out.textures, emblem);
  }
  for (const Json &body : field(catalogue, "body_types").array()) {
    add(out.models, field(body, "model"));
    add(out.models, field(body, "emblem_model"));
    add(out.motions, field(body, "motions"));
  }
  for (const Json &legs : field(catalogue, "leg_types").array()) {
    add(out.models, field(legs, "model"));
    add(out.motions, field(legs, "motions"));
    for (const Json &pair : field(legs, "arm_motions").array()) {
      for (const Json &motion : pair.array()) {
        add(out.motions, motion);
      }
    }
    for (const Json &paint : field(legs, "paints").array()) {
      add(out.textures, paint);
    }
  }
  for (const char *key : {"arm_types", "shoulder_types"}) {
    for (const Json &part : field(catalogue, key).array()) {
      for (const Json &model : field(part, "models").array()) {
        add(out.models, model);
      }
    }
  }
  out.motions.erase(0);
  return out;
}

} // namespace phantomcrash::extract
