#include "phantomcrash/battle_sounds.hpp"

#include "phantomcrash/robot_combat.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {

namespace {

// Loops are keyed well away from other screens' keys; each robot has its
// own run of them.
constexpr std::uint64_t boost_key_base = 0xB5'0000;

SoundPair parse_pair(const json::JsonValue *value) {
  SoundPair pair;
  if (value == nullptr) {
    return pair;
  }
  const std::string both = value->string_or("sound", "");
  pair.player = value->string_or("player", both);
  pair.computer = value->string_or("computer", both);
  pair.loop = value->boolean_or("loop", false);
  pair.requires_free_arm_action = value->boolean_or("requires_free_arm_action", false);
  const auto trigger = value->string_or("trigger_policy", "");
  if (!trigger.empty()) {
    if (trigger != "held" && trigger != "shot" && trigger != "paired" && trigger != "sustained") {
      throw std::runtime_error("Unknown sound trigger policy");
    }
    pair.trigger_policy = trigger == "sustained" ? 3
                          : trigger == "paired"  ? 2
                          : trigger == "held"    ? 1
                                                 : 0;
  }
  return pair;
}

int parse_mount(const json::JsonValue &value) {
  const std::string mount = value.string_or("mount", "");
  if (mount.empty()) {
    return -1;
  }
  if (mount == "arm") {
    return 0;
  }
  if (mount == "shoulder") {
    return 1;
  }
  throw std::runtime_error("unknown weapon mount \"" + mount + "\"");
}

std::vector<WeaponSoundRule> parse_rules(const json::JsonValue *value) {
  std::vector<WeaponSoundRule> rules;
  if (value == nullptr) {
    return rules;
  }
  for (const json::JsonValue &item : value->array()) {
    WeaponSoundRule rule;
    rule.style = static_cast<int>(item.number_or("style", -1.0));
    rule.mount = parse_mount(item);
    rule.hud_kind = static_cast<int>(item.number_or("hud_kind", -1.0));
    rule.flight = static_cast<int>(item.number_or("flight", -1.0));
    if (const json::JsonValue *models = item.find("models")) {
      for (const json::JsonValue &model : models->array()) {
        rule.models.push_back(static_cast<int>(model.number()));
      }
    }
    rule.sound = parse_pair(&item);
    if (const auto *cycle = item.find("fire_cycle")) {
      const double group = cycle->number_or("group", -1);
      if (group < 0 || group > 255 || std::floor(group) != group ||
          rule.sound.trigger_policy != 2) {
        throw std::runtime_error("Invalid shared fire sound group");
      }
      rule.cycle.group = static_cast<int>(group);
      rule.cycle.start_together = cycle->boolean_or("start_together", false);
      rule.cycle.secondary = parse_pair(cycle->find("secondary"));
      rule.cycle.ending = parse_pair(cycle->find("ending"));
    } else if (rule.sound.trigger_policy == 2) {
      throw std::runtime_error("Paired fire sound requires its source group");
    }
    if (const auto *target = item.find("on_robot")) {
      rule.on_robot = target->boolean() ? 1 : 0;
    }
    if (const auto *variants = item.find("variants")) {
      for (const auto &variant : variants->array()) {
        rule.variants.push_back({variant.string_or("sound", ""), variant.number_or("weight", 1)});
      }
    }
    rules.push_back(std::move(rule));
  }
  return rules;
}

bool is_shoulder(const int slot) {
  return slot == static_cast<int>(WeaponSlot::right_shoulder) ||
         slot == static_cast<int>(WeaponSlot::left_shoulder);
}

} // namespace

bool WeaponSoundRule::matches(const WeaponTuning &weapon, const int slot) const {
  if (style >= 0 && style != weapon.fire_sound) {
    return false;
  }
  const bool shoulder =
      slot >= 0 ? is_shoulder(slot) : weapon.part && weapon.part->table == WeaponTable::shoulder;
  if (mount >= 0 && (mount == 1) != shoulder) {
    return false;
  }
  if (hud_kind >= 0 && hud_kind != weapon.hud_kind) {
    return false;
  }
  if (flight >= 0 && flight != weapon.flight) {
    return false;
  }
  return models.empty() || std::ranges::find(models, weapon.model) != models.end();
}

