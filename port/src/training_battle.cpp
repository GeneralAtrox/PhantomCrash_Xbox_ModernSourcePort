#include "phantomcrash/training_battle.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>

namespace phantomcrash {
namespace {

constexpr std::array<const char *, weapon_slots> slot_keys{"right_arm", "left_arm",
                                                           "right_shoulder", "left_shoulder"};
constexpr std::array<const char *, 2> message_keys{"first", "second"};

TrainingRobot parse_robot(const json::JsonValue &value, TrainingRobot robot) {
  if (const json::JsonValue *weapons = value.find("weapons")) {
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      robot.weapons[slot] = weapons->string_or(slot_keys[slot], "");
    }
  }
  robot.hp = static_cast<float>(value.number_or("hp", robot.hp));
  robot.pilot = static_cast<int>(value.number_or("pilot", robot.pilot));
  robot.boss = value.boolean_or("boss", robot.boss);
  if (const json::JsonValue *modules = value.find("modules")) {
    robot.modules.clear();
    for (const json::JsonValue &module : modules->array()) {
      robot.modules.push_back(static_cast<int>(module.number()));
    }
  }
  if (const json::JsonValue *chip = value.find("chip")) {
    robot.chip = static_cast<int>(json::required(*chip, "index").number());
    robot.chip_level = std::clamp(static_cast<int>(chip->number_or("level", 0.0)), -part_level_max,
                                  part_level_max);
  }
  return robot;
}

void read_button(const json::JsonValue &value, const char *key, Buttons &out) {
  if (const json::JsonValue *found = value.find(key)) {
    out = buttons_from_json(*found);
  }
}

BattleButtons parse_buttons(const json::JsonValue &value) {
  BattleButtons buttons;
  if (const json::JsonValue *fire = value.find("fire")) {
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      read_button(*fire, slot_keys[slot], buttons.fire[slot]);
    }
  }
  if (const json::JsonValue *labels = value.find("fire_labels")) {
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      buttons.fire_labels[slot] = labels->string_or(slot_keys[slot], buttons.fire_labels[slot]);
    }
  }
  read_button(value, "camouflage", buttons.camouflage);
  read_button(value, "jump", buttons.jump);
  read_button(value, "dash", buttons.dash);
  read_button(value, "down_boost", buttons.down_boost);
  read_button(value, "reset_twist", buttons.reset_twist);
  read_button(value, "dodge_left", buttons.dodge_left);
  read_button(value, "dodge_right", buttons.dodge_right);
  read_button(value, "exit", buttons.exit);
  read_button(value, "view_first_person", buttons.view_first_person);
  read_button(value, "view_behind", buttons.view_behind);
  read_button(value, "view_next", buttons.view_next);
  read_button(value, "view_previous", buttons.view_previous);
  return buttons;
}

} // namespace

TrainingBattle parse_training_battle(const json::JsonValue &value) {
  TrainingBattle battle;
  battle.settings = parse_battle_settings(value);
  if (const json::JsonValue *camera = value.find("chase_camera")) {
    battle.camera = parse_chase_camera(*camera);
  }
  if (const json::JsonValue *views = value.find("camera_views")) {
    battle.views = parse_camera_views(*views);
  }
  if (const json::JsonValue *optical = value.find("optical_camouflage")) {
    battle.optical = parse_optical_camouflage(*optical);
  }
  if (const json::JsonValue *shield = value.find("entry_shield")) {
    battle.shield_look = parse_entry_shield_look(*shield);
  }
  if (const json::JsonValue *shadows = value.find("robot_shadows")) {
    battle.shadows = parse_robot_shadow_look(*shadows);
  }
  if (const json::JsonValue *supply = value.find("supply")) {
    battle.supply = parse_supply_settings(*supply);
  }
  if (const json::JsonValue *buttons = value.find("buttons")) {
    battle.buttons = parse_buttons(*buttons);
  }
  if (const json::JsonValue *scenes = value.find("scenes")) {
    battle.scenes = parse_scene_tuning(*scenes);
  }
  if (const json::JsonValue *courses = value.find("courses")) {
    battle.courses = parse_lesson_courses(*courses);
  }
  battle.lessons = value.string_or("lessons", battle.lessons);
  battle.chatter = value.string_or("chatter", battle.chatter);
  battle.chatter_sound = value.string_or("chatter_sound", battle.chatter_sound);
  battle.prize_sound = value.string_or("prize_sound", battle.prize_sound);
  battle.chatter_frame_rate = value.number_or("chatter_frame_rate", battle.chatter_frame_rate);
  battle.enemy_path = value.string_or("enemy_path", battle.enemy_path);
  battle.stride_speeds = value.boolean_or("stride_speeds", battle.stride_speeds);
  battle.armour_from_parts = value.boolean_or("armour_from_parts", battle.armour_from_parts);
  if (const json::JsonValue *player = value.find("player")) {
    battle.player = parse_robot(*player, battle.player);
    if (const json::JsonValue *start = player->find("start")) {
      const auto at = json::numbers<3>(*start, "player start");
      battle.player_start = {at[0], at[1], at[2]};
    }
    battle.player_heading = radians(static_cast<float>(player->number_or("heading_degrees", 0.0)));
  }
  if (const json::JsonValue *drone = value.find("drone")) {
    battle.drone = parse_robot(*drone, battle.drone);
    battle.drones = static_cast<int>(drone->number_or("count", battle.drones));
  }
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    for (const std::string &name : {battle.player.weapons[slot], battle.drone.weapons[slot]}) {
      if (!name.empty() && !battle.settings.weapons.contains(name)) {
        throw std::runtime_error("unknown weapon \"" + name + "\"");
      }
    }
  }
  return battle;
}

