#include "desktop_battle_panels.hpp"
#include "phantomcrash/money.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace phantomcrash {
namespace {

constexpr int number_layer = 0x108;
constexpr float percent_full = 100.0F;
// MISSILE ALERT's alpha: floor + swing * the triangle wave (0x4ad3b).
constexpr float alert_floor = 0.5F;
constexpr float alert_swing = 0.5F;

constexpr int ammo_digits = 4;
constexpr int charge_digits = 3;
constexpr int ammo_shown_max = 9999;

// The message an event shows, or -1: chatter has its own window, and
// respawns and supply pickups show none.
int message_for(const BattleEventKind kind) {
  switch (kind) {
  case BattleEventKind::damaged:
  case BattleEventKind::shot_down:
    return hud_message_damaged;
  case BattleEventKind::hit:
    return hud_message_hit;
  case BattleEventKind::bingo:
    return hud_message_bingo;
  case BattleEventKind::chatter:
  case BattleEventKind::respawned:
  case BattleEventKind::supply:
  case BattleEventKind::carrier_down:
  case BattleEventKind::object_reward:
  case BattleEventKind::fired:
  case BattleEventKind::impact:
    break;
  }
  return -1;
}

float ratio(const float value, const float maximum) {
  return maximum > 0.0F ? std::clamp(value / maximum, 0.0F, 1.0F) : 0.0F;
}

} // namespace

BattlePanels::BattlePanels(TextureCache &textures, const GlyphFont &font, BattlePanelsStyle style,
                           const int viewer)
    : textures_(textures), style_(std::move(style)), text_(textures, font, style_),
      log_(style_.messages), viewer_(viewer) {}

const Combatant &BattlePanels::viewer(const Battle &battle) const {
  return battle.robots().at(static_cast<std::size_t>(viewer_));
}

float BattlePanels::edge_x(const HudAnchor anchor, const float x, const Viewport &viewport) const {
  return style_.widescreen ? anchor_x(anchor, x, viewport.visible_left(), viewport.visible_right(),
                                      style_.canvas[0])
                           : x;
}

void BattlePanels::put(const Viewport &viewport, const Sprite &sprite, const HudAnchor anchor,
                       const float dx, const float dy, const std::optional<std::uint32_t> colour,
                       const float alpha, const float width) const {
  Sprite out = sprite;
  out.position = {edge_x(anchor, sprite.position[0] + dx, viewport), sprite.position[1] + dy};
  out.colour = colour.value_or(sprite.colour);
  if (width >= 0.0F) {
    out.size = std::array<float, 2>{width, sprite.rect[3] - sprite.rect[1]};
  }
  if (!text_.draw(out, viewport, alpha)) {
    draw_sprite(textures_, out, viewport, 0.0, alpha);
  }
}

// The messages come from the battle's events for the viewer (0x04da6c).
void BattlePanels::tick(const Battle &battle, const float seconds) {
  for (const BattleEvent &event : battle.events()) {
    if (const int message = message_for(event.kind); event.robot == viewer_ && message >= 0) {
      log_.push(message);
    }
  }
  log_.update(seconds * hud_frames_per_second);
}

// Animate only the displayed NY; rewards and the saved balance remain immediate.
void BattlePanels::update_money(const std::int64_t stored_money, const float seconds) {
  const auto target = shown_ny(stored_money);
  if (!money_target_ || target < *money_target_ || style_.money_count_seconds <= 0.0F) {
    money_target_ = target;
    money_shown_ = static_cast<double>(target);
    money_from_ = money_shown_;
    money_elapsed_ = 0.0;
    return;
  }
  if (target != *money_target_) {
    money_target_ = target;
    money_from_ = money_shown_;
    money_elapsed_ = 0.0;
    return;
  }
  money_elapsed_ += std::max(seconds, 0.0F);
  const double progress = std::min(money_elapsed_ / style_.money_count_seconds, 1.0);
  money_shown_ = std::lerp(money_from_, static_cast<double>(target), progress);
}

void BattlePanels::draw(const Viewport &viewport, const Battle &battle,
                        const BattlePanelsInfo &info, const double clock) const {
  if (info.gauges) {
    draw_gauges(viewport, battle, clock);
  }
  draw_weapons(viewport, battle, clock);
  if (style_.labels) {
    draw_counters(viewport, battle, info);
  }
  draw_alerts(viewport, battle, clock);
}