BattleSoundTable parse_battle_sounds(const json::JsonValue &value) {
  BattleSoundTable table;
  if (const json::JsonValue *falloff = value.find("falloff")) {
    table.falloff = parse_sound_falloff(*falloff);
  }
  if (const json::JsonValue *robot = value.find("robot")) {
    table.footstep = parse_pair(robot->find("footstep"));
    table.landing = parse_pair(robot->find("landing"));
    if (const json::JsonValue *surfaces = robot->find("landing_surfaces")) {
      for (const json::JsonValue &entry : surfaces->array()) {
        BattleSoundTable::SurfaceSound rule;
        if (const json::JsonValue *list = entry.find("surfaces")) {
          for (const json::JsonValue &surface : list->array()) {
            rule.flags |= surface_flags(static_cast<int>(surface.number()));
          }
        }
        rule.sound = entry.string_or("sound", "");
        table.landing_surfaces.push_back(std::move(rule));
      }
    }
    table.jump = parse_pair(robot->find("jump"));
    table.down_boost = parse_pair(robot->find("down_boost"));
    table.boost_start = parse_pair(robot->find("boost_start"));
    table.boost = parse_pair(robot->find("boost"));
    table.dodge = parse_pair(robot->find("dodge"));
    table.air_dodge = parse_pair(robot->find("air_dodge"));
    table.shot_down = parse_pair(robot->find("shot_down"));
    table.low_armour = parse_pair(robot->find("low_armour"));
    table.twist_start = robot->string_or("twist_start", "");
    table.twist_stop = robot->string_or("twist_stop", "");
    table.twist_push = static_cast<float>(robot->number_or("twist_push", table.twist_push));
    table.twist_limit_degrees =
        static_cast<float>(robot->number_or("twist_limit_degrees", table.twist_limit_degrees));
    table.camouflage_on = robot->string_or("camouflage_on", "");
    table.camouflage_off = robot->string_or("camouflage_off", "");
    table.camouflage_break = robot->string_or("camouflage_break", "");
    table.arm_lost = robot->string_or("arm_lost", "");
    table.arm_lost_seconds = std::max(
        static_cast<float>(robot->number_or("arm_lost_seconds", table.arm_lost_seconds)), 0.0F);
    table.locked_on = robot->string_or("locked_on", "");
  }
  table.carrier_engine = value.string_or("carrier_engine", "");
  const auto number = [&](const char *key, const float fallback) {
    return static_cast<float>(value.number_or(key, fallback));
  };
  table.footstep_seconds = std::max(number("footstep_seconds", table.footstep_seconds), 0.05F);
  table.low_armour_share = number("low_armour_share", table.low_armour_share);
  table.low_armour_seconds =
      std::max(number("low_armour_seconds", table.low_armour_seconds), 0.05F);
  table.fire = parse_rules(value.find("fire"));
  table.beam_restart_seconds = std::max(number("beam_restart_seconds", 9.0F), 0.05F);
  table.impact = parse_rules(value.find("impact"));
  table.fire_fallback = parse_pair(value.find("fire_fallback"));
  return table;
}

std::vector<std::string> sound_files(const BattleSoundTable &table) {
  std::vector<std::string> files;
  const auto add = [&](const SoundPair &pair) {
    for (const std::string *name : {&pair.player, &pair.computer}) {
      if (!name->empty() && std::ranges::find(files, *name) == files.end()) {
        files.push_back(*name);
      }
    }
  };
  for (const SoundPair *pair : {&table.footstep, &table.landing, &table.jump, &table.down_boost,
                                &table.boost_start, &table.boost, &table.dodge, &table.air_dodge,
                                &table.shot_down, &table.low_armour, &table.fire_fallback}) {
    add(*pair);
  }
  for (const auto &rule : table.landing_surfaces) {
    add({rule.sound, {}});
  }
  for (const std::string *name :
       {&table.twist_start, &table.twist_stop, &table.camouflage_on, &table.camouflage_off,
        &table.camouflage_break, &table.arm_lost, &table.locked_on, &table.carrier_engine}) {
    add({*name, {}});
  }
  for (const auto *rules : {&table.fire, &table.impact}) {
    for (const WeaponSoundRule &rule : *rules) {
      add(rule.sound);
      add(rule.cycle.secondary);
      add(rule.cycle.ending);
      for (const auto &variant : rule.variants) {
        add({variant.sound, variant.sound});
      }
    }
  }
  return files;
}

const std::string &landing_sound(const BattleSoundTable &table, const Combatant &robot) {
  const unsigned flags =
      robot.stage_contact | (robot.floor_surface >= 0 ? surface_flags(robot.floor_surface) : 0U);
  for (const auto &rule : table.landing_surfaces) {
    if ((flags & rule.flags) != 0 && !rule.sound.empty()) {
      return rule.sound;
    }
  }
  return table.landing.pick(robot.piloted);
}

const SoundPair *weapon_sound(const std::vector<WeaponSoundRule> &rules, const WeaponTuning &weapon,
                              const int slot, const std::optional<bool> on_robot) {
  const auto found = std::ranges::find_if(rules, [&](const WeaponSoundRule &rule) {
    return rule.matches(weapon, slot) &&
           (rule.on_robot < 0 || (on_robot && rule.on_robot == static_cast<int>(*on_robot)));
  });
  return found == rules.end() ? nullptr : &found->sound;
}

std::vector<SoundCue> place_cues(const SoundFalloff &falloff, const SoundListener *listener,
                                 std::vector<PlacedCue> cues) {
  std::vector<SoundCue> out;
  out.reserve(cues.size());
  for (PlacedCue &placed : cues) {
    if (listener != nullptr) {
      placed.cue.mix = place_sound(falloff, *listener, placed.at);
      placed.cue.position = placed.at;
    }
    out.push_back(std::move(placed.cue));
  }
  if (listener != nullptr) {
    SoundCue update;
    update.action = SoundCue::Action::listener;
    update.listener = *listener;
    update.falloff = falloff;
    out.push_back(update); // refresh existing one-shots, even without new battle events
  }
  return out;
}

