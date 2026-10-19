#include "phantomcrash/vs_scoobee.hpp"

#include <algorithm>
#include <array>

namespace phantomcrash {
namespace {

using json::JsonValue;

JsonValue number(const int value) {
  return JsonValue{static_cast<std::int64_t>(value)};
}

int integer_or(const JsonValue &value, const char *key, const int fallback) {
  const JsonValue *found = value.find(key);
  return found != nullptr && found->is_number() ? static_cast<int>(found->number()) : fallback;
}

std::string text_or(const JsonValue &value, const char *key) {
  const JsonValue *found = value.find(key);
  return found != nullptr && found->is_string() ? found->string() : std::string{};
}

VsScoobee scoobee_from_json(const JsonValue &value) {
  VsScoobee out;
  out.name = text_or(value, "name");
  out.chip_name = text_or(value, "chip_name");
  out.saved = text_or(value, "saved");
  out.robot.name = out.name;
  if (const JsonValue *parts = value.find("parts"); parts != nullptr && parts->is_object()) {
    for (std::size_t slot = 0; slot < part_slot_names.size(); ++slot) {
      const JsonValue *part = find_part_slot(*parts, slot);
      if (part != nullptr && part->is_object()) {
        out.robot.parts[slot] =
            RobotPart{integer_or(*part, "id", 0), integer_or(*part, "level", 0)};
      }
    }
  }
  if (const JsonValue *modules = value.find("modules"); modules != nullptr && modules->is_array()) {
    for (const JsonValue &module : modules->array()) {
      if (module.is_number()) {
        out.robot.modules.push_back(static_cast<int>(module.number()));
      }
    }
  }
  out.robot.paint = integer_or(value, "paint", 0);
  out.robot.emblem = integer_or(value, "emblem", emblem_none);
  return out;
}

} // namespace

VsScoobee vs_scoobee_from_hangar(const Garage &garage, const int hangar, const std::string &saved) {
  VsScoobee out;
  out.saved = saved;
  if (hangar < 0 || hangar >= garage_hangar_count) {
    return out;
  }
  const Hangar &bay = garage.hangars[static_cast<std::size_t>(hangar)];
  const auto item = [&garage](const int stock) -> const StockItem * {
    const auto found = garage.stock.find(stock);
    return found != garage.stock.end() ? &found->second : nullptr;
  };
  for (std::size_t slot = 0; slot < bay.parts.size(); ++slot) {
    if (const StockItem *part = item(bay.parts[slot])) {
      out.robot.parts[slot] = RobotPart{part->id, part->level};
    }
  }
  if (const StockItem *chip = item(bay.part(PartSlot::chip))) {
    out.chip_name = chip->name;
  }
  for (const int stock : bay.modules) {
    if (const StockItem *module = item(stock)) {
      out.robot.modules.push_back(module->id);
    }
  }
  out.name = bay.name;
  out.robot.name = bay.name;
  out.robot.paint = bay.paint;
  out.robot.emblem = bay.emblem;
  return out;
}

bool vs_scoobee_damaged(const VsScoobee &scoobee, const PartTables &parts) {
  return vs_scoobee_maker(scoobee, parts) < 0;
}

int vs_scoobee_maker(const VsScoobee &scoobee, const PartTables &parts) {
  const auto &body = scoobee.robot.part(PartSlot::body);
  if (!body || body->id < 0 || body->id >= static_cast<int>(parts.bodies.size())) {
    return -1;
  }
  return parts.bodies[static_cast<std::size_t>(body->id)].maker;
}

std::vector<VsScoobee> vs_scoobees_from_json(const JsonValue &value) {
  std::vector<VsScoobee> out;
  if (value.is_array()) {
    for (const JsonValue &entry : value.array()) {
      if (entry.is_object()) {
        out.push_back(scoobee_from_json(entry));
      }
    }
  }
  return out;
}

JsonValue vs_scoobees_to_json(const std::vector<VsScoobee> &scoobees) {
  JsonValue::Array out;
  for (const VsScoobee &scoobee : scoobees) {
    JsonValue::Object parts;
    for (std::size_t slot = 0; slot < part_slot_names.size(); ++slot) {
      if (const auto &part = scoobee.robot.parts[slot]) {
        parts.emplace_back(
            part_slot_names[slot],
            JsonValue{JsonValue::Object{{"id", number(part->id)}, {"level", number(part->level)}}});
      }
    }
    JsonValue::Array modules;
    for (const int module : scoobee.robot.modules) {
      modules.push_back(number(module));
    }
    out.push_back(JsonValue{JsonValue::Object{
        {"name", JsonValue{scoobee.name}},
        {"chip_name", JsonValue{scoobee.chip_name}},
        {"saved", JsonValue{scoobee.saved}},
        {"parts", JsonValue{std::move(parts)}},
        {"modules", JsonValue{std::move(modules)}},
        {"paint", number(scoobee.robot.paint)},
        {"emblem", number(scoobee.robot.emblem)},
    }});
  }
  return JsonValue{std::move(out)};
}

} // namespace phantomcrash
