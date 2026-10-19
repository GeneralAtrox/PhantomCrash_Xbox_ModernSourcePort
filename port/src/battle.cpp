#include "phantomcrash/battle.hpp"
#include "phantomcrash/robot_combat.hpp"
#include "phantomcrash/battle_chatter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace phantomcrash {
namespace {

float number(const json::JsonValue &value, const char *key, const float fallback) {
  return static_cast<float>(value.number_or(key, fallback));
}

// Height of the muzzles above the robot's ground point, per slot.
constexpr std::array<float, weapon_slots> muzzle_height{2.6F, 2.6F, 4.2F, 4.2F};
constexpr std::array<float, weapon_slots> muzzle_side{1.6F, -1.6F, 1.2F, -1.2F};

} // namespace

BattleSettings parse_battle_settings(const json::JsonValue &value) {
  BattleSettings s;
  if (const json::JsonValue *arena = value.find("arena")) {
    s.arena.half_size = number(*arena, "half_size", s.arena.half_size);
    if (const json::JsonValue *blocks = arena->find("blocks"); blocks && blocks->is_array()) {
      for (const json::JsonValue &block : blocks->array()) {
        const auto &b = block.array();
        if (b.size() != 4) {
          throw std::runtime_error("arena block needs {x0, z0, x1, z1}");
        }
        s.arena.blocks.push_back(
            {static_cast<float>(b[0].number()), static_cast<float>(b[1].number()),
             static_cast<float>(b[2].number()), static_cast<float>(b[3].number())});
      }
    }
  }
  if (const json::JsonValue *movement = value.find("movement")) {
    s.movement = parse_movement_tuning(*movement);
  }
  if (const json::JsonValue *weapons = value.find("weapons"); weapons && weapons->is_object()) {
    s.weapons = parse_weapon_tunings(*weapons);
  }
  s.robot_radius = number(value, "robot_radius", s.robot_radius);
  s.robot_height = number(value, "robot_height", s.robot_height);
  s.hit_flash_seconds = number(value, "hit_flash_seconds", s.hit_flash_seconds);
  s.sight_fov_degrees = number(value, "sight_fov_degrees", s.sight_fov_degrees);
  s.arena_wall_height = number(value, "arena_wall_height", s.arena_wall_height);
  s.fence_contact_seconds = number(value, "fence_contact_seconds", s.fence_contact_seconds);
  s.drone_respawn_seconds = number(value, "drone_respawn_seconds", s.drone_respawn_seconds);
  s.death_fallback_seconds = number(value, "death_fallback_seconds", s.death_fallback_seconds);
  s.drone_walk = number(value, "drone_walk", s.drone_walk);
  s.drone_reach = number(value, "drone_reach", s.drone_reach);
  s.camouflage_seconds = number(value, "camouflage_seconds", s.camouflage_seconds);
  s.camouflage_recharge_seconds =
      number(value, "camouflage_recharge_seconds", s.camouflage_recharge_seconds);
  s.camouflage_fade_seconds = number(value, "camouflage_fade_seconds", s.camouflage_fade_seconds);
  s.camouflage_break_seconds =
      number(value, "camouflage_break_seconds", s.camouflage_break_seconds);
  s.ammo_refill_seconds = number(value, "ammo_refill_seconds", s.ammo_refill_seconds);
  s.idle_chatter_seconds = number(value, "idle_chatter_seconds", s.idle_chatter_seconds);
  if (const json::JsonValue *shield = value.find("entry_shield")) {
    s.entry_shield = parse_entry_shield(*shield);
  }
  if (const json::JsonValue *ai = value.find("ai")) {
    s.ai = parse_drone_ai(*ai);
  }
  if (const auto *wrecks = value.find("robot_wrecks")) {
    s.robot_wrecks = parse_robot_wreck_settings(*wrecks);
  }
  s.hit_reward_ny =
      static_cast<int>(number(value, "hit_reward_ny", static_cast<float>(s.hit_reward_ny)));
  if (const auto *rewards = value.find("object_reward_ny")) {
    s.object_reward_ny.clear();
    for (const json::JsonValue &reward : rewards->array()) {
      s.object_reward_ny.push_back(static_cast<int>(reward.number()));
    }
  }
  return s;
}

