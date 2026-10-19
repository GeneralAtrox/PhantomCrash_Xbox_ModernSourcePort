// The stats a shop's panel has no printed line for (part_stats.hpp).

#include "phantomcrash/part_stats.hpp"

#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

template <typename T> const T *record_at(const std::vector<T> &records, const int index) {
  return index >= 0 && index < static_cast<int>(records.size())
             ? &records[static_cast<std::size_t>(index)]
             : nullptr;
}

// A weapon's `source` as the battle has it from the record at its level.
std::optional<double> weapon_value(const PartTables &parts, const Goods &goods,
                                   const ExtraStatSource source) {
  const bool shoulder = goods.kind == GoodsKind::shoulder;
  const PartRef ref{shoulder ? WeaponTable::shoulder : WeaponTable::arm, goods.id, goods.level};
  const WeaponRecord *record = parts.weapon(ref);
  if (record == nullptr) {
    return std::nullopt;
  }
  const WeaponStats stats = weapon_stats(*record, goods.level);
  WeaponTuning weapon;
  weapon.part = ref;
  apply_weapon_stats(weapon, stats);
  const bool melee = weapon.hud_kind == hud_kind::melee;
  const bool beam = weapon.hud_kind == hud_kind::beam;
  switch (source) {
  case ExtraStatSource::fire_rate:
    // A beam is held, not fired shot by shot.
    if (beam || weapon.cooldown <= 0.0F) {
      return std::nullopt;
    }
    return 1.0 / weapon.cooldown;
  case ExtraStatSource::range:
    return weapon.range;
  case ExtraStatSource::shot_speed:
    if (melee || (stats.top_speed <= 0.0F && !beam)) {
      return std::nullopt;
    }
    return weapon.speed;
  case ExtraStatSource::spread:
    return weapon.rounds > 1 ? weapon.spread_degrees : 0.0;
  case ExtraStatSource::homing:
    return weapon.turn_degrees_per_second;
  case ExtraStatSource::blast_radius:
    return weapon.blast_radius;
  case ExtraStatSource::blast_damage:
    return weapon.blast_radius > 0.0F ? weapon.blast_damage : 0.0;
  case ExtraStatSource::stun:
    return weapon.stagger_seconds;
  case ExtraStatSource::knockback:
    return weapon.knockback;
  case ExtraStatSource::knockdown:
    return weapon.knockdown_percent;
  default:
    return std::nullopt;
  }
}

// The goods' value of `source`; nullopt where the kind has no such stat.
std::optional<double> extra_value(const PartTables &parts, const GarageTables &garage,
                                  const Goods &goods, const ExtraStatSource source) {
  switch (goods.kind) {
  case GoodsKind::module:
    if (const ModuleRecord *module = record_at(garage.modules, goods.id)) {
      if (source == ExtraStatSource::critical_percent) {
        return module->critical_percent;
      }
      if (source == ExtraStatSource::knockdown_resistance) {
        return module->knockdown_resistance;
      }
    }
    return std::nullopt;
  case GoodsKind::chip:
    if (const ChipRecord *chip = parts.chip(goods.id)) {
      switch (source) {
      case ExtraStatSource::critical_percent:
        return chip->critical_percent.value(goods.level);
      case ExtraStatSource::dodge_percent:
        return chip->dodge_percent.value(goods.level);
      case ExtraStatSource::knockdown_resistance:
        return chip->knockdown_resistance.value(goods.level);
      default:
        return std::nullopt;
      }
    }
    return std::nullopt;
  case GoodsKind::legs:
    if (const ArmourRecord *legs = record_at(parts.legs, goods.id);
        legs != nullptr && source == ExtraStatSource::knockback_resistance) {
      return legs->knockback_resistance.value(goods.level);
    }
    return std::nullopt;
  case GoodsKind::arm:
  case GoodsKind::shoulder:
    return weapon_value(parts, goods, source);
  default:
    return std::nullopt;
  }
}

// The type weapon_types.json gives the goods' record; empty for none.
std::string weapon_type(const PartStatsTable &table, const Goods &goods) {
  const std::vector<std::string> *types = nullptr;
  if (goods.kind == GoodsKind::arm) {
    types = &table.weapon_types.arm;
  } else if (goods.kind == GoodsKind::shoulder) {
    types = &table.weapon_types.shoulder;
  }
  const std::string *type = types != nullptr ? record_at(*types, goods.id) : nullptr;
  return type != nullptr ? *type : std::string{};
}

