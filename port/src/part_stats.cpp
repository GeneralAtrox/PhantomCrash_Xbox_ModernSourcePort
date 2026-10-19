#include "phantomcrash/part_stats.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace phantomcrash {
namespace {

constexpr std::array<std::string_view, goods_kind_count> kind_names{
    "body", "legs", "arm", "shoulder", "chip", "module", "paint", "emblem"};

constexpr std::array<std::string_view, stat_row_count> row_names{
    "level",  "weight",         "capacity",    "armour",      "sockets",
    "speed",  "jump",           "ammo",        "rounds",      "recharge",
    "damage", "module_sockets", "active_time", "charge_time", "laser_reduction"};

template <typename T> const T *at(const std::vector<T> &records, const int index) {
  if (index < 0 || index >= static_cast<int>(records.size())) {
    return nullptr;
  }
  return &records[static_cast<std::size_t>(index)];
}

const ArmourRecord *armour_of(const PartTables &parts, const Goods &goods) {
  if (goods.kind == GoodsKind::body) {
    return at(parts.bodies, goods.id);
  }
  return goods.kind == GoodsKind::legs ? at(parts.legs, goods.id) : nullptr;
}

const WeaponRecord *weapon_of(const PartTables &parts, const Goods &goods) {
  if (goods.kind == GoodsKind::arm) {
    return at(parts.arm_weapons, goods.id);
  }
  return goods.kind == GoodsKind::shoulder ? at(parts.shoulder_weapons, goods.id) : nullptr;
}

std::optional<StatRow> row_named(const std::string_view name) {
  for (std::size_t row = 0; row < row_names.size(); ++row) {
    if (row_names[row] == name) {
      return static_cast<StatRow>(row);
    }
  }
  return std::nullopt;
}

// A weapon's value is never below the record's minimum (0x03c652, 0x03c683).
float at_least(const LevelCurve &curve, const int minimum, const int level) {
  return std::max(std::trunc(curve.value(level)), static_cast<float>(minimum));
}

// The value a row shows, or nothing when the goods have no such line.
std::optional<double> row_value(const PartStatsTable &table, const PartTables &parts,
                                const GarageTables &garage, const Goods &goods, const StatRow row) {
  const ArmourRecord *armour = armour_of(parts, goods);
  const WeaponRecord *weapon = weapon_of(parts, goods);
  const ModuleRecord *module =
      goods.kind == GoodsKind::module ? at(garage.modules, goods.id) : nullptr;
  const ChipRecord *chip = goods.kind == GoodsKind::chip ? parts.chip(goods.id) : nullptr;
  const int level = goods.level;
  switch (row) {
  case StatRow::level:
    return std::abs(level);
  case StatRow::weight:
    if (armour != nullptr) {
      return armour->weight.value(level) * table.hundredths;
    }
    if (weapon != nullptr) {
      return weapon->weight.value(level) * table.hundredths;
    }
    if (chip != nullptr) {
      return chip->weight.value(level) * table.hundredths;
    }
    return module != nullptr ? std::optional<double>(module->weight * table.hundredths)
                             : std::nullopt;
  case StatRow::capacity:
    return armour != nullptr
               ? std::optional<double>(armour->capacity.value(level) * table.hundredths)
               : std::nullopt;
  case StatRow::armour:
    if (armour != nullptr) {
      return std::max(armour->armour.value(level), armour->player_floor); // 0x03c5e2
    }
    return module != nullptr ? std::optional<double>(module->armour) : std::nullopt;
  case StatRow::sockets:
    return armour != nullptr ? std::optional<double>(std::trunc(armour->sockets.value(level)))
                             : std::nullopt;
  case StatRow::speed:
    return armour != nullptr ? std::optional<double>(armour->speed.value(level)) : std::nullopt;
  case StatRow::jump:
    return armour != nullptr ? std::optional<double>(armour->jump.value(level)) : std::nullopt;
  case StatRow::ammo:
    return weapon != nullptr
               ? std::optional<double>(at_least(weapon->ammo, weapon->ammo_min, level))
               : std::nullopt;
  case StatRow::rounds:
    return weapon != nullptr
               ? std::optional<double>(at_least(weapon->rounds, weapon->rounds_min, level))
               : std::nullopt;
  case StatRow::recharge:
    return weapon != nullptr
               ? std::optional<double>(std::trunc(weapon->recharge_frames.value(level)) /
                                       table.battle_rate)
               : std::nullopt;
  case StatRow::damage:
    return weapon != nullptr
               ? std::optional<double>(at_least(weapon->damage, weapon->damage_min, level))
               : std::nullopt;
  case StatRow::module_sockets:
    return module != nullptr ? std::optional<double>(module->sockets) : std::nullopt;
  case StatRow::active_time:
  case StatRow::charge_time:
    if (module == nullptr || module->laser_reduction > 0) {
      return std::nullopt;
    }
    return (row == StatRow::active_time ? module->active_frames : module->charge_frames) /
           table.display_rate;
  case StatRow::laser_reduction:
    if (module == nullptr || module->laser_reduction <= 0) {
      return std::nullopt;
    }
    return module->laser_reduction;
  }
  return std::nullopt;
}

} // namespace

std::optional<PartKind> stock_kind(const GoodsKind kind) {
  if (kind == GoodsKind::paint || kind == GoodsKind::emblem) {
    return std::nullopt;
  }
  return static_cast<PartKind>(kind);
}

