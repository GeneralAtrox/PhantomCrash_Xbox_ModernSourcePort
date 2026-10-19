// The garage's robot preview: its camera, where the robot stands and the
// parts shown one by one.

#include "phantomcrash/robot_preview.hpp"

#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

Vec3 vec3(const json::JsonValue &value, const char *what) {
  const auto numbers = json::numbers<3>(value, what);
  return {numbers[0], numbers[1], numbers[2]};
}

Vec3 direction(const json::JsonValue &value, const char *what) {
  const Vec3 way = vec3(value, what);
  if (length(way) <= 0.0F) {
    throw std::runtime_error(std::string(what) + " has no length");
  }
  return normalize(way);
}

PreviewPlacement parse_place(const json::JsonValue &value, const char *key) {
  PreviewPlacement place;
  place.position = vec3(json::required(value, key), key);
  place.yaw = static_cast<float>(value.number_or("yaw", 0.0));
  return place;
}

std::vector<int> ints(const json::JsonValue &value) {
  std::vector<int> out;
  for (const json::JsonValue &item : value.array()) {
    out.push_back(static_cast<int>(item.number()));
  }
  return out;
}

// The leg places and slide-in a screen may have.
void parse_screen_extras(const json::JsonValue &value, const float frame_rate,
                         RobotPreviewScreen &screen) {
  if (const auto *legs = value.find("leg_positions")) {
    for (const json::JsonValue &at : legs->array()) {
      screen.leg_positions.push_back(vec3(at, "leg_positions"));
    }
  }
  screen.parts_at_leg_positions = value.boolean_or("parts_at_leg_positions", false);
  if (const auto *clip = value.find("part_clip")) {
    const auto rect = json::numbers<4>(*clip, "part_clip");
    if (rect[2] <= rect[0] || rect[3] <= rect[1]) {
      throw std::runtime_error("part_clip needs right above left and bottom below top");
    }
    screen.part_clip = rect;
  }
  if (const auto *slide = value.find("slide_in")) {
    screen.slide_offset = vec3(json::required(*slide, "offset"), "offset");
    const double ease = json::required(*slide, "ease").number();
    if (ease <= 0.0 || ease >= 1.0) {
      throw std::runtime_error("slide_in.ease must be between 0 and 1");
    }
    screen.slide_rate = static_cast<float>(-std::log(1.0 - ease) * frame_rate);
  }
}

LonePartDisplay parse_lone_part(const json::JsonValue &value, const float frame_rate) {
  LonePartDisplay display;
  const double frames = json::required(value, "pop_in_frames").number();
  display.pop_in_seconds = static_cast<float>(frames / frame_rate);
  display.frame_rate = frame_rate;
  display.pop_in_pivot = vec3(json::required(value, "pop_in_pivot"), "pop_in_pivot");
  for (const json::JsonValue &place : json::required(value, "places").array()) {
    const auto numbers = json::numbers<6>(place, "places");
    display.places.push_back(
        {{numbers[0], numbers[1], numbers[2]}, {numbers[3], numbers[4], numbers[5]}});
  }
  constexpr std::array<const char *, 4> kinds{"body", "legs", "arms", "shoulders"};
  const auto &by_type = json::required(value, "by_type");
  for (std::size_t kind = 0; kind < kinds.size(); ++kind) {
    display.by_type[kind] = ints(json::required(by_type, kinds[kind]));
    for (const int place : display.by_type[kind]) {
      if (place < 0 || place >= static_cast<int>(display.places.size())) {
        throw std::runtime_error(std::string("lone_part.by_type.") + kinds[kind] +
                                 " names a place out of range");
      }
    }
  }
  return display;
}

// The lone part's place from the robot's root (0x084855).
Mat4 lone_place(const LonePartDisplay &display, const std::size_t kind, const int type) {
  const auto &types = display.by_type[kind];
  if (type < 0 || type >= static_cast<int>(types.size())) {
    return Mat4{};
  }
  const LonePartPlace &at = display.places[static_cast<std::size_t>(types[type])];
  return rotation_x(at.turn.x) * rotation_y(at.turn.y) * rotation_z(at.turn.z) *
         translation(at.offset);
}

// The type of a part id, -1 when unknown.
int part_type(const std::vector<int> &parts, const int id) {
  return id < 0 || id >= static_cast<int>(parts.size()) ? -1 : parts[static_cast<std::size_t>(id)];
}

// A model resource of a part's type, -1 when the id or type is unknown.
int type_model(const std::vector<int> &parts, const int id, const auto &types, auto model) {
  if (id < 0 || id >= static_cast<int>(parts.size())) {
    return -1;
  }
  const int type = parts[static_cast<std::size_t>(id)];
  if (type < 0 || type >= static_cast<int>(types.size())) {
    return -1;
  }
  return model(types[static_cast<std::size_t>(type)]);
}

} // namespace

