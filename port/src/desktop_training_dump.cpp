// What the scene dump says of a battle (docs/SCENE_DUMP.md): its kind,
// stage, clock, camera, entrance or exit scene and each robot.

#include "desktop_training.hpp"
#include "desktop_training_finale.hpp"
#include "desktop_training_vs.hpp"

#include <array>
#include <cstdio>
#include <numbers>

namespace phantomcrash {
namespace {

using Object = json::JsonValue::Object;
using Array = json::JsonValue::Array;

constexpr std::array<const char *, 11> mode_names{"stand",  "walk",     "turn",     "dash",
                                                  "jump",   "dodge",    "brake",    "hit",
                                                  "strike", "scripted", "destroyed"};

json::JsonValue text(std::string value) {
  return {std::move(value)};
}

json::JsonValue whole(const std::int64_t value) {
  return {value};
}

// Two decimals keep the dump short.
json::JsonValue rounded(const double value) {
  return {static_cast<double>(static_cast<std::int64_t>(value * 100.0 + (value < 0 ? -0.5 : 0.5))) /
          100.0};
}

std::string clock_text(const double seconds) {
  char buffer[16];
  const int whole_seconds = static_cast<int>(seconds);
  std::snprintf(buffer, sizeof buffer, "%d:%02d", whole_seconds / 60, whole_seconds % 60);
  return buffer;
}

json::JsonValue robot_details(const Combatant &robot, const int index, const BattleScene *scene) {
  const Vec3 at = robot.mover.position();
  const auto mode = static_cast<std::size_t>(robot.mover.mode());
  Object out{{"index", whole(index)},
             {"name", text(robot.name)},
             {"team", whole(robot.team)},
             {"hp", rounded(robot.hp)},
             {"max_hp", rounded(robot.max_hp)},
             {"alive", {robot.alive}},
             {"mode", text(mode < mode_names.size() ? mode_names[mode] : "?")},
             {"position", {Array{rounded(at.x), rounded(at.y), rounded(at.z)}}},
             {"heading_degrees", rounded(robot.mover.heading() * 180.0 / std::numbers::pi)}};
  if (!robot.tag.empty()) {
    out.push_back({"tag", text(robot.tag)});
  }
  for (const auto &[flag, set] :
       {std::pair{"piloted", robot.piloted}, std::pair{"rival", robot.rival},
        std::pair{"boss", robot.boss}, std::pair{"drone", robot.drone}}) {
    if (set) {
      out.push_back({flag, {true}});
    }
  }
  if (scene != nullptr && scene->active()) {
    out.push_back({"scene_state", whole(scene->robot_state(index))});
  }
  return {std::move(out)};
}

} // namespace

std::optional<SceneProgress> TrainingScreen::scene_progress() const {
  if (finale_) {
    const FinalePhase *phase = finale_->sequence.phase();
    const SceneTrack *track = finale_->sequence.scene();
    if (phase == nullptr || track == nullptr) {
      return std::nullopt;
    }
    return SceneProgress{"Final battle scene (state " + std::to_string(phase->state) + ")",
                         static_cast<int>(finale_->sequence.scene_frame()),
                         static_cast<int>(track->frames)};
  }
  const auto playing = scene_ ? scene_->scene().playback() : std::nullopt;
  if (!playing) {
    return std::nullopt;
  }
  std::string label;
  if (playing->state == scene_state::exit) {
    label = "Exit";
  } else if (playing->state == scene_state::boss) {
    label = "Boss entrance";
  } else if (playing->robot >= 0) {
    label = "Gate walk-out";
  } else {
    label = "Entrance (shot " + std::to_string(playing->state) + ")";
  }
  if (playing->robot >= 0 && battle_ &&
      playing->robot < static_cast<int>(battle_->robots().size())) {
    label += ": " + battle_->robots()[static_cast<std::size_t>(playing->robot)].name;
  }
  return SceneProgress{label, static_cast<int>(playing->frame), static_cast<int>(playing->total)};
}

json::JsonValue TrainingScreen::scene_details() const {
  Array kinds;
  std::string summary;
  const auto kind = [&](const char *name, const char *label) {
    kinds.push_back(text(name));
    summary += (summary.empty() ? "" : ", ") + std::string(label);
  };
  if (finale_) {
    kind("finale", "final battle");
  }
  if (vs_) {
    kind("vs", "VS battle");
  }
  if (coop_match_) {
    kind("coop_match", "co-op match");
  }
  if (match_results_ && !finale_) {
    kind("rumbling", "RUMBLING contest");
  }
  if (free_play_) {
    kind("free_play", "free play");
  }
  if (in_scene()) {
    kind("scripted_scene", "in a scripted scene");
  }
  if (paused_) {
    kind("paused", "paused");
  }
  const BattleScene *scene = scene_ ? &scene_->scene() : nullptr;
  Object out{{"stage", text(script_.stage)},
             {"lesson_mode", whole(mode_)},
             {"clock", text(clock_text(clock_))},
             {"camera_view", whole(views_.view())},
             {"split_screens", whole(screens_)}};
  if (live_area_ >= 0) {
    out.push_back({"live_area", whole(live_area_)});
  }
  if (scene != nullptr) {
    out.push_back({"entrance_scene",
                   {Object{{"state", whole(scene->state())},
                           {"active", {scene->active()}},
                           {"cinematic", {scene->cinematic()}},
                           {"finished", {scene->finished()}}}}});
  }
  if (vs_) {
    out.push_back({"vs_decided_seconds", rounded(vs_->decided_seconds)});
  }
  if (battle_ && !battle_->robots().empty()) {
    Array robots;
    const auto &all = battle_->robots();
    for (std::size_t index = 0; index < all.size(); ++index) {
      robots.push_back(robot_details(all[index], static_cast<int>(index), scene));
    }
    const Combatant &player = battle_->player();
    summary += (summary.empty() ? "" : ", ") + std::to_string(all.size()) + " robots, " +
               clock_text(clock_) + " in, player " + player.name + " " +
               std::to_string(static_cast<int>(player.hp)) + "/" +
               std::to_string(static_cast<int>(player.max_hp)) + " HP";
    out.push_back({"robots", {std::move(robots)}});
  }
  out.insert(out.begin(), {"kinds", {std::move(kinds)}});
  out.insert(out.begin(), {"summary", text("battle on " + script_.stage + ": " + summary)});
  return {std::move(out)};
}

} // namespace phantomcrash
