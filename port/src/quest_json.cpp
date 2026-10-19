// A game's save, as the settings file keeps it (the game kept now and the
// save slots).

#include "phantomcrash/settings.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace phantomcrash {
namespace {

constexpr std::array<const char *, 4> source_names{"member", "leader", "champion", "extra"};

RosterSource source_from(const std::string &name) {
  for (std::size_t k = 0; k < source_names.size(); ++k) {
    if (name == source_names[k]) {
      return static_cast<RosterSource>(k);
    }
  }
  return RosterSource::member;
}

LiveRobot live_from_json(const json::JsonValue &value) {
  LiveRobot live;
  live.placed = value.boolean_or("placed", false);
  if (const auto *at = value.find("position"); at != nullptr && at->is_array()) {
    const auto &xyz = at->array();
    for (std::size_t k = 0; k < live.position.size() && k < xyz.size(); ++k) {
      live.position[k] = static_cast<float>(xyz[k].number());
    }
  }
  live.heading = static_cast<float>(value.number_or("heading", 0.0));
  live.armour = static_cast<float>(value.number_or("armour", 0.0));
  live.max_armour = static_cast<float>(value.number_or("max_armour", 0.0));
  if (const auto *ammo = value.find("ammo"); ammo != nullptr && ammo->is_array()) {
    const auto &rounds = ammo->array();
    for (std::size_t k = 0; k < live.ammo.size() && k < rounds.size(); ++k) {
      live.ammo[k] = static_cast<int>(rounds[k].number());
    }
  }
  live.shield = static_cast<float>(value.number_or("shield", 0.0));
  live.down = value.boolean_or("down", false);
  live.arrival = static_cast<float>(value.number_or("arrival", 0.0));
  live.kills = static_cast<int>(value.number_or("kills", 0));
  return live;
}

json::JsonValue live_to_json(const LiveRobot &live) {
  using json::JsonValue;
  JsonValue::Array position;
  for (const float value : live.position) {
    position.push_back(JsonValue{static_cast<double>(value)});
  }
  JsonValue::Array ammo;
  for (const int rounds : live.ammo) {
    ammo.push_back(JsonValue{static_cast<std::int64_t>(rounds)});
  }
  return JsonValue{JsonValue::Object{
      {"placed", JsonValue{live.placed}},
      {"position", JsonValue{std::move(position)}},
      {"heading", JsonValue{static_cast<double>(live.heading)}},
      {"armour", JsonValue{static_cast<double>(live.armour)}},
      {"max_armour", JsonValue{static_cast<double>(live.max_armour)}},
      {"ammo", JsonValue{std::move(ammo)}},
      {"shield", JsonValue{static_cast<double>(live.shield)}},
      {"down", JsonValue{live.down}},
      {"arrival", JsonValue{static_cast<double>(live.arrival)}},
      {"kills", JsonValue{static_cast<std::int64_t>(live.kills)}},
  }};
}

AreaRoster roster_from_json(const json::JsonValue &value) {
  AreaRoster roster;
  roster.day = static_cast<int>(value.number_or("day", -1));
  roster.contest = static_cast<int>(value.number_or("contest", -1));
  roster.drawn_for = static_cast<int>(value.number_or("drawn_for", roster.contest));
  if (const auto *eliminated = value.find("eliminated")) {
    for (const auto &entry : eliminated->array()) {
      roster.eliminated.push_back(static_cast<int>(entry.number()));
    }
  }
  if (const auto *members = value.find("members"); members != nullptr && members->is_array()) {
    for (const auto &entry : members->array()) {
      RosterMember member;
      member.entrant = static_cast<int>(entry.number_or("entrant", -1));
      member.source = source_from(entry.string_or("source", "member"));
      member.team = static_cast<int>(entry.number_or("team", -1));
      member.place = static_cast<int>(entry.number_or("place", 0));
      if (const auto *live = entry.find("live"); live != nullptr && live->is_object()) {
        member.live = live_from_json(*live);
      }
      if (member.entrant >= 0) {
        roster.members.push_back(member);
      }
    }
  }
  return roster;
}

json::JsonValue roster_to_json(const AreaRoster &roster) {
  using json::JsonValue;
  JsonValue::Array members;
  JsonValue::Array eliminated;
  for (const int entrant : roster.eliminated) {
    eliminated.emplace_back(static_cast<std::int64_t>(entrant));
  }
  for (const RosterMember &member : roster.members) {
    members.push_back(JsonValue{JsonValue::Object{
        {"entrant", JsonValue{static_cast<std::int64_t>(member.entrant)}},
        {"source", JsonValue{std::string(source_names[static_cast<std::size_t>(member.source)])}},
        {"team", JsonValue{static_cast<std::int64_t>(member.team)}},
        {"place", JsonValue{static_cast<std::int64_t>(member.place)}},
        {"live", live_to_json(member.live)},
    }});
  }
  return JsonValue{JsonValue::Object{
      {"day", JsonValue{static_cast<std::int64_t>(roster.day)}},
      {"contest", JsonValue{static_cast<std::int64_t>(roster.contest)}},
      {"drawn_for", JsonValue{static_cast<std::int64_t>(roster.drawn_for)}},
      {"eliminated", JsonValue{std::move(eliminated)}},
      {"members", JsonValue{std::move(members)}},
  }};
}

} // namespace