RobotPreviewScript parse_robot_preview(const json::JsonValue &document) {
  try {
    RobotPreviewScript script;
    script.layer = static_cast<int>(json::required(document, "layer").number());
    const auto &camera = json::required(document, "camera");
    script.eye = vec3(json::required(camera, "eye"), "eye");
    script.target = vec3(json::required(camera, "target"), "target");
    script.fov_degrees = static_cast<float>(json::required(camera, "fov_degrees").number());
    script.near_plane = static_cast<float>(json::required(camera, "near").number());
    script.far_plane = static_cast<float>(json::required(camera, "far").number());
    if (script.fov_degrees <= 0.0F || script.near_plane <= 0.0F ||
        script.far_plane <= script.near_plane) {
      throw std::runtime_error("camera needs a fov above zero and near below far");
    }
    const auto frame_rate = static_cast<float>(json::required(document, "frame_rate").number());
    const auto &light = json::required(document, "light");
    const Vec3 light_direction = direction(json::required(light, "direction"), "direction");
    script.diffuse = static_cast<float>(light.number_or("diffuse", 1.0));
    script.ambient = static_cast<float>(light.number_or("ambient", 0.0));
    script.animate = document.boolean_or("animate", false);
    for (const auto &[name, value] : json::required(document, "screens").object()) {
      try {
        RobotPreviewScreen screen;
        screen.place = parse_place(value, "position");
        screen.spin = radians(static_cast<float>(value.number_or("spin_degrees_per_frame", 0.0)) *
                              frame_rate);
        const auto *own = value.find("light_direction");
        screen.light_direction =
            own == nullptr ? light_direction : direction(*own, "light_direction");
        parse_screen_extras(value, frame_rate, screen);
        script.screens[name] = screen;
      } catch (const std::exception &error) {
        throw std::runtime_error("screens." + name + ": " + error.what());
      }
    }
    const auto &display = json::required(document, "part_display");
    for (std::size_t part = 0; part < preview_parts; ++part) {
      const char *key = part_slot_names[part];
      script.part_display[part] = json::member(
          display, key, [](const auto &value) { return parse_place(value, "offset"); });
    }
    if (const auto *types = document.find("body_leg_types")) {
      script.body_leg_types = ints(*types);
    }
    script.lone_part = parse_lone_part(json::required(document, "lone_part"), frame_rate);
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("robot_preview.json: ") + error.what());
  }
}

RobotParts PreviewRobot::parts() const {
  RobotParts parts;
  parts.body = body;
  parts.legs = legs;
  parts.arms = arms;
  parts.shoulders = shoulders;
  parts.colour = colour;
  return parts;
}

PreviewRobot preview_robot(const Garage &garage, const Hangar &hangar) {
  const auto id = [&](const PartSlot slot) {
    const auto found = garage.stock.find(hangar.part(slot));
    return found == garage.stock.end() ? -1 : found->second.id;
  };
  PreviewRobot robot;
  robot.body = id(PartSlot::body);
  robot.legs = id(PartSlot::legs);
  robot.arms = {id(PartSlot::right_arm), id(PartSlot::left_arm)};
  robot.shoulders = {id(PartSlot::right_shoulder), id(PartSlot::left_shoulder)};
  robot.colour = hangar.paint;
  return robot;
}

PreviewRobot preview_record(const RobotRecord &record) {
  const auto id = [&](const PartSlot slot) {
    const auto &part = record.part(slot);
    return part ? part->id : -1;
  };
  PreviewRobot robot;
  robot.body = id(PartSlot::body);
  robot.legs = id(PartSlot::legs);
  robot.arms = {id(PartSlot::right_arm), id(PartSlot::left_arm)};
  robot.shoulders = {id(PartSlot::right_shoulder), id(PartSlot::left_shoulder)};
  robot.colour = record.paint;
  return robot;
}

PreviewRobot preview_part(const PartKind kind, const int id) {
  PreviewRobot robot;
  robot.lone = true;
  switch (kind) {
  case PartKind::body:
    robot.body = id;
    break;
  case PartKind::legs:
    robot.legs = id;
    break;
  case PartKind::arm:
    robot.arms[0] = id;
    break;
  case PartKind::shoulder:
    robot.shoulders[0] = id;
    break;
  case PartKind::chip:
  case PartKind::module:
    break; // no model
  }
  return robot;
}

