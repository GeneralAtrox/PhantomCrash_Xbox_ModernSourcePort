// The battle scenes' tables (the converter's tables/battle_scenes.json) and
// their tuning (data/world/training.json "scenes").
#include "phantomcrash/battle_scene.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

using json::JsonValue;
using json::required;

std::array<MotionChannel, 3> channels(const JsonValue &value, const char *x, const char *y,
                                      const char *z) {
  return {parse_motion_channel(required(value, x)), parse_motion_channel(required(value, y)),
          parse_motion_channel(required(value, z))};
}

SceneTrack parse_track(const JsonValue &value) {
  SceneTrack track;
  track.frames = static_cast<float>(required(value, "frames").number());
  if (track.frames <= 0.0F) {
    throw std::runtime_error("a scene needs frames");
  }
  track.relative = value.boolean_or("relative", false);
  const JsonValue &camera = required(value, "camera");
  track.camera.roll = parse_motion_channel(required(camera, "roll"));
  track.camera.fov = parse_motion_channel(required(camera, "fov"));
  track.camera.eye = channels(camera, "eye_x", "eye_y", "eye_z");
  track.camera.target = channels(camera, "target_x", "target_y", "target_z");
  if (const JsonValue *walk = value.find("walk"); walk != nullptr && walk->is_array()) {
    if (walk->array().size() != 3) {
      throw std::runtime_error("a scene's walk needs three channels");
    }
    track.walk = std::array<MotionChannel, 3>{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      (*track.walk)[axis] = parse_motion_channel(walk->array()[axis]);
    }
  }
  return track;
}

Gate parse_gate(const JsonValue &value) {
  const auto position = json::numbers<3>(required(value, "position"), "position");
  return {{position[0], position[1], position[2]},
          static_cast<float>(required(value, "heading").number())};
}

float number(const JsonValue &value, const char *key, const float fallback) {
  return static_cast<float>(value.number_or(key, fallback));
}

} // namespace

BattleSceneTables parse_battle_scenes(const JsonValue &document) {
  try {
    BattleSceneTables tables;
    for (const auto &[state, track] : required(document, "states").object()) {
      tables.states[std::stoi(state)] = parse_track(track);
    }
    for (const auto &[stage, gates] : required(document, "gates").object()) {
      auto &out = tables.gates[std::stoi(stage)];
      for (const JsonValue &gate : gates.array()) {
        out.push_back(parse_gate(gate));
      }
    }
    return tables;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("battle_scenes.json: ") + error.what());
  }
}

SceneTuning parse_scene_tuning(const JsonValue &value) {
  SceneTuning s;
  s.tables = value.string_or("tables", s.tables);
  s.stage = static_cast<int>(value.number_or("stage", s.stage));
  s.gate = static_cast<int>(value.number_or("gate", s.gate));
  s.entrance = value.boolean_or("entrance", s.entrance);
  s.exit = value.boolean_or("exit", s.exit);
  s.computer_entrance = value.boolean_or("computer_entrance", s.computer_entrance);
  s.computer_entrance_speed = number(value, "computer_entrance_speed", s.computer_entrance_speed);
  s.computer_entrance_skip_stand =
      value.boolean_or("computer_entrance_skip_stand", s.computer_entrance_skip_stand);
  if (!std::isfinite(s.computer_entrance_speed) || s.computer_entrance_speed <= 0.0F) {
    throw std::runtime_error("computer_entrance_speed must be positive and finite");
  }
  s.rider_entrance = value.boolean_or("rider_entrance", s.rider_entrance);
  s.computer_exit = value.boolean_or("computer_exit", s.computer_exit);
  s.regular_battle = value.boolean_or("regular_battle", s.regular_battle);
  s.ready_frame = number(value, "ready_frame", s.ready_frame);
  s.go_frame = number(value, "go_frame", s.go_frame);
  s.stand_frame = number(value, "stand_frame", s.stand_frame);
  s.door_frame = number(value, "door_frame", s.door_frame);
  s.close_frame = number(value, "close_frame", s.close_frame);
  s.play_in_frame = number(value, "play_in_frame", s.play_in_frame);
  s.music_gap_seconds = std::max(0.0F, number(value, "music_gap_seconds", s.music_gap_seconds));
  s.entry_delay = number(value, "entry_delay", s.entry_delay);
  s.blend_frames = number(value, "blend_frames", s.blend_frames);
  s.letterbox_frames = number(value, "letterbox_frames", s.letterbox_frames);
  s.gate_radius = number(value, "gate_radius", s.gate_radius);
  s.gate_height = number(value, "gate_height", s.gate_height);
  s.exit_facing = number(value, "exit_facing", s.exit_facing);
  s.exit_turn = number(value, "exit_turn", s.exit_turn);
  s.exit_step = number(value, "exit_step", s.exit_step);
  s.still_step = number(value, "still_step", s.still_step);
  s.left_armour = number(value, "left_armour", s.left_armour);
  s.defeat_frames = number(value, "defeat_frames", s.defeat_frames);
  s.first_ranker_defeat_frames =
      number(value, "first_ranker_defeat_frames", s.first_ranker_defeat_frames);
  s.defeat_watch = number(value, "defeat_watch", s.defeat_watch);
  s.gone_shield_frames = number(value, "gone_shield_frames", s.gone_shield_frames);
  s.ready_sound = value.string_or("ready_sound", s.ready_sound);
  s.go_sound = value.string_or("go_sound", s.go_sound);
  s.play_in_music = value.string_or("play_in_music", s.play_in_music);
  s.boss_music = value.string_or("boss_music", s.boss_music);
  if (const JsonValue *doors = value.find("doors")) {
    s.doors = parse_door_tuning(*doors);
  }
  return s;
}

} // namespace phantomcrash