LessonMessages parse_lesson_messages(const json::JsonValue &document, const GlyphFont &font,
                                     const bool standard_controls) {
  LessonMessages messages;
  for (const auto &[state, entry] : json::required(document, "states").object()) {
    auto &scripts = messages[std::stoi(state)];
    const json::JsonValue *standard = standard_controls ? entry.find("standard") : nullptr;
    for (std::size_t index = 0; index < message_keys.size(); ++index) {
      const json::JsonValue *commands =
          standard != nullptr ? standard->find(message_keys[index]) : nullptr;
      if (commands == nullptr) {
        commands = entry.find(message_keys[index]);
      }
      if (commands != nullptr) {
        scripts[index] = parse_event_commands(*commands, font);
      }
    }
  }
  return messages;
}

RobotControl battle_control(const InputFrame &input, const BattleButtons &buttons,
                            const LessonStep *step, const bool standard, const bool beginner) {
  RobotControl control;
  if (standard) {
    // Robot +0x2740 bit 0 set: the left stick's x strafes (+0x2f68, moved
    // sideways by 0x08dede), the right stick's turns (+0x2f60).
    control.move = {input.right_stick[0], input.left_stick[1]};
    control.strafe = input.left_stick[0];
    // The mover adds Standard's double-flick dodge and its turn in the air.
    control.standard = true;
  } else {
    // Clear: the left stick's x turns (+0x2f60), the right stick's twists
    // the upper body (+0x2f68 into +0xf8, 0x07d330).
    control.move = {input.left_stick[0], input.left_stick[1]};
    control.twist = input.right_stick[0];
    // BEGINNER MODE (robot +0x273c) turns it into the quick turn.
    control.quick_turn = beginner;
  }
  // The original torso-twist lesson still needs its dedicated upper-body control.
  if (standard && step &&
      (step->goal == LessonGoal::twist_both || step->goal == LessonGoal::twist_reset)) {
    control.twist = control.move[0];
    control.move[0] = 0;
  }
  control.pitch = input.right_stick[1];
  control.mouse_aim = input.mouse_aim;
  if (step != nullptr) {
    control.move[1] = std::clamp(control.move[1], -step->walk_limit, step->walk_limit);
    if (step->turn_only != 0 && control.move[0] * static_cast<float>(step->turn_only) < 0.0F) {
      control.move[0] = 0.0F;
    }
  }
  control.jump = (input.held & buttons.jump) != 0;
  control.dash = input.any_pressed(buttons.dash);
  control.down_boost = input.any_pressed(buttons.down_boost);
  control.reset_twist = input.any_pressed(buttons.reset_twist);
  if (input.any_pressed(buttons.dodge_left)) {
    control.dodge = -1;
  } else if (input.any_pressed(buttons.dodge_right)) {
    control.dodge = 1;
  }
  return control;
}

FireControl battle_fire(const InputFrame &input, const BattleButtons &buttons) {
  FireControl fire;
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    fire.fire[slot] = (input.held & buttons.fire[slot]) != 0;
  }
  fire.camouflage = input.any_pressed(buttons.camouflage);
  fire.fire[static_cast<std::size_t>(WeaponSlot::left_arm)] |= input.mouse_fire_left;
  fire.fire[static_cast<std::size_t>(WeaponSlot::right_arm)] |= input.mouse_fire_right;
  return fire;
}

void apply_button_config(BattleButtons &buttons, const ControlOptions &controls) {
  if (controls.bindings == ControlOptions{}.bindings) {
    return;
  }
  const auto bound = [&controls](const std::size_t action) {
    return static_cast<std::size_t>(
        std::clamp(controls.bindings[action], 0, static_cast<int>(control_action_count) - 1));
  };
  // Actions 0-3 are the weapons in WeaponSlot order.
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    buttons.fire[slot] = bindable_buttons[bound(slot)];
    buttons.fire_labels[slot] = std::string(bindable_button_labels[bound(slot)]);
  }
  buttons.dodge_right = bindable_buttons[bound(4)];
  buttons.dodge_left = bindable_buttons[bound(5)];
  buttons.jump = bindable_buttons[bound(6)];
  buttons.camouflage = bindable_buttons[bound(7)];
}

InputFrame view_changed(InputFrame input, const ControlOptions &controls) {
  if (controls.view != 0) {
    input.right_stick[1] = -input.right_stick[1];
  }
  return input;
}

LessonSample lesson_sample(const Combatant &player, const float heading_before,
                           const float seconds) {
  LessonSample sample;
  sample.mode = player.mover.mode();
  sample.grounded = player.mover.grounded();
  sample.local_velocity = player.mover.local_velocity();
  sample.seconds = seconds;
  sample.heading_change = wrap_angle(player.mover.heading() - heading_before);
  sample.height = player.mover.position().y;
  sample.dodged = player.mover.dodged();
  sample.twist = player.mover.twist();
  sample.kills = player.kills;
  sample.camouflaged = player.camouflaged;
  return sample;
}

} // namespace phantomcrash
