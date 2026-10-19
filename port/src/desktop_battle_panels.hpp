#pragma once

#include "desktop_textures.hpp"
#include "desktop_battle_panel_text.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/battle_hud_state.hpp"
#include "phantomcrash/battle_panels_style.hpp"
#include "phantomcrash/canvas.hpp"

#include <cstdint>
#include <optional>

namespace phantomcrash {

// What the panels show besides the battle itself.
struct BattlePanelsInfo {
  int day{}; // counted from 0, shown from DAY-01
  std::int64_t money{};
  double mission_seconds{};
  bool gauges{true}; // DURABILITY and O.C TIME: not in the cockpit (0x4aaed)
};

// The original's single-player battle panels (data/ui/battle_panels.json):
// DURABILITY and O.C TIME at the bottom, the four weapon panels at the
// sides, the day, time, money and shootdown count, the DAMAGED / HIT /
// BINGO! messages, the durability warning and MISSILE ALERT.
class BattlePanels final {
public:
  // `viewer` is the robot the panels show: 0, the player's, or a split
  // screen's robot.
  BattlePanels(TextureCache &textures, const GlyphFont &font, BattlePanelsStyle style,
               int viewer = 0);

  // Turns what happened this tick into messages and moves them on.
  void tick(const Battle &battle, float seconds);
  void update_money(std::int64_t stored_money, float seconds);
  void draw(const Viewport &viewport, const Battle &battle, const BattlePanelsInfo &info,
            double clock) const;

private:
  // Draws `sprite` at its position plus `dx`, `dy`, kept to `anchor`.
  void put(const Viewport &viewport, const Sprite &sprite, HudAnchor anchor, float dx = 0.0F,
           float dy = 0.0F, std::optional<std::uint32_t> colour = {}, float alpha = 1.0F,
           float width = -1.0F) const;
  void draw_gauges(const Viewport &viewport, const Battle &battle, double clock) const;
  void draw_weapons(const Viewport &viewport, const Battle &battle, double clock) const;
  void draw_weapon(const Viewport &viewport, const Combatant &player, std::size_t slot,
                   double clock) const;
  void draw_digits(const Viewport &viewport, const WeaponPanelSide &side,
                   std::array<float, 2> origin, int value, int count) const;
  void draw_counters(const Viewport &viewport, const Battle &battle,
                     const BattlePanelsInfo &info) const;
  void draw_counter_label(const Viewport &viewport, const HudCounter &counter,
                          float number_left) const;
  void draw_time(const Viewport &viewport, double seconds) const;
  void draw_alerts(const Viewport &viewport, const Battle &battle, double clock) const;
  void draw_numbers(const Viewport &viewport, const HudCounter &counter, std::int64_t value) const;
  [[nodiscard]] float edge_x(HudAnchor anchor, float x, const Viewport &viewport) const;
  [[nodiscard]] const Combatant &viewer(const Battle &battle) const;

  TextureCache &textures_;
  BattlePanelsStyle style_;
  BattlePanelText text_;
  HudMessageLog log_;
  int viewer_{};
  std::optional<std::int64_t> money_target_;
  double money_shown_{};
  double money_from_{};
  double money_elapsed_{};
};

} // namespace phantomcrash
