// The player's robot's entrance (robot states 1-4: 0x088ffa, 0x08928c,
// 0x0897a2) and exit (state 6: 0x089fa3, 0x08a161, 0x08a496), and the
// gates that start the exit (0x08224d, 0x044839). Computer robots'
// entrances are in battle_scene_robots.cpp.
#include "phantomcrash/battle_scene.hpp"

#include "phantomcrash/battle_chatter.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {

// A battle tick at 30 Hz plays two of the scenes' 60 Hz frames; the exit's
// turn and walk steps are per battle tick.
constexpr float frames_per_tick = 2.0F;
constexpr int entrance_states = scene_state::entrance_last - scene_state::entrance_first + 1;

// `v` in a frame turned by `heading` (the original's y rotation).
Vec3 turned(const Vec3 v, const float heading) {
  const float c = std::cos(heading);
  const float s = std::sin(heading);
  return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}

Vec3 sample(const std::array<MotionChannel, 3> &channels, const float frame) {
  return {channels[0].sample(frame), channels[1].sample(frame), channels[2].sample(frame)};
}

Vec3 mix(const Vec3 a, const Vec3 b, const float k) {
  return a * (1.0F - k) + b * k;
}

bool entering(const int state) {
  return state >= scene_state::entrance_first && state <= scene_state::entrance_last;
}

} // namespace

Vec3 walked_out(const BattleSceneTables &tables, const SceneTuning &tuning, const Gate &gate,
                const int state) {
  const auto found = tables.states.find(state);
  if (found == tables.states.end() || !found->second.walk) {
    return gate.position;
  }
  const SceneTrack &scene = found->second;
  const float end = scene.frames - std::min(tuning.stand_frame, scene.frames);
  Vec3 moved = turned(sample(*scene.walk, end) - sample(*scene.walk, 0.0F), gate.heading);
  moved.y = 0.0F; // 0x090f8a keeps the walk's x and z
  return gate.position + moved;
}

BattleScene::BattleScene(BattleSceneTables tables, SceneTuning tuning, const std::uint32_t seed)
    : tables_(std::make_shared<const BattleSceneTables>(std::move(tables))),
      tuning_(std::move(tuning)), random_(seed) {}

bool BattleScene::start_entrance(Battle &battle) {
  const std::vector<Gate> *list = gates();
  if (!tuning_.entrance || battle.robots().empty() || list == nullptr || list->empty()) {
    return false;
  }
  const int state = scene_state::entrance_first + static_cast<int>(random_() % entrance_states);
  const SceneTrack *scene = track(state);
  if (scene == nullptr) {
    return false;
  }
  gate_ = std::max(tuning_.gate, 0) % static_cast<int>(list->size());
  origin_ = (*list)[static_cast<std::size_t>(gate_)];
  battle.place(battle.player_index(), origin_.position, origin_.heading);
  battle.raise_shield(battle.player_index()); // 0x082552: protected as soon as the robot is placed.
  state_ = state;
  left_ = scene->frames;
  gate_flag_ = 0;
  letterbox_ = 1.0F; // 0x088ffa sets the bars straight in (global +0xef320 1)
  letterbox_way_ = 0.0F;
  if (tuning_.regular_battle) {
    lines_.emplace_back(battle.player_index(), chatter_event::entrance);
  }
  finished_ = false;
  return true;
}

void BattleScene::prepare(Battle &battle, const float seconds) {
  for (const int index : tuning_.computer_entrance ? battle.take_arrivals() : std::vector<int>{}) {
    if (battle.robots()[static_cast<std::size_t>(index)].alive) {
      static_cast<void>(start_robot_entrance(battle, index));
    }
  }
  const float step = seconds * scene_frames_per_second;
  if (letterbox_way_ != 0.0F && tuning_.letterbox_frames > 0.0F) {
    letterbox_ =
        std::clamp(letterbox_ + letterbox_way_ * step / tuning_.letterbox_frames, 0.0F, 1.0F);
  }
  if (finished_ || battle.robots().empty() || seconds <= 0.0F) {
    return;
  }
  tick_defeat(step);
  if (entering(state_)) {
    walk_entrance(battle, seconds, step);
  } else if (state_ == scene_state::exit) {
    walk_exit(battle, seconds, step);
  }
  for (RobotEntrance &entry : robots_) {
    if (entry.state != scene_state::none &&
        entry.robot < static_cast<int>(battle.robots().size())) {
      walk_robot(battle, entry, seconds, step);
    }
  }
}