std::optional<GoodsKind> goods_kind_named(const std::string_view name) {
  for (std::size_t kind = 0; kind < kind_names.size(); ++kind) {
    if (kind_names[kind] == name) {
      return static_cast<GoodsKind>(kind);
    }
  }
  return std::nullopt;
}

int goods_maker(const PartTables &parts, const Goods &goods) {
  if (const ArmourRecord *armour = armour_of(parts, goods)) {
    return armour->maker;
  }
  if (const WeaponRecord *weapon = weapon_of(parts, goods)) {
    return weapon->maker;
  }
  return no_maker;
}

std::string goods_name(const PartTables &parts, const GarageTables &garage, const Goods &goods) {
  if (const auto kind = stock_kind(goods.kind)) {
    return part_name(parts, garage, *kind, goods.id);
  }
  const PriceEntry *entry =
      at(goods.kind == GoodsKind::paint ? garage.paints : garage.emblems, goods.id);
  return entry != nullptr ? entry->name : std::string{};
}

std::int64_t goods_price(const PartTables &parts, const GarageTables &garage, const Goods &goods) {
  if (const auto kind = stock_kind(goods.kind)) {
    return part_price(parts, garage, *kind, goods.id, goods.level);
  }
  const PriceEntry *entry =
      at(goods.kind == GoodsKind::paint ? garage.paints : garage.emblems, goods.id);
  return entry != nullptr ? entry->price : 0;
}

bool reduces_lasers(const GarageTables &garage, const Goods &goods) {
  const ModuleRecord *module =
      goods.kind == GoodsKind::module ? at(garage.modules, goods.id) : nullptr;
  return module != nullptr && module->laser_reduction > 0;
}

PartStatsTable parse_part_stats(const json::JsonValue &document) {
  try {
    PartStatsTable table;
    for (const auto &[name, rows] : json::required(document, "rows").object()) {
      const auto kind = goods_kind_named(name);
      if (!kind) {
        throw std::runtime_error("rows: no kind " + name);
      }
      for (const json::JsonValue &row : rows.array()) {
        const auto parsed = row_named(row.string());
        if (!parsed) {
          throw std::runtime_error("rows." + name + ": no row " + row.string());
        }
        table.rows[static_cast<std::size_t>(*kind)].push_back(*parsed);
      }
    }
    for (const auto &[name, decimals] : json::required(document, "decimals").object()) {
      const auto row = row_named(name);
      if (!row) {
        throw std::runtime_error("decimals: no row " + name);
      }
      table.decimals[static_cast<std::size_t>(*row)] =
          std::clamp(static_cast<int>(decimals.number()), 0, 6);
    }
    table.hundredths = static_cast<float>(document.number_or("hundredths", table.hundredths));
    table.battle_rate = static_cast<float>(document.number_or("battle_rate", table.battle_rate));
    table.display_rate = static_cast<float>(document.number_or("display_rate", table.display_rate));
    if (table.battle_rate <= 0.0F || table.display_rate <= 0.0F) {
      throw std::runtime_error("the rates must be above 0");
    }
    if (const json::JsonValue *full = document.find("full_price")) {
      for (const json::JsonValue &module : full->array()) {
        table.full_price.push_back(static_cast<int>(module.number()));
      }
    }
    table.extras = parse_extra_stats(document);
    table.details = parse_extra_stats(document, "details");
    return table;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("part_stats.json: ") + error.what());
  }
}

std::vector<StatLine> part_stats(const PartStatsTable &table, const PartTables &parts,
                                 const GarageTables &garage, const Goods &goods) {
  std::vector<StatLine> lines;
  for (const StatRow row : table.rows[static_cast<std::size_t>(goods.kind)]) {
    if (const auto value = row_value(table, parts, garage, goods, row)) {
      lines.push_back({row, *value, table.decimals[static_cast<std::size_t>(row)]});
    }
  }
  return lines;
}

std::int64_t buy_back_price(const PartStatsTable &table, const PartTables &parts,
                            const GarageTables &garage, const Goods &goods, const double rate) {
  const std::int64_t price = goods_price(parts, garage, goods);
  const bool whole = goods.kind == GoodsKind::module &&
                     std::find(table.full_price.begin(), table.full_price.end(), goods.id) !=
                         table.full_price.end();
  return whole ? price : static_cast<std::int64_t>(static_cast<double>(price) * rate);
}

PartStatsStyle parse_part_stats_style(const json::JsonValue &value) {
  PartStatsStyle style;
  style.right = static_cast<float>(json::required(value, "right").number());
  style.step = static_cast<float>(json::required(value, "step").number());
  style.layer = static_cast<int>(json::required(value, "layer").number());
  style.colour = parse_colour(json::required(value, "colour").string());
  style.sign_offset = static_cast<float>(value.number_or("sign_offset", 0.0));
  const auto signs = json::member(value, "signs", parse_sprites);
  if (signs.size() != style.signs.size()) {
    throw std::runtime_error("signs: needs LIGHT, NORMAL and HEAVY");
  }
  std::copy(signs.begin(), signs.end(), style.signs.begin());
  return style;
}

std::string stat_text(const StatLine &line) {
  char text[32];
  if (line.decimals == 0) {
    std::snprintf(text, sizeof text, "%lld", static_cast<long long>(std::trunc(line.value)));
  } else {
    std::snprintf(text, sizeof text, "%.*f", line.decimals, line.value);
  }
  return text;
}

} // namespace phantomcrash
