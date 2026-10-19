#include "phantomcrash/added_chips.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace phantomcrash {
namespace {

using json::JsonValue;

constexpr double face_size = 64.0; // a face picture is one 64x64 cell, as the original's

JsonValue &member(JsonValue &object, const char *key) {
  for (auto &[name, value] : std::get<JsonValue::Object>(object.value)) {
    if (name == key) {
      return value;
    }
  }
  throw std::runtime_error(std::string("missing \"") + key + "\"");
}

// Sets `key` in `object`, adding it when the object lacks it.
void set(JsonValue &object, const std::string &key, const JsonValue &value) {
  auto &members = std::get<JsonValue::Object>(object.value);
  for (auto &[name, field] : members) {
    if (name == key) {
      field = value;
      return;
    }
  }
  members.emplace_back(key, value);
}

JsonValue number(const double value) {
  return JsonValue{value};
}

// A family's "face": a picture of its own, or {image, rect} naming a cell of
// another (a boss's portrait in the original's atlas).
JsonValue face(const JsonValue &given) {
  if (given.is_object()) {
    return given;
  }
  const std::string &image = given.string();
  JsonValue::Array rect{number(0.0), number(0.0), number(face_size), number(face_size)};
  return JsonValue{
      JsonValue::Object{{"image", JsonValue{image}}, {"rect", JsonValue{std::move(rect)}}}};
}

// Calls visit(family, personality, index) for each added record in order.
template <typename Visit> void each_record(const JsonValue &added, Visit visit) {
  int index = static_cast<int>(json::required(added, "first_index").number());
  for (const auto &family : json::required(added, "families").array()) {
    for (const auto &chatter : json::required(family, "chatter").array()) {
      visit(family, static_cast<int>(chatter.number()), index++);
    }
  }
}

int record_index(const JsonValue &added, const std::string &name, const int chatter) {
  int found = -1;
  each_record(added, [&](const JsonValue &family, const int personality, const int index) {
    if (personality == chatter && json::required(family, "name").string() == name) {
      found = index;
    }
  });
  if (found < 0) {
    throw std::runtime_error("no added CHIP " + name + " with chatter " + std::to_string(chatter));
  }
  return found;
}

// The port's retunes of original CHIPs ("retune"): each listed stat of every
// record whose name is in the entry's "names" takes the entry's value.
void retune(JsonValue::Array &chips, const JsonValue &added) {
  const JsonValue *entries = added.find("retune");
  if (entries == nullptr) {
    return;
  }
  for (const auto &entry : entries->array()) {
    const auto &names = json::required(entry, "names").array();
    for (auto &chip : chips) {
      const std::string &name = json::required(chip, "name").string();
      if (std::none_of(names.begin(), names.end(),
                       [&](const JsonValue &listed) { return listed.string() == name; })) {
        continue;
      }
      for (const auto &[key, value] : json::required(entry, "stats").object()) {
        set(chip, key, value);
      }
    }
  }
}

} // namespace

bool add_chips(JsonValue &parts, const JsonValue &added) {
  auto &chips = std::get<JsonValue::Array>(member(parts, "chips").value);
  if (static_cast<double>(chips.size()) != json::required(added, "first_index").number()) {
    return false;
  }
  retune(chips, added);
  each_record(added, [&](const JsonValue &family, const int chatter, const int index) {
    JsonValue::Object record{{"index", number(index)},
                             {"name", json::required(family, "name")},
                             {"chatter", number(chatter)},
                             {"face", face(json::required(family, "face"))}};
    for (const auto &[key, value] : json::required(family, "stats").object()) {
      record.emplace_back(key, value);
    }
    chips.push_back(JsonValue{std::move(record)});
  });
  return true;
}

void add_chip_lineups(JsonValue &garage, const JsonValue &added) {
  auto &days = std::get<JsonValue::Array>(member(garage, "chip_lineups").value);
  for (const auto &offer : json::required(added, "shop").array()) {
    const auto day = static_cast<std::size_t>(json::required(offer, "day").number());
    if (day >= days.size()) {
      throw std::runtime_error("added CHIP offer on day " + std::to_string(day) +
                               ", past the last shop day");
    }
    const int id = record_index(added, json::required(offer, "chip").string(),
                                static_cast<int>(json::required(offer, "chatter").number()));
    std::get<JsonValue::Array>(days[day].value)
        .push_back(JsonValue{
            JsonValue::Object{{"id", number(id)}, {"level", number(offer.number_or("level", 0))}}});
  }
  const JsonValue *bosses = added.find("boss_shop");
  if (bosses == nullptr) {
    return;
  }
  JsonValue::Array offers;
  for (const auto &offer : bosses->array()) {
    const int id = record_index(added, json::required(offer, "chip").string(),
                                static_cast<int>(json::required(offer, "chatter").number()));
    offers.push_back(JsonValue{JsonValue::Object{{"id", number(id)},
                                                 {"level", number(offer.number_or("level", 0))},
                                                 {"contests", json::required(offer, "contests")}}});
  }
  set(garage, "boss_chips", JsonValue{std::move(offers)});
}

std::optional<JsonValue> read_converted_table(const DataRoots &roots,
                                              const std::string_view relative) {
  const auto path = roots.find_asset(relative);
  if (!path) {
    return std::nullopt;
  }
  auto table = json::read_file(*path);
  const bool parts = relative == "tables/parts.json";
  const bool garage = relative == "tables/garage.json";
  if ((parts || garage) && !roots.slai() && roots.has_document(added_chips_document)) {
    const auto added = roots.document(added_chips_document);
    // The shop offers only follow when the records joined the table.
    if (parts) {
      add_chips(table, added);
    } else if (const auto chips = roots.find_asset("tables/parts.json");
               chips &&
               json::required(json::read_file(*chips), "chips").array().size() ==
                   static_cast<std::size_t>(json::required(added, "first_index").number())) {
      add_chip_lineups(table, added);
    }
  }
  return table;
}

} // namespace phantomcrash
