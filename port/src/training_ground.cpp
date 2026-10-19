#include "phantomcrash/training_ground.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {
namespace {

Vec3 vec3_or(const json::JsonValue &value, const char *key, const Vec3 fallback) {
  const json::JsonValue *found = value.find(key);
  if (found == nullptr) {
    return fallback;
  }
  const auto v = json::numbers<3>(*found, key);
  return {v[0], v[1], v[2]};
}

float float_or(const json::JsonValue &value, const char *key, const float fallback) {
  return static_cast<float>(value.number_or(key, fallback));
}

ScreenPass parse_screen(const json::JsonValue &value) {
  constexpr float byte_max = 255.0F;
  ScreenPass screen;
  if (const json::JsonValue *tint = value.find("tint_argb")) {
    const auto argb = json::numbers<4>(*tint, "tint_argb");
    screen.tint = {argb[1] / byte_max, argb[2] / byte_max, argb[3] / byte_max};
  }
  if (const json::JsonValue *levels = value.find("levels")) {
    screen.levels_min = vec3_or(*levels, "min", screen.levels_min);
    screen.levels_max = vec3_or(*levels, "max", screen.levels_max);
  }
  if (const json::JsonValue *blur = value.find("depth_of_field")) {
    screen.depth_of_field = blur->boolean_or("enabled", true);
    screen.focus = float_or(*blur, "focus", screen.focus);
    screen.focus_scale = float_or(*blur, "focus_scale", screen.focus_scale);
    screen.focus_base = float_or(*blur, "focus_base", screen.focus_base);
    screen.ramp = float_or(*blur, "ramp", screen.ramp);
    screen.depth_near = float_or(*blur, "depth_near", screen.depth_near);
    screen.depth_far = float_or(*blur, "depth_far", screen.depth_far);
    screen.blur_texels = float_or(*blur, "blur_texels", screen.blur_texels);
    screen.day_focus_range = float_or(*blur, "day_focus_range", screen.day_focus_range);
  }
  if (const auto *wave = value.find("distortion")) {
    screen.distortion = wave->boolean_or("enabled", false);
    screen.wave_amplitude = float_or(*wave, "amplitude", screen.wave_amplitude);
    screen.wave_phase_step = float_or(*wave, "phase_step", screen.wave_phase_step);
    screen.wave_update_hz = float_or(*wave, "update_hz", screen.wave_update_hz);
  }
  return screen;
}

StageLighting parse_lighting(const json::JsonValue &value) {
  StageLighting lighting;
  if (const json::JsonValue *light = value.find("lighting")) {
    lighting.ambient = vec3_or(*light, "ambient", lighting.ambient);
    lighting.sun_direction = normalize(vec3_or(*light, "sun_direction", lighting.sun_direction));
    lighting.sun_colour = vec3_or(*light, "sun_colour", lighting.sun_colour);
    lighting.clear_colour = vec3_or(*light, "clear_colour", lighting.clear_colour);
    lighting.source_lights = parse_source_lights(*light);
    if (const json::JsonValue *shadow = light->find("shadow_colour")) {
      lighting.shadow_colour = json::numbers<4>(*shadow, "shadow_colour");
    }
  }
  if (const json::JsonValue *fog = value.find("fog")) {
    lighting.fog = fog->boolean_or("enabled", lighting.fog);
    lighting.fog_colour = vec3_or(*fog, "colour", lighting.fog_colour);
    lighting.fog_start = float_or(*fog, "start", lighting.fog_start);
    lighting.fog_end = float_or(*fog, "end", lighting.fog_end);
  }
  if (const json::JsonValue *screen = value.find("screen")) {
    lighting.screen = parse_screen(*screen);
  }
  return lighting;
}

} // namespace

