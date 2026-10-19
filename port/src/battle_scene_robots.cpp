// Computer robots reuse the entrance walk at their own speed and can hand
// control to AI as soon as it ends, shielded while they walk out
// (entry_shield.hpp), and their exits (state 6: 0x08224d, 0x089fa3,
// 0x08a161, 0x08a496). NPC arrivals leave the player's camera and music alone.
#include "phantomcrash/battle_scene.hpp"

#include "phantomcrash/battle_chatter.hpp"

#include <algorithm>
#include <numeric>

namespace phantomcrash {

int BattleScene::next_robot_gate(const Battle &battle, const int count) {
  if (remaining_gates_.empty()) {
    remaining_gates_.resize(static_cast<std::size_t>(count));
    std::iota(remaining_gates_.begin(), remaining_gates_.end(), 0);
    std::shuffle(remaining_gates_.begin(), remaining_gates_.end(), random_);
    if (count > 1 && remaining_gates_.back() == last_robot_gate_) {
      std::uniform_int_distribution<int> pick(0, count - 2);
      std::swap(remaining_gates_.back(), remaining_gates_[pick(random_)]);
    }
  }
  // Prefer an unused entrance in this shuffled round. Reserve busy gates
  // only once the free ones have been assigned, keeping their queues balanced.
  const auto load = [&](const int gate) {
    int queued = active() && gate_ == gate ? 1 : 0;
    for (const RobotEntrance &entry : robots_) {
      if (entry.state != scene_state::none && entry.gate == gate) {
        ++queued;
      }
    }
    if (const StageDoors *doors = battle.doors(); doors != nullptr && !doors->idle(gate)) {
      ++queued;
    }
    return queued;
  };
  auto selected = remaining_gates_.end() - 1;
  for (auto gate = remaining_gates_.begin(); gate != remaining_gates_.end(); ++gate) {
    if (*gate != last_robot_gate_ && load(*gate) < load(*selected)) {
      selected = gate;
    }
  }
  std::iter_swap(selected, remaining_gates_.end() - 1);
  last_robot_gate_ = remaining_gates_.back();
  remaining_gates_.pop_back();
  return last_robot_gate_;
}

int BattleScene::robot_gate(const int index) const {
  const auto found = std::ranges::find(robots_, index, &RobotEntrance::robot);
  return found == robots_.end() ? -1 : found->gate;
}

bool BattleScene::start_robot_entrance(Battle &battle, const int index) {
  const std::vector<Gate> *list = gates();
  const SceneTrack *scene = track(scene_state::computer);
  if (!tuning_.computer_entrance || list == nullptr || list->empty() || scene == nullptr ||
      index <= 0 || index >= static_cast<int>(battle.robots().size()) ||
      battle.robots()[index].piloted) {
    return false;
  }
  auto found = std::ranges::find(robots_, index, &RobotEntrance::robot);
  if (found == robots_.end()) {
    found = robots_.insert(robots_.end(), RobotEntrance{.robot = index});
  }
  RobotEntrance &entry = *found;
  entry.state = scene_state::computer;
  entry.gate_flag = 0;
  entry.walking = false;
  entry.delay = tuning_.entry_delay;
  entry.left = scene->frames;
  // Use every door once per shuffled round, without repeating across rounds;
  // a ranker comes out of the gate it waits at (0x0825df).
  entry.gate = -1;
  const int gates = static_cast<int>(list->size());
  entry.gate = tuning_.ranker && tuning_.ranker_gate >= 0 && tuning_.ranker_gate < gates
                   ? tuning_.ranker_gate
                   : next_robot_gate(battle, gates);
  entry.origin = (*list)[static_cast<std::size_t>(entry.gate)];
  battle.place(index, entry.origin.position, entry.origin.heading);
  battle.raise_shield(index);
  hold_still(battle, index);
  return true;
}

bool BattleScene::start_rider_entrance(Battle &battle, const int index, const int gate) {
  const std::vector<Gate> *list = gates();
  const SceneTrack *scene = track(scene_state::computer);
  if (!tuning_.rider_entrance || list == nullptr || list->empty() || scene == nullptr || gate < 0 ||
      index < 0 || index >= static_cast<int>(battle.robots().size())) {
    return false;
  }
  std::erase_if(robots_, [&](const RobotEntrance &entry) { return entry.robot == index; });
  // No door delay: riders sharing a gate still go one after another
  // (waits_for_others).
  RobotEntrance &entry = robots_.emplace_back(RobotEntrance{.robot = index});
  entry.state = scene_state::computer;
  entry.left = scene->frames;
  entry.gate = gate % static_cast<int>(list->size());
  entry.origin = (*list)[static_cast<std::size_t>(entry.gate)];
  entry.rider = true;
  battle.place(index, entry.origin.position, entry.origin.heading);
  battle.raise_shield(index);
  hold_still(battle, index);
  return true;
}

std::optional<BattleScene::Playback> BattleScene::playback() const {
  const auto played = [&](const int state, const int robot,
                          const float left) -> std::optional<Playback> {
    const SceneTrack *scene = track(state);
    if (scene == nullptr) {
      return std::nullopt;
    }
    return Playback{state, robot, scene->frames - std::clamp(left, 0.0F, scene->frames),
                    scene->frames};
  };
  if (cinematic()) {
    const auto boss = std::ranges::find(robots_, cinematic_, &RobotEntrance::robot);
    if (boss != robots_.end()) {
      return played(boss->state, boss->robot, boss->left);
    }
  }
  if (active()) {
    return played(state_, -1, left_);
  }
  const auto rider = std::ranges::find_if(robots_, [](const RobotEntrance &entry) {
    return entry.rider && entry.walking && entry.state != scene_state::none;
  });
  return rider == robots_.end() ? std::nullopt : played(rider->state, rider->robot, rider->left);
}

int BattleScene::robot_state(const int index) const {
  const auto found = std::ranges::find(robots_, index, &RobotEntrance::robot);
  return found == robots_.end() ? scene_state::none : found->state;
}

void BattleScene::walk_robot(Battle &battle, RobotEntrance &entry, const float seconds,
                             const float step) {
  const Combatant &robot = battle.robots()[static_cast<std::size_t>(entry.robot)];
  if (robot.piloted && !entry.rider) {
    entry.state = scene_state::none;
    return;
  }
  if (!robot.alive) {
    end_robot(battle, entry);
    return;
  }
  if (entry.state == scene_state::exit) {
    walk_robot_exit(battle, entry, seconds, step);
    return;
  }
  battle.raise_shield(entry.robot);
  // A boss's scene plays at the battle's pace, as do riders' walks.
  const bool boss = entry.state == scene_state::boss;
  const float entrance_step = step * (entry.rider || boss ? 1.0F : tuning_.computer_entrance_speed);
  if (cinematic() && cinematic_ != entry.robot) {
    battle.raise_shield(entry.robot);
    hold_still(battle, entry.robot); // it walks out after the boss's scene
    return;
  }
  if (!entry.walking) {
    // 0x088ffa: it waits for the other robots' scenes, then for a busy
    // door, but no longer than its delay; a ranker waits all of its delay.
    // It stays shielded while it waits (0x089032, 0x0892d1).
    const StageDoors *doors = battle.doors();
    battle.raise_shield(entry.robot);
    if (waits_for_others(battle, entry)) {
      hold_still(battle, entry.robot);
      return;
    }
    const bool door_busy = doors != nullptr && !doors->idle(entry.gate);
    if (entry.delay > 0.0F && (tuning_.ranker || door_busy)) {
      entry.delay -= entrance_step;
      hold_still(battle, entry.robot);
      return;
    }
    start_walking(battle, entry);
  }
  // Piloted riders keep their full sequence. NPCs can finish at the walk's
  // end; observe() releases them after the last scripted displacement lands.
  const bool skip_stand = !entry.rider && !boss && tuning_.computer_entrance_skip_stand;
  const float before = entry.left;
  entry.left = std::max(skip_stand ? tuning_.stand_frame : 0.0F, entry.left - entrance_step);
  open_door(battle, entry.gate, before, entry.left);
  if (!skip_stand && entry.left <= 0.0F) {
    end_robot(battle, entry);
    return;
  }
  walk_step(battle, entry.robot, entry.state, entry.origin, before, entry.left, seconds);
}

bool BattleScene::waits_for_others(const Battle &battle, const RobotEntrance &entry) const {
  // Entrance sequences own their door, not the whole arena. Other doors
  // can admit robots even while the player is still walking in.
  if (active() && gate_ == entry.gate) {
    return true;
  }
  return std::ranges::any_of(robots_, [&](const RobotEntrance &other) {
    const Combatant &robot = battle.robots()[static_cast<std::size_t>(other.robot)];
    return &other != &entry && other.gate == entry.gate && robot.alive &&
           other.state != scene_state::none && (other.walking || other.state == scene_state::exit);
  });
}

void BattleScene::start_walking(Battle &battle, RobotEntrance &entry) {
  entry.walking = true;
  const Combatant &robot = battle.robots()[static_cast<std::size_t>(entry.robot)];
  if (tuning_.regular_battle && !entry.rider) {
    const int line = !robot.rival ? chatter_event::entrance
                     : robot.boss ? chatter_event::boss_entrance
                                  : chatter_event::rival_entrance;
    lines_.emplace_back(entry.robot, line);
  }
  // 0x082cff: a boss (on the other side, loadout +0x70 1) takes the camera,
  // the letterbox bars and the music for its own shot, the first time in a
  // battle only; the other robots fight on meanwhile.
  const SceneTrack *scene = track(scene_state::boss);
  if (entry.rider || !robot.rival || !robot.boss || boss_played_ || cinematic() ||
      scene == nullptr) {
    return;
  }
  entry.state = scene_state::boss;
  entry.left = scene->frames;
  cinematic_ = entry.robot;
  letterbox_way_ = 1.0F; // the bars slide in (global +0xef320 2)
  music_ = tuning_.boss_music;
}

void BattleScene::end_robot(Battle &battle, RobotEntrance &entry) {
  // 0x0897a2: it stands and takes up its path.
  entry.state = scene_state::none;
  battle.release(entry.robot);
  if (!entry.rider && battle.robots()[static_cast<std::size_t>(entry.robot)].alive) {
    battle.join_path(entry.robot);
  }
  if (cinematic_ == entry.robot) {
    cinematic_ = -1;
    boss_played_ = true; // global +0xef328
    letterbox_way_ = -1.0F;
  }
}

void BattleScene::observe_robot_gates(Battle &battle) {
  // 0x08224d runs for every robot: one that has been out of every gate
  // and walks back into one leaves, while it has armour.
  if (!tuning_.computer_exit) {
    return;
  }
  for (RobotEntrance &entry : robots_) {
    const Combatant &robot = battle.robots()[static_cast<std::size_t>(entry.robot)];
    if (robot.piloted || entry.state != scene_state::none || !robot.alive || robot.hp <= 0.0F ||
        robot.frozen) {
      continue;
    }
    const int gate = gate_at(robot);
    if (entry.gate_flag == 0 && gate < 0) {
      entry.gate_flag = 1;
    } else if (entry.gate_flag == 1 && gate >= 0) {
      entry.gate_flag = 2;
      if (tuning_.ranker) {
        sign_off(battle, entry.robot);
      } else {
        start_robot_exit(battle, entry, gate);
      }
    }
  }
}

void BattleScene::start_robot_exit(Battle &battle, RobotEntrance &entry, const int gate) {
  sign_off(battle, entry.robot);
  const SceneTrack *scene = track(scene_state::exit);
  if (scene == nullptr) {
    return;
  }
  // 0x089fa3: a computer robot's exit has no bars or camera of its own.
  entry.state = scene_state::exit;
  entry.gate = gate;
  entry.origin = (*gates())[static_cast<std::size_t>(gate)];
  entry.heading =
      heading_to(battle.robots()[static_cast<std::size_t>(entry.robot)], entry.origin.position);
  entry.left = scene->frames;
  battle.raise_shield(entry.robot, scene->frames / scene_frames_per_second);
}

void BattleScene::walk_robot_exit(Battle &battle, RobotEntrance &entry, const float seconds,
                                  const float step) {
  entry.left = std::max(entry.left - step, 0.0F);
  if (entry.left > 0.0F) {
    exit_step(battle, entry.robot, entry.heading, entry.origin.position, seconds, step);
    return;
  }
  // 0x08a496: it stands in the gate, shielded, until its door has shut;
  // then 0x047c9e takes it out of the battle.
  hold_still(battle, entry.robot);
  battle.raise_shield(entry.robot, tuning_.gone_shield_frames / scene_frames_per_second);
  if (const StageDoors *doors = battle.doors(); doors == nullptr || doors->idle(entry.gate)) {
    entry.state = scene_state::none;
    battle.withdraw(entry.robot);
  }
}

void BattleScene::hold_still(Battle &battle, const int index) const {
  // A scripted robot stops where it is, its motion held.
  const RobotMover &mover = battle.robots()[static_cast<std::size_t>(index)].mover;
  ScriptedStep still;
  still.heading = mover.heading();
  still.motion = mover.mode() == MoveMode::scripted ? mover.motion() : robot_motion::stand;
  still.loops = mover.motion_loops();
  still.motion_speed = 0.0F;
  battle.script(index, still);
}

} // namespace phantomcrash