QuestSave quest_from_json(const json::JsonValue &value) {
  QuestSave quest;
  quest.player_name = value.string_or("player_name", "");
  quest.day = std::max(0, static_cast<int>(value.number_or("day", 0)));
  quest.money = std::max<std::int64_t>(0, static_cast<std::int64_t>(value.number_or("money", 0)));
  quest.cleared = value.boolean_or("cleared", false);
  quest.riding = value.string_or("riding", "");
  quest.saved = value.string_or("saved", "");
  quest.spin_paid = std::max(0, static_cast<int>(value.number_or("spin_paid", 0)));
  quest.total_days = std::max(0, static_cast<int>(value.number_or("total_days", 0)));
  quest.laps = std::max(0, static_cast<int>(value.number_or("laps", 0)));
  quest.hide_incompatible = value.boolean_or("hide_incompatible", false);
  if (const auto *seen = value.find("seen_events"); seen != nullptr && seen->is_array()) {
    for (const auto &scene : seen->array()) {
      if (scene.is_number()) {
        quest.seen_events.push_back(static_cast<int>(scene.number()));
      }
    }
  }
  if (const auto *won = value.find("won_contests"); won != nullptr && won->is_array()) {
    for (const auto &contest : won->array()) {
      if (contest.is_number()) {
        quest.won_contests.push_back(static_cast<int>(contest.number()));
      }
    }
  }
  if (const auto *beaten = value.find("beaten"); beaten != nullptr && beaten->is_array()) {
    for (const auto &key : beaten->array()) {
      if (key.is_number()) {
        quest.beaten.push_back(static_cast<int>(key.number()));
      }
    }
  }
  if (const auto *rosters = value.find("rosters"); rosters != nullptr && rosters->is_array()) {
    const auto &list = rosters->array();
    for (std::size_t area = 0; area < quest.rosters.size() && area < list.size(); ++area) {
      quest.rosters[area] = roster_from_json(list[area]);
    }
  }
  if (const auto *records = value.find("records"); records != nullptr && records->is_object()) {
    for (const auto &[name, count] : records->object()) {
      if (count.is_number()) {
        quest.records[name] = static_cast<std::int64_t>(count.number());
      }
    }
  }
  if (const auto *music = value.find("music")) {
    quest.music = music_from_json(*music);
  }
  if (const auto *garage = value.find("garage")) {
    quest.garage = garage_from_json(*garage);
  }
  return quest;
}

json::JsonValue quest_to_json(const QuestSave &quest) {
  using json::JsonValue;
  JsonValue::Array seen;
  for (const int scene : quest.seen_events) {
    seen.push_back(JsonValue{static_cast<std::int64_t>(scene)});
  }
  JsonValue::Array won;
  for (const int contest : quest.won_contests) {
    won.push_back(JsonValue{static_cast<std::int64_t>(contest)});
  }
  JsonValue::Array beaten;
  for (const int key : quest.beaten) {
    beaten.push_back(JsonValue{static_cast<std::int64_t>(key)});
  }
  JsonValue::Array rosters;
  for (const AreaRoster &roster : quest.rosters) {
    rosters.push_back(roster_to_json(roster));
  }
  JsonValue::Object records;
  for (const auto &[name, count] : quest.records) {
    records.emplace_back(name, JsonValue{count});
  }
  return JsonValue{JsonValue::Object{
      {"player_name", JsonValue{quest.player_name}},
      {"seen_events", JsonValue{std::move(seen)}},
      {"day", JsonValue{static_cast<std::int64_t>(quest.day)}},
      {"money", JsonValue{quest.money}},
      {"cleared", JsonValue{quest.cleared}},
      {"riding", JsonValue{quest.riding}},
      {"saved", JsonValue{quest.saved}},
      {"spin_paid", JsonValue{static_cast<std::int64_t>(quest.spin_paid)}},
      {"total_days", JsonValue{static_cast<std::int64_t>(quest.total_days)}},
      {"laps", JsonValue{static_cast<std::int64_t>(quest.laps)}},
      {"hide_incompatible", JsonValue{quest.hide_incompatible}},
      {"won_contests", JsonValue{std::move(won)}},
      {"beaten", JsonValue{std::move(beaten)}},
      {"rosters", JsonValue{std::move(rosters)}},
      {"records", JsonValue{std::move(records)}},
      {"music", music_to_json(quest.music)},
      {"garage", garage_to_json(quest.garage)},
  }};
}

} // namespace phantomcrash
