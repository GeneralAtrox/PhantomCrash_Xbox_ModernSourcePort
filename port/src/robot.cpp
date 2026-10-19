#include "phantomcrash/robot.hpp"
#include "phantomcrash/garage.hpp"

#include <algorithm>
#include <stdexcept>

namespace phantomcrash {
namespace {

int integer(const json::JsonValue &value) {
  return value.is_number() ? static_cast<int>(value.number()) : -1;
}

template <typename Node> Node *find_torso(Node &node) {
  if ((node.flags & node_turns_with_aim) != 0) {
    return &node;
  }
  for (auto &child : node.children) {
    if (Node *torso = find_torso(child)) {
      return torso;
    }
  }
  return nullptr;
}

int bone_count(const SkeletonNode &node) {
  int count = node.bone >= 0 ? 1 : 0;
  for (const SkeletonNode &child : node.children) {
    count += bone_count(child);
  }
  return count;
}

// Moves every bone slot by `by`, except under `keep`.
void shift_bones(SkeletonNode &node, const int by, const SkeletonNode *keep) {
  if (&node == keep) {
    return;
  }
  if (node.bone >= 0) {
    node.bone += by;
  }
  for (SkeletonNode &child : node.children) {
    shift_bones(child, by, keep);
  }
}

void drop_tracks(SkeletonNode &node) {
  node.track = -1;
  for (SkeletonNode &child : node.children) {
    drop_tracks(child);
  }
}

// The maker whose leg types `leg_type` is among: each maker's run from its
// first (RobotCatalogue::body_leg_types).
int leg_maker(const std::vector<int> &body_leg_types, const int leg_type) {
  int maker = -1;
  for (std::size_t m = 0; m < body_leg_types.size(); ++m) {
    if (body_leg_types[m] <= leg_type) {
      maker = static_cast<int>(m);
    }
  }
  return maker;
}

// The arm skeleton an arm type hangs from on a body of `body_type`: its
// own, or with another maker's weapon (a port addition) the body's maker's
// first of the same class, whose tracks match the body's arm motions.
int body_arm_type(const RobotCatalogue &catalogue, const RobotSkeletons &skeletons,
                  const std::size_t side, const int arm_type, const int body_type) {
  const auto &firsts = catalogue.arm_maker_types;
  const int maker = leg_maker(firsts, arm_type);
  if (maker < 0 || maker == body_type || body_type < 0 ||
      body_type >= static_cast<int>(firsts.size())) {
    return arm_type;
  }
  const auto begin = static_cast<std::size_t>(firsts[static_cast<std::size_t>(body_type)]);
  const std::size_t end =
      static_cast<std::size_t>(body_type) + 1 < firsts.size()
          ? static_cast<std::size_t>(firsts[static_cast<std::size_t>(body_type) + 1])
          : catalogue.arm_types.size();
  const int arm_class = catalogue.arm_types[static_cast<std::size_t>(arm_type)].arm_class;
  int found = -1;
  for (std::size_t type = begin; type < end && type < catalogue.arm_types.size(); ++type) {
    if (type >= skeletons.arms[side].size() || !skeletons.arms[side][type]) {
      continue;
    }
    if (found < 0 ||
        (catalogue.arm_types[type].arm_class == arm_class &&
         catalogue.arm_types[static_cast<std::size_t>(found)].arm_class != arm_class)) {
      found = static_cast<int>(type);
    }
  }
  return found >= 0 ? found : arm_type;
}

Vec3 shoulder_shift(const RobotCatalogue &catalogue, const std::size_t side, const int type,
                    const int body_type) {
  const auto &mounts = catalogue.shoulder_mounts;
  const int maker = leg_maker(catalogue.shoulder_maker_types, type);
  if (maker < 0 || maker == body_type || body_type < 0 ||
      maker >= static_cast<int>(mounts.size()) || body_type >= static_cast<int>(mounts.size())) {
    return {};
  }
  Vec3 shift =
      mounts[static_cast<std::size_t>(body_type)] - mounts[static_cast<std::size_t>(maker)];
  if (side == 1) {
    shift.x = -shift.x;
  }
  return shift;
}

std::vector<int> integers(const json::JsonValue *array) {
  std::vector<int> out;
  if (array != nullptr && array->is_array()) {
    for (const json::JsonValue &value : array->array()) {
      out.push_back(integer(value));
    }
  }
  return out;
}

std::array<int, 2> pair_of(const json::JsonValue *array) {
  const std::vector<int> values = integers(array);
  return {values.size() > 0 ? values[0] : -1, values.size() > 1 ? values[1] : -1};
}

std::vector<std::string> paint_images(const json::JsonValue &value) {
  std::vector<std::string> out;
  if (const auto *images = value.find("paint_images")) {
    for (const auto &image : images->array()) {
      out.push_back(image.string());
    }
  }
  return out;
}

std::string part_paint(const std::vector<std::string> &images, int colour) {
  if (images.empty()) {
    return {};
  }
  return images[static_cast<std::size_t>(
      colour >= 0 && colour < static_cast<int>(images.size()) ? colour : 0)];
}

const json::JsonValue::Array &list(const json::JsonValue &document, const char *key) {
  const json::JsonValue *value = document.find(key);
  if (value == nullptr || !value->is_array()) {
    throw std::runtime_error(std::string("Robot catalogue has no ") + key);
  }
  return value->array();
}

template <typename T>
const T &pick(const std::vector<T> &table, const int index, const char *what) {
  if (index < 0 || index >= static_cast<int>(table.size())) {
    throw std::runtime_error(std::string("No robot ") + what + " " + std::to_string(index));
  }
  return table[static_cast<std::size_t>(index)];
}

} // namespace

RobotCatalogue parse_robot_catalogue(const json::JsonValue &document) {
  RobotCatalogue catalogue;
  const json::JsonValue *parts = document.find("parts");
  if (parts == nullptr || !parts->is_object()) {
    throw std::runtime_error("Robot catalogue has no parts");
  }
  read_maker_tables(document, catalogue);
  catalogue.body_parts = integers(parts->find("body"));
  catalogue.leg_parts = integers(parts->find("legs"));
  catalogue.arm_parts = integers(parts->find("arm"));
  catalogue.shoulder_parts = integers(parts->find("shoulder"));
  for (const json::JsonValue &value : list(document, "body_types")) {
    catalogue.body_types.push_back({static_cast<int>(value.number_or("model", -1)),
                                    static_cast<int>(value.number_or("emblem_model", -1)),
                                    static_cast<int>(value.number_or("motions", -1)),
                                    static_cast<float>(value.number_or("forearm_turn", 0.0)),
                                    paint_images(value)});
    read_body_model_variants(value, catalogue.body_types.back());
  }
  for (const json::JsonValue &value : list(document, "leg_types")) {
    RobotCatalogue::LegType legs;
    legs.model = static_cast<int>(value.number_or("model", -1));
    legs.motions = static_cast<int>(value.number_or("motions", -1));
    if (const json::JsonValue *arms = value.find("arm_motions"); arms && arms->is_array()) {
      for (std::size_t side = 0; side < 2 && side < arms->array().size(); ++side) {
        legs.arm_motions[side] = pair_of(&arms->array()[side]);
      }
    }
    legs.paints = integers(value.find("paints"));
    legs.paint_images = paint_images(value);
    legs.source_melee_motions = integers(value.find("source_melee_motions"));
    legs.source_motion_actions = integers(value.find("source_motion_actions"));
    legs.source_turn_frames = pair_of(value.find("source_turn_frames"));
    legs.source_transition_frames = integers(value.find("source_transition_frames"));
    if (const json::JsonValue *timings = value.find("timings"); timings && timings->is_array()) {
      for (const json::JsonValue &entry : timings->array()) {
        const auto &f = entry.array();
        if (f.size() == 4) {
          legs.timings.push_back(
              {static_cast<float>(f[0].number()), static_cast<float>(f[1].number()),
               static_cast<float>(f[2].number()), static_cast<float>(f[3].number())});
        }
      }
    }
    legs.radius = static_cast<float>(value.number_or("radius", 0.0));
    legs.shadow_mesh = static_cast<int>(value.number_or("shadow_mesh", -1.0));
    catalogue.leg_types.push_back(std::move(legs));
  }
  for (const json::JsonValue &value : list(document, "arm_types")) {
    catalogue.arm_types.push_back({pair_of(value.find("models")),
                                   static_cast<int>(value.number_or("class", 0)),
                                   paint_images(value)});
  }
  for (const json::JsonValue &value : list(document, "shoulder_types")) {
    catalogue.shoulder_types.push_back({pair_of(value.find("models")), paint_images(value)});
  }
  catalogue.emblems = integers(document.find("emblems"));
  if (const auto *images = document.find("emblem_images")) {
    for (const auto &image : images->array()) {
      catalogue.emblem_images.push_back(image.string());
    }
  }
  return catalogue;
}

RobotParts parse_robot_parts(const json::JsonValue &value) {
  RobotParts parts;
  parts.body = static_cast<int>(value.number_or("body", 0));
  parts.legs = static_cast<int>(value.number_or("legs", 0));
  parts.arms = pair_of(value.find("arms"));
  parts.shoulders = pair_of(value.find("shoulders"));
  parts.colour = static_cast<int>(value.number_or("colour", 0));
  parts.emblem = static_cast<int>(value.number_or("emblem", -1));
  return parts;
}

std::string robot_model_path(const int resource) {
  return resource < 0 ? "" : "models/binlink/" + std::to_string(resource) + ".gltf";
}

std::string robot_texture_path(const int resource) {
  return resource < 0 ? "" : "textures/binlink/" + std::to_string(resource) + ".png";
}

std::string motion_bank_path(const int resource) {
  return resource < 0 ? "" : "motions/" + std::to_string(resource) + ".json";
}

void read_maker_tables(const json::JsonValue &document, RobotCatalogue &catalogue) {
  for (auto [key, table] : {std::pair{"body_leg_types", &catalogue.body_leg_types},
                            std::pair{"arm_maker_types", &catalogue.arm_maker_types},
                            std::pair{"shoulder_maker_types", &catalogue.shoulder_maker_types}}) {
    if (table->empty()) {
      *table = integers(document.find(key));
    }
  }
  const json::JsonValue *mounts = document.find("shoulder_mounts");
  if (catalogue.shoulder_mounts.empty() && mounts != nullptr && mounts->is_array()) {
    for (const json::JsonValue &mount : mounts->array()) {
      const auto &xyz = mount.array();
      if (xyz.size() == 3) {
        catalogue.shoulder_mounts.push_back({static_cast<float>(xyz[0].number()),
                                             static_cast<float>(xyz[1].number()),
                                             static_cast<float>(xyz[2].number())});
      }
    }
  }
}

std::shared_ptr<const SkeletonNode> graft_body_section(const SkeletonNode &legs,
                                                       const SkeletonNode &body) {
  const SkeletonNode *section = find_torso(body);
  auto grafted = std::make_shared<SkeletonNode>(legs);
  SkeletonNode *torso = find_torso(*grafted);
  if (section == nullptr || torso == nullptr) {
    return nullptr;
  }
  shift_bones(*grafted, bone_count(*section) - bone_count(*torso), torso);
  torso->children = section->children;
  for (SkeletonNode &child : torso->children) {
    drop_tracks(child);
  }
  return grafted;
}

RobotAssembly assemble_robot(const RobotCatalogue &catalogue, const RobotSkeletons &skeletons,
                             const RobotParts &parts) {
  RobotAssembly robot;
  const int body_type = pick(catalogue.body_parts, parts.body, "body part");
  robot.body_type = body_type;
  const auto &body = pick(catalogue.body_types, body_type, "body type");
  robot.leg_type = pick(catalogue.leg_parts, parts.legs, "leg part");
  const auto &legs = pick(catalogue.leg_types, robot.leg_type, "leg type");
  robot.body_model = robot_model_path(body_model_for_legs(body, robot.leg_type));
  if (parts.emblem >= 0 && parts.emblem != emblem_none) {
    robot.emblem_model = robot_model_path(body.emblem_model);
    if (parts.emblem < static_cast<int>(catalogue.emblem_images.size())) {
      robot.emblem_image = catalogue.emblem_images[static_cast<std::size_t>(parts.emblem)];
    }
    if (robot.emblem_image.empty() && parts.emblem < static_cast<int>(catalogue.emblems.size())) {
      robot.emblem_image =
          robot_texture_path(catalogue.emblems[static_cast<std::size_t>(parts.emblem)]);
    }
  }
  robot.part_paints[0] = part_paint(body.paint_images, parts.colour);
  robot.body_motions = motion_bank_path(body.motions);
  robot.forearm_turn = body.forearm_turn;
  robot.legs_model = robot_model_path(legs.model);
  robot.part_paints[1] = part_paint(legs.paint_images, parts.colour);
  robot.leg_motions = motion_bank_path(legs.motions);
  robot.timings = legs.timings;
  robot.source_melee_motions = legs.source_melee_motions;
  robot.source_motion_actions = legs.source_motion_actions;
  robot.source_turn_frames = legs.source_turn_frames;
  robot.source_transition_frames = legs.source_transition_frames;
  // 0x080e41 keeps the radius within 0..2.
  robot.radius = std::clamp(legs.radius, 0.0F, max_robot_radius);
  robot.shadow_mesh = legs.shadow_mesh;
  if (!legs.paints.empty()) {
    const int colour =
        parts.colour >= 0 && parts.colour < static_cast<int>(legs.paints.size()) ? parts.colour : 0;
    robot.paint = robot_texture_path(legs.paints[static_cast<std::size_t>(colour)]);
  }
  const auto &skeleton = pick(skeletons.legs, robot.leg_type, "leg skeleton");
  robot.skeleton = skeleton ? &*skeleton : nullptr;
  // Mixed makers (a port addition): the legs' skeleton carries bones for
  // its own maker's body only.
  const auto &homes = catalogue.body_leg_types;
  if (robot.skeleton != nullptr && body_type >= 0 && body_type < static_cast<int>(homes.size()) &&
      leg_maker(homes, robot.leg_type) != body_type) {
    const int home = homes[static_cast<std::size_t>(body_type)];
    if (home >= 0 && home < static_cast<int>(skeletons.legs.size()) &&
        skeletons.legs[static_cast<std::size_t>(home)]) {
      robot.grafted =
          graft_body_section(*robot.skeleton, *skeletons.legs[static_cast<std::size_t>(home)]);
      if (robot.grafted) {
        robot.skeleton = robot.grafted.get();
      }
    }
  }
  // The arm motions place the arms on the body (their tracks carry the
  // joints' translations), so a body on another maker's legs swings its
  // arms from its own maker's first legs' banks.
  const auto *arm_legs = &legs;
  if (robot.grafted) {
    arm_legs =
        &catalogue.leg_types[static_cast<std::size_t>(homes[static_cast<std::size_t>(body_type)])];
  }
  for (std::size_t side = 0; side < 2; ++side) {
    if (parts.arms[side] >= 0) {
      const int arm_type = pick(catalogue.arm_parts, parts.arms[side], "arm part");
      const auto &arm = pick(catalogue.arm_types, arm_type, "arm type");
      robot.arm_models[side] = robot_model_path(arm.models[side]);
      robot.part_paints[side + 2] = part_paint(arm.paint_images, parts.colour);
      const int bank = arm_legs->arm_motions[side][arm.arm_class == 0 ? 0 : 1];
      robot.arm_motions[side] = motion_bank_path(bank);
      const int skeleton_type = body_arm_type(catalogue, skeletons, side, arm_type, body_type);
      const auto &arm_skeleton = pick(skeletons.arms[side], skeleton_type, "arm skeleton");
      robot.arm_skeletons[side] = arm_skeleton ? &*arm_skeleton : nullptr;
    }
    if (parts.shoulders[side] >= 0) {
      const int type = pick(catalogue.shoulder_parts, parts.shoulders[side], "shoulder part");
      if (type >= 0) {
        const auto &shoulder = pick(catalogue.shoulder_types, type, "shoulder type");
        robot.shoulder_models[side] = robot_model_path(shoulder.models[side]);
        robot.part_paints[side + 4] = part_paint(shoulder.paint_images, parts.colour);
        robot.shoulder_shifts[side] = shoulder_shift(catalogue, side, type, body_type);
      }
    }
  }
  return robot;
}

} // namespace phantomcrash
