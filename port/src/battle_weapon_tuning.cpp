// Weapon tunings from data, and the magazines that count their ammunition.
#include "phantomcrash/battle_weapons.hpp"

#include <algorithm>
#include <stdexcept>

namespace phantomcrash {
namespace {

float number(const json::JsonValue &value, const char *key, const float fallback) {
  return static_cast<float>(value.number_or(key, fallback));
}

WeaponKind weapon_kind(const std::string &name) {
  if (name == "gun") {
    return WeaponKind::gun;
  }
  if (name == "melee") {
    return WeaponKind::melee;
  }
  if (name == "missile") {
    return WeaponKind::missile;
  }
  if (name == "grenade") {
    return WeaponKind::grenade;
  }
  throw std::runtime_error("unknown weapon kind \"" + name + "\"");
}

// The HUD kind of a weapon without a record.
int default_hud_kind(const WeaponKind kind) {
  switch (kind) {
  case WeaponKind::gun:
    return hud_kind::cannon;
  case WeaponKind::melee:
    return hud_kind::melee;
  case WeaponKind::missile:
    return hud_kind::missile;
  case WeaponKind::grenade:
    return hud_kind::grenade;
  }
  return hud_kind::cannon;
}

} // namespace

std::map<std::string, WeaponTuning> parse_weapon_tunings(const json::JsonValue &value) {
  std::map<std::string, WeaponTuning> out;
  for (const auto &[name, entry] : value.object()) {
    WeaponTuning w;
    w.name = name;
    w.label = entry.string_or("label", name);
    w.kind = weapon_kind(entry.string_or("kind", "gun"));
    w.aim_mode = static_cast<int>(entry.number_or("aim_mode", w.aim_mode));
    w.damage = number(entry, "damage", w.damage);
    w.cooldown = number(entry, "cooldown", w.cooldown);
    w.rounds = std::max(static_cast<int>(entry.number_or("rounds", w.rounds)), 1);
    w.spread_degrees = number(entry, "spread_degrees", 0.0F);
    w.speed = number(entry, "speed", w.speed);
    w.start_speed = number(entry, "start_speed", 0.0F);
    w.acceleration = number(entry, "acceleration", 0.0F);
    w.range = number(entry, "range", w.range);
    w.life_seconds = number(entry, "life_seconds", 0.0F);
    w.lock_range = number(entry, "lock_range", w.lock_range);
    w.turn_degrees_per_second = number(entry, "turn_degrees_per_second", 0.0F);
    w.gravity = number(entry, "gravity", 0.0F);
    w.launch_degrees = number(entry, "launch_degrees", 0.0F);
    w.blast_radius = number(entry, "blast_radius", 0.0F);
    w.blast_start_radius = number(entry, "blast_start_radius", 0.0F);
    w.blast_seconds = number(entry, "blast_seconds", 0.0F);
    w.blast_damage = number(entry, "blast_damage", 0.0F);
    w.stagger_seconds = number(entry, "stagger_seconds", w.stagger_seconds);
    w.knockback = number(entry, "knockback", 0.0F);
    w.knockdown_percent = number(entry, "knockdown_percent", 0.0F);
    w.recharge_seconds = number(entry, "recharge_seconds", 0.0F);
    w.hud_kind = static_cast<int>(entry.number_or("hud_kind", default_hud_kind(w.kind)));
    w.max_ammo = static_cast<int>(entry.number_or("ammo", 0.0));
    w.homing = entry.boolean_or("homing", w.turn_degrees_per_second > 0.0F);
    if (const json::JsonValue *part = entry.find("part")) {
      w.part = parse_part_ref(*part);
    }
    out.emplace(name, std::move(w));
  }
  return out;
}

void Magazine::reset(const int maximum, const float recharge) {
  max = std::max(maximum, 0);
  ammo = max;
  shown = static_cast<float>(max);
  refill_left = 0.0F;
  dry_seconds = 0.0F;
  recharge_seconds = std::max(recharge, 0.0F);
  charge = 0.0F;
}

void Magazine::take() {
  if (!limited() || ammo <= 0) {
    return;
  }
  --ammo;
  if (refilling()) {
    refill_from = std::max(refill_from - 1.0F, 0.0F);
  } else {
    shown = static_cast<float>(ammo);
  }
}

void Magazine::refill(const int amount) {
  if (!limited() || amount <= 0) {
    return;
  }
  refill_from = shown;
  ammo = std::min(ammo + amount, max);
  refill_left = refill_count_seconds;
  dry_seconds = 0.0F;
}

void Magazine::tick(const float seconds, const bool resting) {
  dry_seconds = empty() ? dry_seconds + seconds : 0.0F;
  if (limited() && recharging() && !refilling()) {
    if (!resting) {
      shown = static_cast<float>(ammo) + charge;
      return;
    }
    if (ammo >= max) {
      charge = 0.0F;
    } else {
      charge += static_cast<float>(max) * seconds / recharge_seconds;
      const int whole = std::min(static_cast<int>(charge), max - ammo);
      ammo += whole;
      charge = ammo >= max ? 0.0F : charge - static_cast<float>(whole);
    }
    shown = static_cast<float>(ammo) + charge;
    return;
  }
  if (!refilling()) {
    return;
  }
  refill_left = std::max(refill_left - seconds, 0.0F);
  const float done = 1.0F - refill_left / refill_count_seconds;
  shown = refill_from + (static_cast<float>(ammo) - refill_from) * done;
}

} // namespace phantomcrash