void BattlePanels::draw_gauges(const Viewport &viewport, const Battle &battle,
                               const double clock) const {
  const Combatant &player = viewer(battle);
  const HudGauge &durability = style_.durability;
  // 0x04aaed draws the shown armour (+0x50) and flashes while it fills.
  const float armour = ratio(player.shown_armour.value(), player.max_hp);
  std::uint32_t colour = armour >= durability.low ? durability.colour : durability.low_colour;
  if (player.shown_armour.filling()) {
    colour = hud_flash(colour, durability.flash_colour, clock, style_.flash_frames);
  }
  put(viewport, durability.frame, durability.anchor);
  put(viewport, durability.fill, durability.anchor, 0.0F, 0.0F, colour, 1.0F,
      durability.width * armour);

  const HudGauge &optic = style_.optic;
  const float gauge = std::clamp(player.camouflage, 0.0F, 1.0F);
  colour = gauge >= optic.low ? optic.colour : optic.low_colour;
  if (!player.camouflaged && player.combat.camouflage_seconds != 0 && gauge < 1.0F) {
    colour = hud_flash(style_.optic_active_colour, optic.colour, clock, style_.flash_frames);
  }
  put(viewport, optic.frame, optic.anchor);
  put(viewport, optic.fill, optic.anchor, 0.0F, 0.0F, colour, 1.0F, optic.width * gauge);
}

void BattlePanels::draw_weapons(const Viewport &viewport, const Battle &battle,
                                const double clock) const {
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    draw_weapon(viewport, viewer(battle), slot, clock);
  }
}

// One weapon's panel (0x4ad3b): the frame and icon flash while the
// ammunition refills; the readout is the ammunition, a charge or nothing.
void BattlePanels::draw_weapon(const Viewport &viewport, const Combatant &player,
                               const std::size_t slot, const double clock) const {
  const WeaponTuning *weapon = player.weapons[slot];
  if (weapon == nullptr) {
    return;
  }
  const Magazine &magazine = player.magazines[slot];
  const WeaponPanelStyle &panels = style_.weapons;
  // Slots 0 and 2 (the right arm and shoulder) use the right side.
  const bool right = slot % 2 == 0;
  const WeaponPanelSide &side = right ? panels.right : panels.left;
  const auto origin = slot < 2 ? side.arm : side.shoulder;
  const std::uint32_t colour =
      magazine.refilling()
          ? hud_flash(panels.ready_colour, panels.refill_colour, clock, style_.flash_frames)
          : panels.ready_colour;
  put(viewport, side.frame, side.anchor, origin[0], origin[1], colour);
  if (player.lost[slot]) {
    put(viewport, side.lost, side.anchor, origin[0], origin[1], std::nullopt);
    return;
  }
  const auto found = panels.icons.find(weapon->hud_kind);
  if (found == panels.icons.end()) {
    return;
  }
  const WeaponIcon &icon = found->second;
  // A missile's icon depends on whether it homes (weapon +0x40).
  const bool homing = weapon->homing && icon.homing_left && icon.homing_right;
  const Sprite &picture =
      right ? (homing ? *icon.homing_right : icon.right) : (homing ? *icon.homing_left : icon.left);
  put(viewport, picture, side.anchor, origin[0] + side.icon[0], origin[1] + side.icon[1], colour);
  if (icon.readout == WeaponReadout::none) {
    return;
  }
  const float amount =
      magazine.limited() ? ratio(magazine.shown, static_cast<float>(magazine.max)) : 1.0F;
  std::uint32_t bar_colour = amount >= panels.bar_low ? panels.bar_colour : panels.bar_low_colour;
  if (icon.readout == WeaponReadout::ammo && magazine.refilling()) {
    bar_colour = hud_flash(bar_colour, panels.refill_colour, clock, style_.flash_frames);
  } else if (icon.readout == WeaponReadout::charge && magazine.ammo < magazine.max) {
    bar_colour =
        hud_flash(panels.bar_colour, panels.bar_charging_colour, clock, style_.flash_frames);
  }
  if (side.bar) {
    const float width = panels.bar_width * amount;
    put(viewport, *side.bar, side.anchor, origin[0] - (side.bar_mirrored ? width : 0.0F), origin[1],
        bar_colour, 1.0F, width);
  }
  if (icon.readout == WeaponReadout::charge) {
    const int percent = static_cast<int>(std::floor(amount * percent_full));
    draw_digits(viewport, side, origin, percent, charge_digits);
    put(viewport, side.percent, side.anchor, origin[0], origin[1], panels.ready_colour);
  } else if (magazine.limited()) {
    const int shown = std::min(static_cast<int>(std::floor(magazine.shown)), ammo_shown_max);
    draw_digits(viewport, side, origin, shown, ammo_digits);
  }
}

