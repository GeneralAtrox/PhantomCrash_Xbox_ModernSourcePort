#include "phantomcrash/vs_battle.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>

namespace phantomcrash {
namespace {

VsRoundTuning parse_round(const json::JsonValue &value, VsRoundTuning round) {
  round.frame_rate = value.number_or("frame_rate", round.frame_rate);
  round.settle_frames = value.number_or("settle_frames", round.settle_frames);
  round.fade_frames = value.number_or("fade_frames", round.fade_frames);
  if (const json::JsonValue *sounds = value.find("sounds")) {
    round.move_sound = sounds->string_or("move", round.move_sound);
    round.answer_sound = sounds->string_or("answer", round.answer_sound);
    round.confirm_sound = sounds->string_or("confirm", round.confirm_sound);
    round.cancel_sound = sounds->string_or("cancel", round.cancel_sound);
  }
  return round;
}

VsOverlay parse_overlay(const json::JsonValue &value) {
  VsOverlay overlay;
  overlay.window = parse_sprite(json::required(value, "window"));
  const auto &items = json::required(value, "items").array();
  if (items.size() != vs_menu_items) {
    throw std::runtime_error("overlay.items: needs RETRY, SELECT AREA, SELECT SV and EXIT");
  }
  for (std::size_t index = 0; index < items.size(); ++index) {
    overlay.items[index] = parse_sprite(items[index]);
  }
  overlay.dim_colour = parse_colour(value.string_or("dim_colour", "#808080"));
  const auto &results = json::required(value, "results");
  overlay.results = {parse_sprite(json::required(results, "win")),
                     parse_sprite(json::required(results, "lose")),
                     parse_sprite(json::required(results, "draw"))};
  overlay.result_halves = json::numbers<2>(json::required(results, "halves"), "results.halves");
  overlay.result_quarters =
      json::numbers<2>(json::required(results, "quarters"), "results.quarters");
  overlay.result_fade_frames = results.number_or("fade_frames", overlay.result_fade_frames);
  overlay.cover = parse_sprite(json::required(value, "cover"));
  for (const json::JsonValue &line : json::required(value, "question").array()) {
    overlay.question.push_back(line.string());
  }
  return overlay;
}

// A rider's weapons take its name, so that the riders' own tunings do not
// replace one another's in the battle's weapons.
void add_rider(TrainingScript &script, const TrainingScript &own, const std::string &name,
               const int port) {
  TrainingRider rider{name, port, own.battle.player};
  for (std::string &weapon : rider.robot.weapons) {
    const auto found = own.battle.settings.weapons.find(weapon);
    if (weapon.empty() || found == own.battle.settings.weapons.end()) {
      continue;
    }
    WeaponTuning tuning = found->second;
    tuning.name = weapon + "_" + name;
    weapon = tuning.name;
    script.battle.settings.weapons[tuning.name] = std::move(tuning);
  }
  script.robots[name] = own.robots.at("player");
  script.battle.riders.push_back(std::move(rider));
}

} // namespace

VsBattle parse_vs_battle(const json::JsonValue &document) {
  try {
    VsBattle battle;
    const auto &value = json::required(document, "battle");
    battle.return_to = value.string_or("return_to", battle.return_to);
    if (const json::JsonValue *lesson = value.find("lesson")) {
      battle.lesson.title = lesson->string_or("title", battle.lesson.title);
      battle.lesson.mode = static_cast<int>(lesson->number_or("mode", battle.lesson.mode));
    }
    if (const json::JsonValue *stages = value.find("stages")) {
      for (const json::JsonValue &stage : stages->array()) {
        battle.stages.push_back(stage.string());
      }
    }
    battle.gate_step = static_cast<float>(value.number_or("gate_step", battle.gate_step));
    if (const json::JsonValue *routes = value.find("routes")) {
      constexpr std::array<const char *, vs_menu_items> names{"retry", "select_area", "select_sv",
                                                              "exit"};
      for (std::size_t index = 0; index < names.size(); ++index) {
        battle.routes[index] = routes->string_or(names[index], battle.routes[index]);
      }
    }
    if (const json::JsonValue *round = document.find("round")) {
      battle.round = parse_round(*round, battle.round);
    }
    battle.overlay = parse_overlay(json::required(document, "overlay"));
    return battle;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("vs_battle.json: ") + error.what());
  }
}

TrainingLesson vs_lesson(const VsBattle &battle, const int players) {
  TrainingLesson lesson = battle.lesson;
  lesson.screens = std::clamp(players, 1, 4);
  return lesson;
}

TrainingScript vs_training(const TrainingScript &base, const TestDriveScript &drive,
                           const VsBattle &battle, const VsSession &session,
                           const PartTables &parts, const GarageTables &tables) {
  TrainingScript script = base;
  script.return_to = battle.routes[static_cast<std::size_t>(VsChoice::exit)];
  if (session.stage >= 0 && session.stage < static_cast<int>(battle.stages.size()) &&
      !battle.stages[static_cast<std::size_t>(session.stage)].empty()) {
    script.stage = battle.stages[static_cast<std::size_t>(session.stage)];
  }
  script.battle.drones = 0;
  script.battle.rider_gate_step = battle.gate_step;
  for (int port = 0; port < controller_ports; ++port) {
    const auto &robot = session.robots[static_cast<std::size_t>(port)];
    if (!session.joined[static_cast<std::size_t>(port)] || !robot) {
      continue;
    }
    // The Scoobee is nobody's garage's: a garage of its own holds it.
    Garage lent;
    install_robot(lent, 0, *robot, parts, tables);
    const TrainingScript own = test_drive_training(base, drive, lent, 0, parts);
    const bool first = script.battle.riders.empty();
    add_rider(script, own, first ? "player" : "vs" + std::to_string(port + 1), port);
    if (first) {
      script.battle.player = script.battle.riders.back().robot;
    }
  }
  return script;
}

} // namespace phantomcrash