std::optional<ExtraStatSource> source_named(const std::string &name) {
  static const std::map<std::string, ExtraStatSource> sources{
      {"critical_percent", ExtraStatSource::critical_percent},
      {"dodge_percent", ExtraStatSource::dodge_percent},
      {"knockdown_resistance", ExtraStatSource::knockdown_resistance},
      {"knockback_resistance", ExtraStatSource::knockback_resistance},
      {"weapon_type", ExtraStatSource::weapon_type},
      {"fire_rate", ExtraStatSource::fire_rate},
      {"range", ExtraStatSource::range},
      {"shot_speed", ExtraStatSource::shot_speed},
      {"spread", ExtraStatSource::spread},
      {"homing", ExtraStatSource::homing},
      {"blast_radius", ExtraStatSource::blast_radius},
      {"blast_damage", ExtraStatSource::blast_damage},
      {"stun", ExtraStatSource::stun},
      {"knockback", ExtraStatSource::knockback},
      {"knockdown", ExtraStatSource::knockdown},
  };
  const auto found = sources.find(name);
  return found != sources.end() ? std::optional{found->second} : std::nullopt;
}

// The lines of `stats` for the goods, leaving out those at 0.
std::vector<ExtraStatLine> stat_lines(const std::vector<ExtraStat> &stats,
                                      const PartStatsTable &table, const PartTables &parts,
                                      const GarageTables &garage, const Goods &goods) {
  std::vector<ExtraStatLine> lines;
  for (const ExtraStat &stat : stats) {
    if (stat.source == ExtraStatSource::weapon_type) {
      if (std::string type = weapon_type(table, goods); !type.empty()) {
        lines.push_back({stat.label, stat.unit, std::move(type), true});
      }
      continue;
    }
    const auto value = extra_value(parts, garage, goods, stat.source);
    if (!value) {
      continue;
    }
    // Only what the goods change: a 0 shows nothing.
    const double scale = std::pow(10.0, stat.decimals);
    if (std::abs(std::trunc(*value * scale)) < 1.0) {
      continue;
    }
    lines.push_back({stat.label, stat.unit, stat_text({StatRow::level, *value, stat.decimals})});
  }
  return lines;
}

} // namespace

std::array<std::vector<ExtraStat>, goods_kind_count>
parse_extra_stats(const json::JsonValue &document, const char *key) {
  std::array<std::vector<ExtraStat>, goods_kind_count> extras;
  const json::JsonValue *table = document.find(key);
  if (table == nullptr) {
    return extras;
  }
  for (const auto &[name, list] : table->object()) {
    const auto kind = goods_kind_named(name);
    if (!kind) {
      throw std::runtime_error(std::string(key) + ": no kind " + name);
    }
    for (const json::JsonValue &entry : list.array()) {
      ExtraStat stat;
      const std::string source = json::required(entry, "stat").string();
      const auto parsed = source_named(source);
      if (!parsed) {
        throw std::runtime_error(std::string(key) + "." + name + ": no stat " + source);
      }
      stat.source = *parsed;
      stat.label = json::required(entry, "label").string();
      stat.unit = entry.string_or("unit", "");
      stat.decimals = std::clamp(static_cast<int>(entry.number_or("decimals", 0)), 0, 6);
      extras[static_cast<std::size_t>(*kind)].push_back(std::move(stat));
    }
  }
  return extras;
}

std::vector<ExtraStatLine> extra_stats(const PartStatsTable &table, const PartTables &parts,
                                       const GarageTables &garage, const Goods &goods) {
  return stat_lines(table.extras[static_cast<std::size_t>(goods.kind)], table, parts, garage,
                    goods);
}

std::vector<ExtraStatLine> detail_stats(const PartStatsTable &table, const PartTables &parts,
                                        const GarageTables &garage, const Goods &goods) {
  return stat_lines(table.details[static_cast<std::size_t>(goods.kind)], table, parts, garage,
                    goods);
}

WeaponTypes parse_weapon_types(const json::JsonValue &document) {
  try {
    const auto list = [&document](const char *key) {
      std::vector<std::string> out;
      if (const json::JsonValue *records = document.find(key)) {
        for (const json::JsonValue &record : records->array()) {
          out.push_back(json::required(record, "type").string());
        }
      }
      return out;
    };
    return {list("arm"), list("shoulder")};
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("weapon_types.json: ") + error.what());
  }
}

PartStatsTable load_part_stats(const DataRoots &roots) {
  PartStatsTable table = parse_part_stats(roots.document("ui/part_stats.json"));
  if (roots.has_document("ui/weapon_types.json")) {
    table.weapon_types = parse_weapon_types(roots.document("ui/weapon_types.json"));
  }
  return table;
}

} // namespace phantomcrash