Battle::Battle(BattleSettings settings, EnemyPath path, const std::uint32_t seed)
    : settings_(std::move(settings)),
      stage_(StageCollision::box(settings_.arena, settings_.arena_wall_height)),
      path_(std::move(path)), random_(seed) {}

void Battle::set_doors(StageDoors doors) {
  doors_ = std::move(doors);
}

void Battle::set_radius(const int index, const float radius) {
  robots_.at(static_cast<std::size_t>(index)).radius = radius;
}

void Battle::set_tag(const int index, std::string tag) {
  robots_.at(static_cast<std::size_t>(index)).tag = std::move(tag);
}

int Battle::add_robot(const std::string &name, const int team, const Vec3 position,
                      const float heading, const std::array<std::string, weapon_slots> &weapons,
                      const float hp) {
  Combatant robot(settings_.movement);
  robot.name = name;
  robot.team = team;
  robot.piloted = robots_.empty(); // the player's robot comes first (player())
  robot.hp = robot.max_hp = hp;
  robot.radius = settings_.robot_radius;
  robot.mover.place(placed(position, robot.radius, robot.mover), heading);
  robots_.push_back(std::move(robot));
  const int index = static_cast<int>(robots_.size()) - 1;
  arm(index, weapons);
  return index;
}

void Battle::set_movement(const int index, const MovementTuning &tuning) {
  if (index >= 0 && index < static_cast<int>(robots_.size())) {
    robots_[static_cast<std::size_t>(index)].mover.set_tuning(tuning);
  }
}

void Battle::set_drones_active(const bool active) {
  drones_active_ = active;
  for (Combatant &robot : robots_) {
    if (robot.drone) {
      robot.frozen = !active;
    }
  }
}

void Battle::place(const int index, const Vec3 position, const float heading) {
  Combatant &robot = robots_.at(static_cast<std::size_t>(index));
  RobotMover &mover = robot.mover;
  robot.last_ground.reset();
  mover.place(placed(position, robot.radius, mover), heading);
}

void Battle::script(const int index, const ScriptedStep &step) {
  robots_.at(static_cast<std::size_t>(index)).mover.script(step);
}

void Battle::release(const int index) {
  robots_.at(static_cast<std::size_t>(index)).mover.release();
}

void Battle::tell(const int index, const int event) {
  chatter(robots_.at(static_cast<std::size_t>(index)), event);
}

void Battle::place_facing_player(const int index, const float distance) {
  if (index <= 0 || index >= static_cast<int>(robots_.size())) {
    return;
  }
  const RobotMover &player = robots_.front().mover;
  Combatant &robot = robots_[static_cast<std::size_t>(index)];
  robot.alive = true;
  robot.hp = robot.max_hp;
  robot.damage_from.clear();
  robot.mover.place(player.position() + heading_forward(player.heading()) * distance,
                    player.heading() + pi);
}

Vec3 Battle::aim(const Combatant &robot) const {
  const float pitch = robot.mover.pitch();
  return heading_forward(robot.mover.aim_heading()) * std::cos(pitch) +
         Vec3{0.0F, std::sin(pitch), 0.0F};
}

Vec3 Battle::muzzle(const Combatant &robot, const WeaponSlot slot) const {
  const auto index = static_cast<std::size_t>(slot);
  if (robot.pose) {
    if (const auto matrix = robot.pose->muzzle(index)) {
      return transform_point({}, *matrix);
    }
  }
  const float heading = robot.mover.aim_heading();
  return robot.mover.position() + Vec3{0.0F, muzzle_height[index], 0.0F} +
         heading_right(heading) * muzzle_side[index] + heading_forward(heading) * 1.5F;
}

RobotControl Battle::drone_control(Combatant &drone, const float seconds) {
  RobotControl control;
  if (drone.frozen || drone.mover.mode() == MoveMode::scripted) {
    drone.ai.fire.fill(false);
    return control;
  }
  if (settings_.ai.fight && settings_.ai.original) {
    return ai_control(drone, seconds);
  }
  if (!drone.path.active()) {
    return control;
  }
  const Vec3 at = drone.mover.position();
  Vec3 offset = drone.path.update(path_, at, settings_.drone_reach, random_) - at;
  offset.y = 0.0F;
  if (length(offset) < 1e-3F) {
    return control;
  }
  // Turn toward the next point, walking once roughly facing it.
  const float error = wrap_angle(heading_toward(offset) - drone.mover.heading());
  control.move[0] = std::clamp(error * 2.0F, -1.0F, 1.0F);
  control.move[1] = std::abs(error) < 0.7F ? settings_.drone_walk : 0.0F;
  return control;
}