void BattleScene::walk_entrance(Battle &battle, const float seconds, const float step) {
  // 0x08928c refreshes protection throughout entry, including a held camera scene.
  // Once control returns, the configured shield duration counts down normally.
  battle.raise_shield(battle.player_index());
  if (cinematic()) {
    hold_still(battle, battle.player_index());
    return;
  }
  const float before = left_;
  left_ -= step;
  const auto crossed = [&](const float frame) { return before > frame && left_ <= frame; };
  if (crossed(tuning_.ready_frame)) {
    sounds_.push_back(tuning_.ready_sound);
  }
  if (crossed(tuning_.go_frame)) {
    sounds_.push_back(tuning_.go_sound);
  }
  if (tuning_.regular_battle && crossed(tuning_.play_in_frame)) {
    music_ = tuning_.play_in_music; // modes 0 and 2 only
  }
  open_door(battle, gate_, before, left_);
  if (left_ <= 0.0F) {
    // 0x0897a2: the robot stands and the battle starts; the bars slide out.
    battle.release(battle.player_index());
    state_ = scene_state::none;
    left_ = 0.0F;
    letterbox_way_ = -1.0F;
    return;
  }
  walk_step(battle, battle.player_index(), state_, origin_, before, left_, seconds);
}

void BattleScene::walk_step(Battle &battle, const int index, const int state, const Gate &origin,
                            const float before, const float left, const float seconds) const {
  const SceneTrack *scene = track(state);
  ScriptedStep walk;
  walk.heading = origin.heading;
  if (left >= tuning_.stand_frame && scene != nullptr && scene->walk) {
    Vec3 moved = turned(sample(*scene->walk, scene->frames - left) -
                            sample(*scene->walk, scene->frames - before),
                        origin.heading);
    moved.y = 0.0F; // 0x090f8a keeps the walk's x and z
    const MovementTuning &legs = battle.robots()[static_cast<std::size_t>(index)].mover.tuning();
    if (length(moved) >= tuning_.still_step * (before - left) / frames_per_tick) {
      walk.velocity = moved * (1.0F / seconds);
      walk.motion = robot_motion::walk;
      walk.motion_speed = legs.walk_speed > 0.0F
                              ? length(walk.velocity) / legs.walk_speed * legs.walk_motion_rate
                              : 1.0F;
    }
  }
  battle.script(index, walk);
}

void BattleScene::open_door(Battle &battle, const int gate, const float before,
                            const float left) const {
  // 0x08928c opens the gate's door as the scene passes door_frame.
  if (StageDoors *doors = battle.doors();
      doors != nullptr && before > tuning_.door_frame && left <= tuning_.door_frame) {
    doors->open(gate);
  }
}

void BattleScene::walk_exit(Battle &battle, const float seconds, const float step) {
  const float before = left_;
  left_ -= step;
  if (StageDoors *doors = battle.doors();
      doors != nullptr && before > tuning_.close_frame && left_ <= tuning_.close_frame) {
    doors->close(gate_); // 0x08a161 shuts the door behind the robot
  }
  if (left_ <= 0.0F) {
    // 0x08a496: the scene ends the battle.
    battle.release(battle.player_index());
    left_ = 0.0F;
    finished_ = true;
    return;
  }
  exit_step(battle, battle.player_index(), exit_heading_, origin_.position, seconds, step);
}

void BattleScene::exit_step(Battle &battle, const int index, const float heading, const Vec3 gate,
                            const float seconds, const float step) const {
  const RobotMover &mover = battle.robots()[static_cast<std::size_t>(index)].mover;
  const float ticks = step / frames_per_tick;
  ScriptedStep walk;
  walk.heading = mover.heading();
  const float turn = wrap_angle(heading - mover.heading());
  if (std::fabs(turn) > tuning_.exit_facing) {
    // It turns on the spot with its legs held mid-stride.
    const float most = tuning_.exit_turn * ticks;
    walk.heading = mover.heading() + std::clamp(turn, -most, most);
    walk.motion = robot_motion::walk;
    walk.motion_speed = 0.0F;
    battle.script(index, walk);
    return;
  }
  Vec3 to_gate = gate - mover.position();
  to_gate.y = 0.0F;
  const float distance = std::min(length(to_gate), tuning_.exit_step * ticks);
  if (distance >= tuning_.still_step * ticks) {
    const MovementTuning &legs = mover.tuning();
    walk.velocity = normalize(to_gate) * (distance / seconds);
    walk.motion = robot_motion::walk;
    walk.motion_speed = legs.walk_speed > 0.0F
                            ? length(walk.velocity) / legs.walk_speed * legs.walk_motion_rate
                            : 1.0F;
  }
  battle.script(index, walk);
}

void BattleScene::observe(Battle &battle) {
  if (battle.robots().empty()) {
    return;
  }
  for (const auto &[robot, event] : std::exchange(lines_, {})) {
    battle.tell(robot, event);
  }
  for (const BattleEvent &event : battle.events()) {
    if (event.kind == BattleEventKind::respawned) {
      static_cast<void>(start_robot_entrance(battle, event.robot));
    }
  }
  for (RobotEntrance &entry : robots_) {
    if (!entry.rider && entry.walking && entry.state == scene_state::computer &&
        tuning_.computer_entrance_skip_stand && entry.left <= tuning_.stand_frame) {
      end_robot(battle, entry);
    }
  }
  observe_robot_gates(battle);
  const Combatant &player = battle.player();
  if (!tuning_.exit || state_ != scene_state::none || !player.alive || player.hp <= 0.0F) {
    return;
  }
  const int gate = gate_at(player);
  if (gate_flag_ == 0 && gate < 0) {
    gate_flag_ = 1; // out of every gate: walking back into one leaves
  } else if (gate_flag_ == 1 && gate >= 0) {
    gate_flag_ = 2;
    if (tuning_.ranker) {
      sign_off(battle, battle.player_index());
    } else {
      start_exit(battle, gate);
    }
  }
}

