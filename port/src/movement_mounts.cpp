#include "phantomcrash/movement_mounts.hpp"
#include <stdexcept>

namespace phantomcrash {
namespace {
const json::JsonValue &field(const json::JsonValue &value, const char *key) {
  const auto *found = value.find(key);
  if (!found) {
    throw std::runtime_error(std::string("Missing movement mount field: ") + key);
  }
  return *found;
}
Vec3 vector(const json::JsonValue &value) {
  const auto &a = value.array();
  if (a.size() != 3) {
    throw std::runtime_error("Movement mount needs three coordinates");
  }
  return {static_cast<float>(a[0].number()), static_cast<float>(a[1].number()),
          static_cast<float>(a[2].number())};
}
} // namespace

MovementMounts parse_movement_mounts(const json::JsonValue &value) {
  MovementMounts out;
  for (const auto &leg : field(value, "legs").array()) {
    const auto &slots = field(leg, "slots").array();
    if (slots.size() != movement_slots) {
      throw std::runtime_error("Movement mounts need 14 slots per leg type");
    }
    auto &points = out.legs.emplace_back();
    for (std::size_t i = 0; i < slots.size(); ++i) {
      points[i] = {static_cast<int>(slots[i].number_or("kind", -1)),
                   vector(field(slots[i], "position")), vector(field(slots[i], "direction"))};
    }
  }
  const auto &groups = field(value, "groups");
  out.groups.jump = static_cast<int>(groups.number_or("jump", -1));
  out.groups.descent = static_cast<int>(groups.number_or("descent", -1));
  out.groups.dodge = static_cast<int>(groups.number_or("dodge", -1));
  out.groups.dash_stop = static_cast<int>(groups.number_or("dash_stop", -1));
  out.groups.takeoff = static_cast<int>(groups.number_or("takeoff", -1));
  out.groups.takeoff_seconds = static_cast<float>(groups.number_or("takeoff_seconds", 0));
  for (const auto &row : field(groups, "contact").array()) {
    if (row.array().size() != 3) {
      throw std::runtime_error("Contact effects need three material classes");
    }
    auto &entry = out.groups.contact.emplace_back();
    for (std::size_t i = 0; i < entry.size(); ++i) {
      entry[i] = static_cast<int>(row.array()[i].number());
    }
  }
  const auto &dash = field(groups, "dash").array();
  if (dash.size() != out.groups.dash.size()) {
    throw std::runtime_error("Movement dash needs two effect groups");
  }
  for (std::size_t i = 0; i < dash.size(); ++i) {
    out.groups.dash[i] = static_cast<int>(dash[i].number());
  }
  return out;
}
} // namespace phantomcrash