std::uint64_t BattleSounds::boost_key(const std::size_t index) {
  return boost_key_base + index;
}

void BattleSounds::play(const std::string &name, const Vec3 at, const AudioBus bus) {
  if (!name.empty()) {
    SoundCue cue{SoundCue::Action::play, name};
    cue.bus = bus;
    cues_.push_back({std::move(cue), at});
  }
}

void BattleSounds::observe(const Battle &battle, const float seconds) {
  const std::vector<Combatant> &robots = battle.robots();
  watch_.resize(robots.size());
  for (std::size_t index = 0; index < robots.size(); ++index) {
    observe_robot(index, robots[index], seconds);
    observe_pilot(battle, index, seconds);
  }
  observe_events(battle);
  observe_beams(battle, seconds);
  observe_sustained(battle);
  observe_fire_pairs(battle);
  observe_carrier(battle);
}

bool BattleSounds::stepped(const Combatant &robot, Watch &watch) {
  watch.stepped = false;
  if (!robot.pose || robot.pose->pose()[0].contacts.empty()) {
    watch.contacts.clear();
    return false;
  }
  const auto &points = robot.pose->pose()[0].contacts;
  for (std::size_t slot = 0; slot < points.size() && slot < watch.contacts.size(); ++slot) {
    watch.stepped |= contact_stepped(watch.contacts[slot], points[slot]);
  }
  watch.contacts = points;
  return true;
}

void BattleSounds::observe_robot(const std::size_t index, const Combatant &robot,
                                 const float seconds) {
  Watch &watch = watch_[index];
  const bool piloted = robot.piloted;
  const RobotMover &mover = robot.mover;
  const Vec3 at = robot.aim_origin(); // robot +0xb0
  if (!robot.alive) {
    watch.alive = false;
    watch.down_boosting = false;
    watch.grounded = mover.grounded();
    observe_boost(index, robot, watch);
    watch.mode = mover.mode();
    return;
  }
  watch.alive = true;
  const MoveMode mode = mover.mode();
  if (watch.grounded && !mover.grounded() && mover.velocity().y > 0) {
    play(table_.jump.pick(piloted), at);
  }
  if (mover.down_boosting() && !watch.down_boosting) {
    play(table_.down_boost.pick(piloted), at);
  }
  watch.down_boosting = mover.down_boosting();
  if (mover.grounded() && !watch.grounded) {
    play(landing_sound(table_, robot), at);
  }
  if (mover.dodged() != 0) {
    play((mover.grounded() ? table_.dodge : table_.air_dodge).pick(piloted), at);
  }
  // The walk's steps: a foot's contact keys (0x0ad737 counts their
  // crossings in +0x210c, 0x093637 then plays the step). A robot without
  // contact nodes counts them by time instead, faster as the walk speeds up.
  // A strafe alone takes no steps (0x08dede leaves the stand state's motion).
  const bool walking =
      mover.grounded() && !mover.strafing() && (mode == MoveMode::walk || mode == MoveMode::turn);
  const bool keyed = stepped(robot, watch);
  if (keyed) {
    if (walking && watch.stepped) {
      play(table_.footstep.pick(piloted), at);
    }
  } else if (walking) {
    const float full = std::max(mover.tuning().walk_motion_rate, 1e-3F);
    const float rate = mode == MoveMode::walk ? mover.motion_speed() / full : 1.0F;
    watch.step_left -= seconds * std::max(rate, 0.1F);
    if (watch.step_left <= 0.0F) {
      play(table_.footstep.pick(piloted), at);
      watch.step_left += table_.footstep_seconds;
    }
  } else {
    watch.step_left = 0.0F;
  }
  if (piloted && robot.hp < robot.max_hp * table_.low_armour_share) {
    watch.alarm_left -= seconds;
    if (watch.alarm_left <= 0.0F) {
      play(table_.low_armour.pick(piloted), at);
      watch.alarm_left = table_.low_armour_seconds;
    }
  } else {
    watch.alarm_left = 0.0F;
  }
  observe_boost(index, robot, watch);
  watch.mode = mode;
  watch.grounded = mover.grounded();
}

void BattleSounds::observe_boost(const std::size_t index, const Combatant &robot, Watch &watch) {
  const bool boosting = robot.alive && robot.mover.mode() == MoveMode::dash;
  const std::uint64_t key = boost_key(index);
  const Vec3 at = robot.aim_origin();
  const std::string &loop = table_.boost.pick(robot.piloted);
  if (boosting && !watch.boosting) {
    play(table_.boost_start.pick(robot.piloted), at);
    if (!loop.empty()) {
      SoundCue cue{SoundCue::Action::play, loop, {}, true, key};
      cue.bus = AudioBus::robots;
      cues_.push_back({std::move(cue), at});
    }
  } else if (boosting) {
    cues_.push_back({SoundCue{SoundCue::Action::move, {}, {}, false, key}, at});
  } else if (watch.boosting) {
    cues_.push_back({SoundCue{SoundCue::Action::stop, {}, {}, false, key}, at});
  }
  watch.boosting = boosting;
}

} // namespace phantomcrash