std::map<std::string, StageDefinition> parse_stages(const json::JsonValue &document) {
  std::map<std::string, StageDefinition> stages;
  for (const auto &[name, value] : json::required(document, "stages").object()) {
    try {
      StageDefinition stage;
      stage.name = name;
      stage.model = value.string_or("model", "");
      stage.collision = value.string_or("collision", "");
      stage.collision_fixes = value.string_or("collision_fixes", "");
      stage.enemy_path = value.string_or("enemy_path", "");
      if (const auto *textures = value.find("two_sided_textures")) {
        for (const auto &texture : textures->array()) {
          stage.two_sided_textures.push_back(texture.string());
        }
      }
      if (const auto *nodes = value.find("shadow_nodes")) {
        for (const auto &node : nodes->array()) {
          stage.shadow_nodes.push_back(node.string());
        }
      }
      if (const auto *monitor = document.find("monitor")) {
        stage.monitor = parse_battle_monitor(*monitor);
      }
      if (const auto *monitor = value.find("monitor")) {
        stage.monitor = parse_battle_monitor(*monitor);
      }
      if (const auto *nodes = value.find("monitor_nodes")) {
        for (const auto &node : nodes->array()) {
          stage.monitor_nodes.push_back(node.string());
        }
      }
      if (const json::JsonValue *sky = value.find("sky_nodes")) {
        for (const auto &node : sky->array()) {
          stage.sky_nodes.push_back(node.string());
        }
      }
      if (const auto *motions = value.find("sky_motion")) {
        for (const auto &entry : motions->array()) {
          SkyMotion motion;
          motion.node = json::required(entry, "node").string();
          if (std::ranges::find(stage.sky_nodes, motion.node) == stage.sky_nodes.end()) {
            throw std::runtime_error("sky_motion node is not a sky node: " + motion.node);
          }
          motion.rotation_per_second = float_or(entry, "rotation_per_second", 0.0F);
          if (const auto *uv = entry.find("uv_per_second")) {
            motion.uv_per_second = json::numbers<2>(*uv, "sky_motion uv_per_second");
          }
          stage.sky_motion.push_back(std::move(motion));
        }
      }
      stage.lighting = parse_lighting(value);
      stage.scene_stage = static_cast<int>(value.number_or("scene_stage", -1.0));
      stage.door_model = value.string_or("door_model", "");
      stage.props_model = value.string_or("props_model", "");
      if (const auto *pickups = value.find("supply_pickups")) {
        stage.supply_pickups = parse_supply_pickup_look(*pickups);
      }
      if (const auto *routes = value.find("supply_routes")) {
        stage.supply_routes = parse_supply_route_list(*routes);
      }
      if (value.find("spawn")) {
        stage.spawn = vec3_or(value, "spawn", {});
      }
      if (const auto *spawns = value.find("player_spawns")) {
        for (const auto &point : spawns->array()) {
          const auto xyz = json::numbers<3>(point, "player spawn");
          stage.player_spawns.push_back({xyz[0], xyz[1], xyz[2]});
        }
      }
      stages.emplace(name, std::move(stage));
    } catch (const std::exception &error) {
      throw std::runtime_error("stages.json, stage " + name + ": " + error.what());
    }
  }
  return stages;
}

void apply_stage(SceneTuning &scenes, const StageDefinition &stage) {
  if (stage.scene_stage >= 0) {
    scenes.stage = stage.scene_stage;
  }
  if (!stage.door_model.empty()) {
    scenes.doors.model = stage.door_model;
  }
}

TrainingScript parse_training(const json::JsonValue &document) {
  try {
    TrainingScript script;
    script.stage = json::required(document, "stage").string();
    script.music = document.string_or("music", "");
    script.return_to = document.string_or("return_to", "pepper_guide");
    for (const auto &[name, value] : json::required(document, "lessons").object()) {
      script.lessons[name] = {value.string_or("title", name),
                              static_cast<int>(value.number_or("mode", 3)),
                              static_cast<int>(value.number_or("screens", 1))};
    }
    if (const json::JsonValue *robots = document.find("robots"); robots && robots->is_object()) {
      for (const auto &[name, value] : robots->object()) {
        script.robots[name] = parse_robot_parts(value);
      }
    }
    if (const json::JsonValue *battle = document.find("battle")) {
      script.battle = parse_training_battle(*battle);
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("training.json: ") + error.what());
  }
}

RobotLoadout training_loadout(const TrainingScript &script, const std::string &name,
                              const TrainingRobot &robot) {
  RobotLoadout loadout;
  loadout.chip = robot.chip;
  loadout.chip_level = robot.chip_level;
  if (const auto parts = script.robots.find(name); parts != script.robots.end()) {
    loadout.body = parts->second.body;
    loadout.body_level = robot.body_level;
    loadout.legs = parts->second.legs;
    loadout.legs_level = robot.legs_level;
  }
  const auto &weapons = script.battle.settings.weapons;
  for (const std::string &weapon : robot.weapons) {
    if (const auto found = weapons.find(weapon); found != weapons.end() && found->second.part) {
      loadout.weapons.push_back(*found->second.part);
    }
  }
  return loadout;
}

} // namespace phantomcrash
