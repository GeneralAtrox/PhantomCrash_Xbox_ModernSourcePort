#include "phantomcrash/stage_objects.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/skeleton.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace phantomcrash {
namespace {
Mat4 rotation_z(float angle) {
  Mat4 out;
  out.at(0, 0) = std::cos(angle);
  out.at(0, 1) = std::sin(angle);
  out.at(1, 0) = -std::sin(angle);
  out.at(1, 1) = std::cos(angle);
  return out;
}

StageObject parse_object(const json::JsonValue &value) {
  StageObject object;
  object.kind = static_cast<int>(value.number_or("kind", -1));
  object.mesh = static_cast<int>(json::required(value, "mesh").number());
  object.wreck_mesh = static_cast<int>(value.number_or("wreck_mesh", -1));
  object.break_effect = static_cast<int>(value.number_or("break_effect", -1));
  object.wreck_break_effect = static_cast<int>(value.number_or("wreck_break_effect", -1));
  object.break_sound = value.string_or("break_sound", "");
  object.wreck_solid = value.boolean_or("wreck_solid", true);
  object.wreck_hit_delay = static_cast<float>(value.number_or("wreck_hit_delay", 0));
  const auto p = json::numbers<3>(json::required(value, "position"), "position");
  const auto r = json::numbers<3>(json::required(value, "rotation"), "rotation");
  object.position = {p[0], p[1], p[2]};
  object.world =
      rotation_x(r[0]) * rotation_y(r[1]) * rotation_z(r[2]) * translation(object.position);
  Vec3 low{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
           std::numeric_limits<float>::max()};
  Vec3 high = low * -1.0F;
  std::vector<CollisionTriangle> triangles;
  for (const auto &entry : json::required(value, "collision").array()) {
    const auto points = json::numbers<9>(entry, "collision triangle");
    CollisionTriangle triangle;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      const Vec3 point = transform_point(
          {points[corner * 3], points[corner * 3 + 1], points[corner * 3 + 2]}, object.world);
      triangle.points[corner] = point;
      low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
      high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
    }
    triangle.normal = normalize(
        cross(triangle.points[1] - triangle.points[0], triangle.points[2] - triangle.points[0]));
    triangles.push_back(triangle);
  }
  if (triangles.empty()) {
    throw std::runtime_error("Stage object has no collision triangles");
  }
  object.centre = (low + high) * 0.5F;
  object.radius = length(high - low) * 0.5F;
  object.collision = StageCollision(std::move(triangles), {});
  if (const auto *wreck = value.find("wreck_collision")) {
    std::vector<CollisionTriangle> wreck_triangles;
    for (const auto &entry : wreck->array()) {
      const auto points = json::numbers<9>(entry, "wreck collision triangle");
      CollisionTriangle triangle;
      for (std::size_t corner = 0; corner < 3; ++corner) {
        triangle.points[corner] = transform_point(
            {points[corner * 3], points[corner * 3 + 1], points[corner * 3 + 2]}, object.world);
      }
      triangle.normal = normalize(
          cross(triangle.points[1] - triangle.points[0], triangle.points[2] - triangle.points[0]));
      wreck_triangles.push_back(triangle);
    }
    if (!wreck_triangles.empty()) {
      object.wreck_collision = StageCollision(std::move(wreck_triangles), {});
    }
  }
  return object;
}
} // namespace

StageObjects parse_stage_objects(const json::JsonValue &document, const int stage) {
  for (const auto &entry : document.array()) {
    if (entry.number_or("stage", -1) != stage) {
      continue;
    }
    std::vector<StageObject> objects;
    for (const auto &value : json::required(entry, "objects").array()) {
      if (value.number_or("kind", -1) != 6) {
        objects.push_back(parse_object(value));
      }
    }
    return StageObjects(std::move(objects));
  }
  throw std::runtime_error("Stage objects do not contain stage " + std::to_string(stage));
}

void StageObjects::tick(const float seconds) {
  for (auto &object : objects_) {
    object.cooldown = std::max(0.0F, object.cooldown - seconds);
  }
}

unsigned StageObjects::push_sphere(Vec3 &centre, const float radius) const {
  unsigned contact = 0;
  for (const auto &object : objects_) {
    if (object.solid()) {
      contact |= object.collision.push_sphere(centre, radius);
    }
  }
  return contact;
}

float StageObjects::ground_height(const Vec3 at, const float step, const float radius) const {
  float height = no_ground;
  for (const auto &object : objects_) {
    if (object.solid()) {
      height = std::max(height, object.collision.ground_height(at, step, radius));
    }
  }
  return height;
}

std::optional<ObjectBreak> StageObjects::hit(const std::size_t index) {
  if (index >= objects_.size()) {
    return std::nullopt;
  }
  auto &object = objects_[index];
  const int effect = object.destroyed ? object.wreck_break_effect : object.break_effect;
  if (object.removed || object.cooldown > 0.0F || effect < 0) {
    return std::nullopt;
  }
  object.removed = object.destroyed || object.wreck_mesh < 0;
  if (!object.destroyed && object.wreck_collision) {
    // Keep wrecks solid, but only where their remaining geometry is visible.
    object.collision = std::move(*object.wreck_collision);
    object.wreck_collision.reset();
  }
  object.destroyed = true;
  object.cooldown = object.wreck_hit_delay;
  if (!object.break_sound.empty()) {
    sounds_.emplace_back(object.break_sound, object.position);
  }
  return ObjectBreak{effect, object.position, object.kind};
}
} // namespace phantomcrash