std::vector<PreviewPart> preview_part_display(const RobotCatalogue &catalogue,
                                              const RobotPreviewScript &script,
                                              const PreviewRobot &robot) {
  const auto side_model = [](const std::size_t side) {
    return [side](const auto &type) { return type.models[side]; };
  };
  const std::array<int, preview_parts> models{
      type_model(catalogue.body_parts, robot.body, catalogue.body_types,
                 [](const auto &type) { return type.model; }),
      type_model(catalogue.leg_parts, robot.legs, catalogue.leg_types,
                 [](const auto &type) { return type.model; }),
      type_model(catalogue.arm_parts, robot.arms[0], catalogue.arm_types, side_model(0)),
      type_model(catalogue.arm_parts, robot.arms[1], catalogue.arm_types, side_model(1)),
      type_model(catalogue.shoulder_parts, robot.shoulders[0], catalogue.shoulder_types,
                 side_model(0)),
      type_model(catalogue.shoulder_parts, robot.shoulders[1], catalogue.shoulder_types,
                 side_model(1))};
  std::vector<PreviewPart> parts;
  if (robot.lone) {
    // The first part it has (0x084855), placed by its model's type.
    const std::array<int, preview_parts> types{
        part_type(catalogue.body_parts, robot.body),
        part_type(catalogue.leg_parts, robot.legs),
        part_type(catalogue.arm_parts, robot.arms[0]),
        part_type(catalogue.arm_parts, robot.arms[1]),
        part_type(catalogue.shoulder_parts, robot.shoulders[0]),
        part_type(catalogue.shoulder_parts, robot.shoulders[1])};
    constexpr std::array<std::size_t, preview_parts> kind_of{0, 1, 2, 2, 3, 3};
    for (std::size_t part = 0; part < preview_parts; ++part) {
      if (models[part] >= 0) {
        parts.push_back({robot_model_path(models[part]),
                         lone_place(script.lone_part, kind_of[part], types[part])});
        break;
      }
    }
    return parts;
  }
  for (std::size_t part = 0; part < preview_parts; ++part) {
    if (models[part] < 0) {
      continue;
    }
    const PreviewPlacement &at = script.part_display[part];
    parts.push_back(
        {robot_model_path(models[part]), rotation_y(at.yaw) * translation(at.position)});
  }
  return parts;
}

Mat4 preview_root(const RobotPreviewScreen &screen, const double seconds, const bool turns) {
  constexpr double turn = 6.283185307179586;
  const double spin = turns ? screen.spin : 0.0F;
  const double yaw = std::remainder(screen.place.yaw + spin * seconds, turn);
  return rotation_y(static_cast<float>(yaw)) * translation(screen.place.position);
}

Mat4 preview_root(const RobotPreviewScreen &screen, Vec3 spot, const double seconds,
                  const bool standing) {
  if (standing && screen.slide_rate > 0.0F) {
    const auto gap = static_cast<float>(std::exp(-screen.slide_rate * seconds));
    spot = spot + screen.slide_offset * gap;
  }
  RobotPreviewScreen placed = screen;
  placed.place.position = spot;
  return preview_root(placed, seconds, standing);
}

Vec3 preview_spot(const RobotCatalogue &catalogue, const RobotPreviewScript &script,
                  const RobotPreviewScreen &screen, const PreviewRobot &robot) {
  if (screen.leg_positions.empty() || robot.lone ||
      (!robot.standing() && !screen.parts_at_leg_positions)) {
    return screen.place.position;
  }
  // The legs' type, else the leg type the body's type stands as (0x082d3d).
  int type = part_type(catalogue.leg_parts, robot.legs);
  if (type < 0) {
    const int body = part_type(catalogue.body_parts, robot.body);
    if (body >= 0 && body < static_cast<int>(script.body_leg_types.size())) {
      type = script.body_leg_types[static_cast<std::size_t>(body)];
    }
  }
  if (type < 0 || type >= static_cast<int>(screen.leg_positions.size())) {
    return screen.place.position;
  }
  return screen.leg_positions[static_cast<std::size_t>(type)];
}

float lone_part_scale(const LonePartDisplay &display, const double seconds) {
  if (display.pop_in_seconds <= 0.0F) {
    return 1.0F;
  }
  // Whole frames, as the original counts them.
  const double frames = std::floor(seconds * display.frame_rate + 1e-6);
  const double full = std::round(display.pop_in_seconds * display.frame_rate);
  return static_cast<float>(std::clamp(frames / full, 0.0, 1.0));
}

Mat4 lone_part_pop_in(const LonePartDisplay &display, const float scale) {
  return translation(display.pop_in_pivot * -1.0F) * scaling({scale, scale, scale}) *
         translation(display.pop_in_pivot);
}

} // namespace phantomcrash
