#include "phantomcrash/rumbling_battle.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>

namespace phantomcrash {

namespace {

QuickRental parse_quick_rental(const json::JsonValue &value) {
  return {json::required(value, "label").string(),
          json::required(value, "screen").string(),
          static_cast<int>(json::required(value, "day").number()),
          static_cast<int>(json::required(value, "area").number()),
          static_cast<int>(json::required(value, "gate").number()),
          static_cast<int>(json::required(value, "robot").number())};
}

} // namespace

const QuickRental *find_quick_rental(const RumblingBattle &battle, const std::string_view screen) {
  const auto found = std::ranges::find(battle.quick_rentals, screen, &QuickRental::screen);
  return found != battle.quick_rentals.end() ? &*found : nullptr;
}

RumblingBattle parse_rumbling_battle(const json::JsonValue &document) {
  try {
    RumblingBattle battle;
    const auto &value = json::required(document, "battle");
    battle.return_to = value.string_or("return_to", battle.return_to);
    battle.hit_reward_ny = static_cast<int>(value.number_or("hit_reward_ny", battle.hit_reward_ny));
    battle.prize_sound = value.string_or("prize_sound", battle.prize_sound);
    // quick_rental, a single entry, is the older form; mods may still use it.
    if (const auto *quick = value.find("quick_rental")) {
      battle.quick_rentals.push_back(parse_quick_rental(*quick));
    }
    if (const auto *quick = value.find("quick_rentals")) {
      for (const json::JsonValue &entry : quick->array()) {
        battle.quick_rentals.push_back(parse_quick_rental(entry));
      }
    }
    for (const QuickRental &rental : battle.quick_rentals) {
      const std::string &screen = rental.screen;
      if (screen.empty() || screen == "rumbling_battle" ||
          std::ranges::count(battle.quick_rentals, screen, &QuickRental::screen) > 1) {
        throw std::runtime_error("quick_rentals: each needs its own screen name, not '" + screen +
                                 "'");
      }
    }
    if (const json::JsonValue *lesson = value.find("lesson")) {
      battle.lesson.title = lesson->string_or("title", battle.lesson.title);
      battle.lesson.mode = static_cast<int>(lesson->number_or("mode", battle.lesson.mode));
    }
    return battle;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("rumbling.json: ") + error.what());
  }
}

std::optional<RumblingEntry> prepare_quick_rental(const QuickRental &preset,
                                                  const ContestTables &contests,
                                                  const RumblingRules &rules,
                                                  const GarageTables &garage, QuestSave &quest) {
  const RobotRecord *robot = garage.robot(preset.robot);
  if (preset.day < 0 || preset.area < 0 || preset.area >= calendar_venues || preset.gate < 0 ||
      robot == nullptr || !robot->complete() ||
      std::ranges::find(rules.rental_robots, preset.robot) == rules.rental_robots.end()) {
    return std::nullopt;
  }
  const int held = contest_on(contests, preset.day, preset.area, quest.cleared);
  const Contest *contest = contests.contest(held);
  const ContestClass *category = contest ? contests.contest_class(contest->contest_class) : nullptr;
  if (category == nullptr || !rental_allowed(rules, contest->contest_class) || category->fee < 0 ||
      quest.money < category->fee) {
    return std::nullopt;
  }
  quest.day = preset.day;
  quest.money -= category->fee;
  quest.rosters = {};
  quest.live_epoch = 0;
  return RumblingEntry{preset.area, held, preset.gate, true, preset.robot};
}

TrainingScript rumbling_training(const TrainingScript &base, const TestDriveScript &drive,
                                 const RumblingBattle &battle, const RumblingEntry &entry,
                                 const Garage &garage, const PartTables &parts,
                                 const GarageTables &tables) {
  TrainingScript script;
  const RobotRecord *rental = entry.rental ? tables.robot(entry.rental_robot) : nullptr;
  if (rental != nullptr) {
    // The rental is nobody's: a garage of its own holds it.
    Garage lent;
    install_robot(lent, 0, *rental, parts, tables);
    script = test_drive_training(base, drive, lent, 0, parts);
  } else {
    script = test_drive_training(base, drive, garage, garage.riding, parts);
  }
  script.return_to = battle.return_to;
  // RUMBLING uses the regular entrance cues; the inherited tutorial suppresses them.
  script.battle.scenes.regular_battle = true;
  // A contest is battle mode 0: hits pay and the prize is heard growing.
  script.battle.settings.hit_reward_ny = battle.hit_reward_ny;
  script.battle.prize_sound = battle.prize_sound;
  return script;
}

} // namespace phantomcrash
