#include "phantomcrash/weapon_mounts.hpp"
#include "phantomcrash/skeleton.hpp"
#include <stdexcept>

namespace phantomcrash {
namespace {
Vec3 vector(const json::JsonValue &value) {
  const auto &a = value.array();
  if (a.size() != 3) {
    throw std::runtime_error("Weapon transform needs three coordinates");
  }
  return {static_cast<float>(a[0].number()), static_cast<float>(a[1].number()),
          static_cast<float>(a[2].number())};
}
const json::JsonValue &field(const json::JsonValue &v, const char *key) {
  const auto *p = v.find(key);
  if (!p) {
    throw std::runtime_error(std::string("Missing weapon transform ") + key);
  }
  return *p;
}
} // namespace

WeaponMounts parse_weapon_mounts(const json::JsonValue &value) {
  WeaponMounts out;
  for (const auto &[model, data] : field(value, "weapons").object()) {
    auto &muzzles = out[model];
    for (const auto &entry : field(data, "muzzles").array()) {
      Mat4 muzzle = translation(vector(field(entry, "position")));
      Mat4 local = muzzle;
      bool first = true;
      for (const auto &node : field(entry, "chain").array()) {
        const Vec3 r = vector(field(node, "rotation"));
        muzzle = muzzle * scaling(vector(field(node, "scale"))) * rotation_x(r.x) *
                 rotation_y(r.y) * rotation_z(r.z) *
                 translation(vector(field(node, "translation")));
        if (first) {
          local = muzzle;
        }
        first = false;
      }
      muzzles.push_back({local, muzzle, static_cast<int>(entry.number_or("bone", -1))});
    }
  }
  return out;
}

CombatAnimation parse_combat_animation(const json::JsonValue &value) {
  CombatAnimation out;
  out.raise_seconds = static_cast<float>(value.number_or("raise_seconds", out.raise_seconds));
  out.fade_frames = static_cast<float>(value.number_or("fade_frames", out.fade_frames));
  out.aim_distance = static_cast<float>(value.number_or("aim_distance", out.aim_distance));
  out.joint_pitch_limit =
      static_cast<float>(value.number_or("joint_pitch_limit", out.joint_pitch_limit));
  if (const auto *aux = value.find("auxiliary")) {
    out.auxiliary_rate = static_cast<float>(aux->number_or("frame_rate", out.auxiliary_rate));
    out.auxiliary_release_turn =
        static_cast<float>(aux->number_or("release_turn", out.auxiliary_release_turn));
    out.auxiliary_loop_end = static_cast<float>(aux->number_or("loop_end", out.auxiliary_loop_end));
    out.auxiliary_loop_length =
        static_cast<float>(aux->number_or("loop_length", out.auxiliary_loop_length));
    out.auxiliary_last = static_cast<float>(aux->number_or("last", out.auxiliary_last));
    out.auxiliary_fire_rate =
        static_cast<float>(aux->number_or("fire_rate", out.auxiliary_fire_rate));
  }
  if (const auto *poses = value.find("arm_poses")) {
    if (poses->array().size() != 2) {
      throw std::runtime_error("Combat animation needs two arm pose indices");
    }
    for (std::size_t side = 0; side < 2; ++side) {
      out.arm_poses[side] = static_cast<int>(poses->array()[side].number());
    }
  }
  if (out.raise_seconds <= 0 || out.aim_distance <= 0 || out.joint_pitch_limit <= 0 ||
      out.auxiliary_rate <= 0 || out.auxiliary_loop_length <= 0 || out.auxiliary_last <= 0) {
    throw std::runtime_error("Invalid combat animation timing or aim limits");
  }
  return out;
}
} // namespace phantomcrash