// Two spheres against the stage (0x07c7c0): one resting on the ground,
// which can stand the robot on what it touches, and one under the top of
// the robot, which only pushes it aside or stops a jump.
void Battle::collide(const int index, const float seconds) {
  Combatant &robot = robots_[static_cast<std::size_t>(index)];
  std::optional<StageHit> fence_hit;
  // The doors stop robots walking in the arena; a scripted walk goes
  // through them (0x08928c sets its position outright).
  StageDoors *doors = robot.mover.mode() != MoveMode::scripted ? this->doors() : nullptr;
  const auto push = [&](Vec3 &centre, const float radius) {
    unsigned contact = stage_.push_sphere(centre, radius, &fence_hit);
    if (doors != nullptr) {
      contact |= doors->push_sphere(centre, radius);
    }
    if (objects_ && robot.mover.mode() != MoveMode::scripted) {
      contact |= objects_->push_sphere(centre, radius);
    }
    if (robot.mover.mode() != MoveMode::scripted) {
      contact |= wrecks_.push_sphere(centre, radius);
    }
    return contact;
  };
  const float r = robot.radius;
  const float head_height = std::max(settings_.robot_height - r, r);
  Vec3 at = robot.mover.position();
  // The floor triangles never push, so the robot stands on the one under it
  // however far its feet have sunk below, and that grounds it: otherwise it
  // walks into a step or a ramp's foot built from them.
  const float floor = stage_.floor_under(at.x, at.z);
  const bool on_floor = floor != no_ground && at.y <= floor;
  if (on_floor) {
    at.y = floor;
  }
  Vec3 foot = at + Vec3{0.0F, r, 0.0F};
  const unsigned foot_contact = push(foot, r);
  at = foot - Vec3{0.0F, r, 0.0F};
  Vec3 head = at + Vec3{0.0F, head_height, 0.0F};
  const unsigned head_contact = push(head, r);
  const float drop = at.y + head_height - head.y;
  at = {head.x, at.y - std::max(drop, 0.0F), head.z};
  robot.mover.set_position(at);
  if (on_floor || (foot_contact & stage_contact::ground) != 0) {
    robot.mover.land(at.y);
  }
  if (drop >= head_bump_drop) {
    robot.mover.bump_head();
  }
  robot.stage_contact = foot_contact | head_contact;
  // Tangency can flicker between neighbouring triangles. Losing contact must
  // not bypass the pulse interval by resetting its cooldown to zero.
  robot.fence_contact_left = std::max(0.0F, robot.fence_contact_left - seconds);
  if (fence_hit && robot.mover.mode() != MoveMode::scripted &&
      settings_.fence_contact_seconds > 0.0F && robot.fence_contact_left <= 0.0F) {
    BattleEvent event{index, BattleEventKind::fence_contact};
    event.at = fence_hit->position;
    event.surface = fence_hit;
    events_.push_back(event);
    robot.fence_contact_left = settings_.fence_contact_seconds;
  }
}

void Battle::separate() {
  for (std::size_t a = 0; a < robots_.size(); ++a) {
    for (std::size_t b = a + 1; b < robots_.size(); ++b) {
      if (!robots_[a].alive || !robots_[b].alive) {
        continue;
      }
      const float gap = robots_[a].radius + robots_[b].radius;
      Vec3 pa = robots_[a].mover.position();
      Vec3 pb = robots_[b].mover.position();
      Vec3 offset{pb.x - pa.x, 0.0F, pb.z - pa.z};
      const float distance = length(offset);
      if (distance >= gap || std::abs(pa.y - pb.y) > settings_.robot_height) {
        continue;
      }
      const Vec3 push = (distance > 1e-4F ? offset * (1.0F / distance) : Vec3{1.0F, 0.0F, 0.0F}) *
                        ((gap - distance) * 0.5F);
      robots_[a].mover.set_position(carried(robots_[a], pa, push * -1.0F));
      robots_[b].mover.set_position(carried(robots_[b], pb, push));
    }
  }
}