const std::vector<Gate> *BattleScene::gates() const {
  const auto found = tables_->gates.find(tuning_.stage);
  return found == tables_->gates.end() ? nullptr : &found->second;
}

int BattleScene::gate_at(const Combatant &robot) const {
  const std::vector<Gate> *list = gates();
  if (list == nullptr) {
    return -1;
  }
  // 0x044839 tests a point of the robot's; the port takes its lower
  // collision sphere's centre.
  const Vec3 point = robot.mover.position() + Vec3{0.0F, robot.radius, 0.0F};
  for (std::size_t index = 0; index < list->size(); ++index) {
    const Vec3 gap = point - (*list)[index].position;
    const float across = std::hypot(gap.x, gap.z);
    if (gap.y > 0.0F && gap.y < tuning_.gate_height && across < tuning_.gate_radius) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void BattleScene::sign_off(Battle &battle, const int index) const {
  // 0x08224d: the chip signs off by the robot's armour.
  const Combatant &robot = battle.robots()[static_cast<std::size_t>(index)];
  const float armour = robot.max_hp > 0.0F ? robot.hp / robot.max_hp : 0.0F;
  battle.tell(index,
              armour >= tuning_.left_armour ? chatter_event::left : chatter_event::left_hurt);
}

float BattleScene::heading_to(const Combatant &robot, const Vec3 gate) const {
  const Vec3 to_gate = gate - robot.mover.position();
  return std::hypot(to_gate.x, to_gate.z) > tuning_.still_step ? std::atan2(-to_gate.x, -to_gate.z)
                                                               : robot.mover.heading();
}

void BattleScene::start_exit(Battle &battle, const int gate) {
  sign_off(battle, battle.player_index());
  const SceneTrack *scene = track(scene_state::exit);
  if (scene == nullptr) {
    return;
  }
  gate_ = gate;
  origin_ = (*gates())[static_cast<std::size_t>(gate)];
  exit_heading_ = heading_to(battle.player(), origin_.position);
  state_ = scene_state::exit;
  left_ = scene->frames;
  battle.raise_shield(battle.player_index(), scene->frames / scene_frames_per_second); // 0x08224d
  letterbox_way_ = 1.0F;
}

const SceneTrack *BattleScene::track(const int state) const {
  const auto scene = tables_->states.find(state);
  return scene == tables_->states.end() ? nullptr : &scene->second;
}

std::optional<CameraShot> BattleScene::scene_shot(const int state, const Gate &origin,
                                                  const float left) const {
  const SceneTrack *scene = track(state);
  if (scene == nullptr) {
    return std::nullopt;
  }
  const float frame = std::clamp(scene->frames - left, 0.0F, std::max(scene->frames - 1.0F, 0.0F));
  const auto place = [&](const std::array<MotionChannel, 3> &channels) {
    Vec3 point = sample(channels, frame);
    point.z = -point.z;
    return scene->relative ? origin.position + turned(point, origin.heading) : point;
  };
  CameraShot shot;
  shot.eye = place(scene->camera.eye);
  shot.target = place(scene->camera.target);
  shot.fov_degrees = scene->camera.fov.sample(frame);
  shot.roll = scene->camera.roll.sample(frame);
  return shot;
}

std::optional<CameraShot> BattleScene::shot(const CameraShot &chase) const {
  if (cinematic()) {
    // 0x07e456 sets the boss's shot outright, with no easing.
    const auto boss = std::ranges::find(robots_, cinematic_, &RobotEntrance::robot);
    return boss == robots_.end() ? std::nullopt : scene_shot(boss->state, boss->origin, boss->left);
  }
  if (!active()) {
    return std::nullopt;
  }
  std::optional<CameraShot> shot = scene_shot(state_, origin_, left_);
  if (shot && entering(state_) && left_ < tuning_.blend_frames && tuning_.blend_frames > 0.0F) {
    // 0x0800e8: the last frames ease into the chase camera.
    const float k = std::max(left_, 0.0F) / tuning_.blend_frames;
    shot->eye = mix(chase.eye, shot->eye, k);
    shot->target = mix(chase.target, shot->target, k);
    shot->fov_degrees = chase.fov_degrees + (shot->fov_degrees - chase.fov_degrees) * k;
    shot->roll = chase.roll + (shot->roll - chase.roll) * k;
  }
  return shot;
}

float BattleScene::letterbox() const noexcept {
  return letterbox_;
}

SceneCue BattleScene::cue() const noexcept {
  if (!entering(state_)) {
    return SceneCue::none;
  }
  if (left_ <= tuning_.go_frame) {
    return SceneCue::go;
  }
  return left_ <= tuning_.ready_frame ? SceneCue::ready : SceneCue::none;
}

} // namespace phantomcrash
