#include "phantomcrash/online_battle_setup.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/online_agreement.hpp"
#include <cmath>
#include <limits>

namespace phantomcrash::online {
namespace {
using json::JsonValue;
JsonValue number(const std::int64_t value) {
  return JsonValue{value};
}

std::int64_t integer(const JsonValue &value, const char *key, const std::int64_t low,
                     const std::int64_t high) {
  const double number = json::required(value, key).number();
  if (!std::isfinite(number) || number < low || number > high || std::trunc(number) != number) {
    throw std::runtime_error(std::string("Invalid online battle setup field: ") + key);
  }
  return static_cast<std::int64_t>(number);
}

JsonValue entry_json(const RumblingEntry &entry) {
  return JsonValue{JsonValue::Object{{"area", number(entry.area)},
                                     {"contest", number(entry.contest)},
                                     {"gate", number(entry.gate)},
                                     {"rental", JsonValue{entry.rental}},
                                     {"rental_robot", number(entry.rental_robot)},
                                     {"ranker_gate", number(entry.ranker_gate)}}};
}

RumblingEntry read_entry(const JsonValue &value) {
  RumblingEntry entry;
  entry.area = static_cast<int>(integer(value, "area", 0, roster_areas - 1));
  entry.contest = static_cast<int>(integer(value, "contest", 0, std::numeric_limits<int>::max()));
  entry.gate =
      static_cast<int>(integer(value, "gate", 0, std::numeric_limits<int>::max() - max_players));
  entry.rental = json::required(value, "rental").boolean();
  entry.rental_robot =
      static_cast<int>(integer(value, "rental_robot", -1, std::numeric_limits<int>::max()));
  entry.ranker_gate =
      static_cast<int>(integer(value, "ranker_gate", -1, std::numeric_limits<int>::max()));
  return entry;
}

void validate(const BattleSetup &setup) {
  if (setup.area < 0 || setup.area >= roster_areas || setup.contest < 0 || setup.robots < 4 ||
      setup.robots > max_rumble_robots || setup.world.day < 0 || !setup.participants() ||
      setup.options.enemy_level_offset < -99 || setup.options.enemy_level_offset > 99 ||
      setup.options.kill_ny_percent < 0 || setup.options.kill_ny_percent > 1000) {
    throw std::runtime_error("Invalid online battle setup");
  }
  for (const auto &player : setup.players) {
    if (player && (player->profile.day != setup.world.day || player->entry.area != setup.area ||
                   player->entry.contest != setup.contest || player->entry.gate < 0 ||
                   player->entry.gate > std::numeric_limits<int>::max() - max_players ||
                   player->entry.rental_robot < -1 || player->entry.ranker_gate < -1 ||
                   (player->entry.rental && player->entry.rental_robot < 0))) {
      throw std::runtime_error("Online player does not belong to the proposed arena");
    }
  }
}
} // namespace

std::uint8_t BattleSetup::participants() const {
  std::uint8_t mask{};
  for (std::uint8_t slot = 0; slot < max_players; ++slot) {
    if (players[slot]) {
      mask |= static_cast<std::uint8_t>(1U << slot);
    }
  }
  return mask;
}

std::vector<std::uint8_t> encode_battle_setup(const BattleSetup &setup) {
  validate(setup);
  JsonValue::Array players;
  for (const auto &player : setup.players) {
    players.push_back(player
                          ? JsonValue{JsonValue::Object{{"profile", quest_to_json(player->profile)},
                                                        {"entry", entry_json(player->entry)}}}
                          : JsonValue{});
  }
  const auto &options = setup.options;
  JsonValue document{JsonValue::Object{
      {"version", number(1)},
      {"seed", number(setup.seed)},
      {"area", number(setup.area)},
      {"contest", number(setup.contest)},
      {"robots", number(setup.robots)},
      {"world", quest_to_json(setup.world)},
      {"players", JsonValue{std::move(players)}},
      {"options",
       JsonValue{JsonValue::Object{{"enemy_level_offset", number(options.enemy_level_offset)},
                                   {"kill_ny_percent", number(options.kill_ny_percent)},
                                   {"share_cash", JsonValue{options.share_cash}},
                                   {"share_shootdowns", JsonValue{options.share_shootdowns}},
                                   {"share_boss_clears", JsonValue{options.share_boss_clears}}}}}}};
  const auto text = json::write(document);
  if (text.size() > max_timeline_setup_bytes) {
    throw std::runtime_error("Online battle setup exceeds transfer capacity");
  }
  return {text.begin(), text.end()};
}

BattleSetup decode_battle_setup(const std::span<const std::uint8_t> bytes) {
  if (bytes.empty() || bytes.size() > max_timeline_setup_bytes) {
    throw std::runtime_error("Invalid online battle setup size");
  }
  const auto document = json::parse(std::string(bytes.begin(), bytes.end()));
  if (integer(document, "version", 1, 1) != 1) {
    throw std::runtime_error("Unsupported online battle setup version");
  }
  BattleSetup setup;
  setup.seed = static_cast<std::uint32_t>(
      integer(document, "seed", 0, std::numeric_limits<std::uint32_t>::max()));
  setup.area = static_cast<int>(integer(document, "area", 0, roster_areas - 1));
  setup.contest =
      static_cast<int>(integer(document, "contest", 0, std::numeric_limits<int>::max()));
  setup.robots = static_cast<int>(integer(document, "robots", 4, max_rumble_robots));
  setup.world = quest_from_json(json::required(document, "world"));
  const auto &players = json::required(document, "players").array();
  if (players.size() != max_players) {
    throw std::runtime_error("Online battle setup requires assigned player slots");
  }
  for (std::size_t slot = 0; slot < players.size(); ++slot) {
    if (!players[slot].is_null()) {
      setup.players[slot] = BattlePlayer{quest_from_json(json::required(players[slot], "profile")),
                                         read_entry(json::required(players[slot], "entry"))};
    }
  }
  const auto &options = json::required(document, "options");
  setup.options.enemy_level_offset =
      static_cast<int>(integer(options, "enemy_level_offset", -99, 99));
  setup.options.kill_ny_percent = static_cast<int>(integer(options, "kill_ny_percent", 0, 1000));
  setup.options.share_cash = json::required(options, "share_cash").boolean();
  setup.options.share_shootdowns = json::required(options, "share_shootdowns").boolean();
  setup.options.share_boss_clears = json::required(options, "share_boss_clears").boolean();
  validate(setup);
  return setup;
}
} // namespace phantomcrash::online