void Battle::tick(const RobotControl &player, const FireControl &fire, const float seconds) {
  events_.clear();
  const int plans = settings_.ai.navigation.plans_per_tick;
  nav_plans_left_ = plans > 0 ? plans : std::numeric_limits<int>::max();
  wrecks_.tick(seconds);
  if (objects_) {
    objects_->tick(seconds);
  }
  if (doors_) {
    doors_->tick(seconds);
  }
  for (std::size_t index = 0; index < robots_.size(); ++index) {
    Combatant &robot = robots_[index];
    reset_fire_inputs(robot);
    robot.pose_updated = false;
    robot.hit_flash = std::max(0.0F, robot.hit_flash - seconds);
    robot.shown_armour.tick(robot.hp, robot.max_hp, seconds);
    if (!robot.alive) {
      tick_death(static_cast<int>(index), seconds);
      robot.respawn_left -= seconds;
      if (!robot.dying && robot.drone && robot.auto_respawn && robot.respawn_left <= 0.0F) {
        respawn(robot);
      }
      continue;
    }
    const float stage_floor = this->stage_floor(robot);
    float floor = stage_floor;
    if (objects_ && robot.mover.mode() != MoveMode::scripted) {
      floor = std::max(floor,
                       objects_->ground_height(robot.mover.position(), ground_snap, robot.radius));
    }
    if (robot.mover.mode() != MoveMode::scripted) {
      floor =
          std::max(floor, wrecks_.ground_height(robot.mover.position(), ground_snap, robot.radius));
    }
    if (floor > stage_floor) {
      robot.floor_surface = -1; // on a prop or a wreck
    }
    robot.mover.set_floor(floor);
    const bool own_controls = cooperative_ || versus_;
    const RobotControl control =
        robot.piloted ? (own_controls ? controls_[static_cast<int>(index)].first : player)
                      : drone_control(robot, seconds);
    const RobotAssembly *assembly = robot.pose ? &robot.pose->assembly() : nullptr;
    robot.mover.set_timings(assembly ? &assembly->timings : nullptr,
                            assembly ? assembly->leg_type : -1);
    robot.mover.tick(control, seconds,
                     robot.pose ? robot.pose->root_motion_source() : RootMotionSource{});
    recover(robot, seconds);
  }
  separate();
  for (std::size_t index = 0; index < robots_.size(); ++index) {
    if (robots_[index].alive) {
      collide(static_cast<int>(index), seconds);
    }
  }

  for (std::size_t index = 0; index < robots_.size(); ++index) {
    Combatant &self = robots_[index];
    if (!self.piloted || !self.alive) {
      continue;
    }
    const auto &own = controls_[static_cast<int>(index)];
    const FireControl &own_fire = cooperative_ || versus_ ? own.second : fire;
    const RobotControl &own_control = cooperative_ || versus_ ? own.first : player;
    update_camouflage(self, own_fire.camouflage, seconds);
    fire_weapons(static_cast<int>(index), own_fire, seconds);
    watch_idle(self, own_control, own_fire, seconds);
  }
  if (settings_.ai.fight) {
    fire_drones(seconds);
  }
  for (Combatant &robot : robots_) {
    if (robot.alive) {
      watch_ammo(robot);
    }
  }
  for (std::size_t index = 0; index < robots_.size(); ++index) {
    if (robots_[index].alive && !robots_[index].pose_updated) {
      pose_robot(static_cast<int>(index), seconds);
    }
  }
  for (Combatant &robot : robots_) {
    const float fade = robot.camouflage_broken ? settings_.camouflage_break_seconds
                                               : settings_.camouflage_fade_seconds;
    const float sweep = seconds / std::max(fade, 1e-3F);
    const float target = robot.camouflaged ? 1.0F : 0.0F;
    robot.camouflage_sweep += std::clamp(target - robot.camouflage_sweep, -sweep, sweep);
    robot.camouflage_broken = robot.camouflage_broken && robot.camouflage_sweep > 0.0F;
  }
  tick_supply(seconds);
  std::vector<WeaponHit> hits;
  armory_.tick(seconds, targets(), stage_, hits, doors());
  apply_hits(hits);
  for (const Landing &landing : armory_.take_landings()) {
    BattleEvent impact{landing.owner, BattleEventKind::impact};
    impact.weapon = landing.weapon;
    impact.slot = landing.slot;
    impact.at = landing.at;
    impact.on_robot = landing.on_robot;
    impact.surface = landing.surface;
    impact.target = landing.target;
    events_.push_back(impact);
  }
}

} // namespace phantomcrash