// `count` zero-padded digits of `value` at the side's digit positions.
void BattlePanels::draw_digits(const Viewport &viewport, const WeaponPanelSide &side,
                               const std::array<float, 2> origin, int value,
                               const int count) const {
  for (int i = count - 1; i >= 0; --i) {
    const auto &at = side.digit_positions[static_cast<std::size_t>(i)];
    put(viewport, side.digits[static_cast<std::size_t>(value % 10)], side.anchor, origin[0] + at[0],
        origin[1] + at[1], style_.weapons.ready_colour);
    value /= 10;
  }
}

void BattlePanels::draw_numbers(const Viewport &viewport, const HudCounter &counter,
                                const std::int64_t value) const {
  const auto digits = number_sprites(style_.font, counter.number, value, number_layer);
  float left = counter.number.right[0];
  for (const Sprite &sprite : digits) {
    left = std::min(left, sprite.position[0]);
    put(viewport, sprite, counter.anchor);
  }
  draw_counter_label(viewport, counter, left);
}

void BattlePanels::draw_counter_label(const Viewport &viewport, const HudCounter &counter,
                                      const float number_left) const {
  Sprite label = counter.label;
  auto size = label.size.value_or(
      std::array<float, 2>{label.rect[2] - label.rect[0], label.rect[3] - label.rect[1]});
  // Atlas lettering had transparent right padding; outline glyphs need an explicit gap.
  size[0] = std::min(size[0], std::max(0.0F, number_left - label.position[0] - style_.text_gap));
  label.size = size;
  put(viewport, label, counter.anchor);
}

void BattlePanels::draw_time(const Viewport &viewport, const double seconds) const {
  const HudCounter &counter = style_.time;
  const auto total = static_cast<std::int64_t>(std::max(seconds, 0.0));
  // Hours, minutes and seconds, written right to left (0xbd123).
  const std::array<std::int64_t, 3> fields{total % 60, total / 60 % 60, total / 3600};
  NumberStyle style = counter.number;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    float left = style.right[0];
    for (const Sprite &sprite : number_sprites(style_.font, style, fields[i], number_layer)) {
      put(viewport, sprite, counter.anchor);
      left = std::min(left, sprite.position[0]);
    }
    if (i + 1 < fields.size()) {
      Sprite colon = style_.colon;
      colon.position = {left - style_.colon_advance, style.right[1]};
      put(viewport, colon, counter.anchor, 0.0F, 0.0F, style.colour);
      left -= style_.colon_advance;
    }
    style.right[0] = left;
  }
  draw_counter_label(viewport, counter, style.right[0]);
}

void BattlePanels::draw_counters(const Viewport &viewport, const Battle &battle,
                                 const BattlePanelsInfo &info) const {
  draw_numbers(viewport, style_.day, info.day + 1);
  draw_time(viewport, info.mission_seconds);
  draw_numbers(viewport, style_.money,
               money_target_ ? static_cast<std::int64_t>(money_shown_) : shown_ny(info.money));
  draw_numbers(viewport, style_.shootdown, viewer(battle).kills);
}

void BattlePanels::draw_alerts(const Viewport &viewport, const Battle &battle,
                               const double clock) const {
  const std::vector<HudMessageLog::Entry> entries =
      style_.messages_shown ? log_.entries() : std::vector<HudMessageLog::Entry>{};
  for (const HudMessageLog::Entry &entry : entries) {
    const auto kind = static_cast<std::size_t>(std::clamp(entry.kind, 0, 2));
    put(viewport, style_.message_sprites[kind], style_.messages_anchor, style_.messages_position[0],
        style_.messages_position[1] + entry.offset, std::nullopt, entry.alpha);
  }

  const Combatant &player = viewer(battle);
  const float armour = player.hp / std::max(player.max_hp, 1e-3F);
  if (armour > 0.0F && armour < style_.warning_quarter_below) {
    put(viewport, style_.warning_stripes, style_.warning_anchor);
    put(viewport, style_.warning_quarter, style_.warning_anchor);
  } else if (armour < style_.warning_half_below) {
    put(viewport, style_.warning_stripes, style_.warning_anchor);
    put(viewport, style_.warning_half, style_.warning_anchor);
  }

  // A missile homing on the viewer (0x51045 marks it, 0x4ad3b warns).
  const auto &shots = battle.armory().projectiles();
  const bool homing = std::any_of(shots.begin(), shots.end(), [this](const Projectile &shot) {
    return shot.target == viewer_ && shot.owner != viewer_ && shot.weapon != nullptr &&
           shot.weapon->kind == WeaponKind::missile;
  });
  if (homing) {
    const float alpha = alert_floor + alert_swing * hud_wave(clock, style_.alert_wave_frames);
    put(viewport, style_.missile_alert, style_.alert_anchor, 0.0F, 0.0F, std::nullopt, alpha);
  }
}

} // namespace phantomcrash
