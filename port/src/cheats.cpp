#include "phantomcrash/cheats.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

namespace phantomcrash {
namespace {

int integer_or(const json::JsonValue &value, const char *key, const int fallback) {
  return static_cast<int>(value.number_or(key, fallback));
}

// A body's model goes by its maker (its "class").
int body_model(const PartTables &parts, const int body) {
  return body >= 0 && body < static_cast<int>(parts.bodies.size())
             ? parts.bodies[static_cast<std::size_t>(body)].maker
             : -1;
}

// The body model `hangar` has fitted, or -1.
int fitted_body_model(const Garage &garage, const PartTables &parts, const Hangar &hangar) {
  const auto item = garage.stock.find(hangar.part(PartSlot::body));
  return item != garage.stock.end() ? body_model(parts, item->second.id) : -1;
}

// Records that are the same part to the player: the enemy-only copies of a
// shop part, the "Spare" and Roy's weapons, CHIPs that differ only in their
// chatter. `pick` is the one given: the first a shop sells, else the first.
struct SamePart {
  int pick{-1};
  bool sold{};
  std::vector<int> ids;
};

// Groups `records` by `key` (none when empty), in order of first record.
template <typename Record, typename Key, typename Sold>
std::vector<SamePart> same_parts(const std::vector<Record> &records, Key key, Sold sold) {
  std::vector<std::string> keys;
  std::vector<SamePart> groups;
  for (std::size_t id = 0; id < records.size(); ++id) {
    const std::string name = key(records[id]);
    if (name.empty()) {
      continue;
    }
    auto at = std::ranges::find(keys, name);
    if (at == keys.end()) {
      keys.push_back(name);
      groups.emplace_back();
      at = keys.end() - 1;
    }
    SamePart &group = groups[static_cast<std::size_t>(at - keys.begin())];
    group.ids.push_back(static_cast<int>(id));
    if (group.pick < 0 || (!group.sold && sold(records[id]))) {
      group.pick = static_cast<int>(id);
      group.sold = sold(records[id]);
    }
  }
  return groups;
}

int loose_copies(const Garage &garage, const PartKind kind, const std::vector<int> &ids) {
  return static_cast<int>(std::ranges::count_if(garage.stock, [&](const auto &entry) {
    const StockItem &item = entry.second;
    return item.kind == kind && item.hangar < 0 && std::ranges::find(ids, item.id) != ids.end();
  }));
}

// Adds loose copies of the group's pick until the group has `copies`, or
// the stock is full.
void top_up(Garage &owned, const PartTables &parts, const GarageTables &garage, const PartKind kind,
            const SamePart &group, const int copies) {
  const std::string name =
      kind == PartKind::chip ? part_name(parts, garage, kind, group.pick) : std::string{};
  for (int have = loose_copies(owned, kind, group.ids); have < copies; ++have) {
    if (owned.add_stock({kind, group.pick, 0, name, -1}) < 0) {
      return;
    }
  }
}

// Every record of `count` owned, in order.
void own_all(std::vector<int> &owned, const std::size_t count) {
  for (std::size_t id = 0; id < count; ++id) {
    const int value = static_cast<int>(id);
    if (const auto at = std::ranges::lower_bound(owned, value); at == owned.end() || *at != value) {
      owned.insert(at, value);
    }
  }
}

} // namespace

CheatTables parse_cheat_tables(const json::JsonValue &document) {
  CheatTables tables;
  if (const auto *robots = document.find("hangar_robots"); robots != nullptr) {
    for (const auto &robot : robots->array()) {
      tables.hangar_robots.push_back(static_cast<int>(robot.number()));
    }
  }
  tables.weapon_copies = std::max(integer_or(document, "weapon_copies", tables.weapon_copies), 0);
  tables.part_copies = std::max(integer_or(document, "part_copies", tables.part_copies), 0);
  tables.stock_size = std::max(integer_or(document, "stock_size", tables.stock_size), 0);
  return tables;
}

int fill_hangars(QuestSave &quest, const CheatTables &cheats, const PartTables &parts,
                 const GarageTables &garage) {
  Garage &owned = quest.garage;
  int fitted = 0;
  for (const int index : cheats.hangar_robots) {
    const RobotRecord *robot = garage.robot(index);
    if (robot == nullptr || !robot->part(PartSlot::body)) {
      continue;
    }
    const int model = body_model(parts, robot->part(PartSlot::body)->id);
    const bool held = std::ranges::any_of(owned.hangars, [&](const Hangar &hangar) {
      return fitted_body_model(owned, parts, hangar) == model;
    });
    const auto empty = std::ranges::find_if(owned.hangars, &Hangar::empty);
    if (held || empty == owned.hangars.end()) {
      continue;
    }
    const int hangar = static_cast<int>(empty - owned.hangars.begin());
    if (owned.free_stock() < robot->stock_needed()) {
      owned.stock_size = std::max(owned.stock_size, cheats.stock_size);
    }
    if (install_robot(owned, hangar, *robot, parts, garage) >= 0) {
      empty->owned = true;
      ++fitted;
    }
  }
  return fitted;
}

void add_all_items(QuestSave &quest, const CheatTables &cheats, const PartTables &parts,
                   const GarageTables &garage, const int track_count) {
  Garage &owned = quest.garage;
  owned.stock_size = std::max(owned.stock_size, cheats.stock_size);
  const auto add = [&](const PartKind kind, const std::vector<SamePart> &groups, const int copies) {
    for (const SamePart &group : groups) {
      top_up(owned, parts, garage, kind, group, copies);
    }
  };
  const auto model = [](const int value) { return value >= 0 ? std::to_string(value) : ""; };
  const auto sold = [](const auto &record) { return record.price.base > 0.0F; };
  const auto weapon = [&](const WeaponRecord &record) { return model(record.model); };
  const auto body = [&](const ArmourRecord &record) { return model(record.maker); };
  const auto legs = [&](const ArmourRecord &record) { return model(record.model); };
  const auto name = [](const auto &record) { return record.name; };
  const auto module_sold = [](const ModuleRecord &record) { return record.price > 0; };
  add(PartKind::body, same_parts(parts.bodies, body, sold), cheats.part_copies);
  add(PartKind::legs, same_parts(parts.legs, legs, sold), cheats.part_copies);
  add(PartKind::arm, same_parts(parts.arm_weapons, weapon, sold), cheats.weapon_copies);
  add(PartKind::shoulder, same_parts(parts.shoulder_weapons, weapon, sold), cheats.weapon_copies);
  add(PartKind::chip, same_parts(parts.chips, name, sold), cheats.part_copies);
  add(PartKind::module, same_parts(garage.modules, name, module_sold), cheats.part_copies);
  own_all(owned.paints, garage.paints.size());
  own_all(owned.emblems, garage.emblems.size());
  for (int track = 0; track < track_count; ++track) {
    quest.music.owned.try_emplace(track, TrackState::fresh);
  }
}

void go_to_day(QuestSave &quest, const int day, const SleepScript &sleep) {
  if (sleep.days_in_cycle <= 0) {
    return;
  }
  const int target = ((day % sleep.days_in_cycle) + sleep.days_in_cycle) % sleep.days_in_cycle;
  for (int night = 0; night < sleep.days_in_cycle && quest.day != target; ++night) {
    sleep_one_night(quest, sleep);
  }
}

} // namespace phantomcrash
