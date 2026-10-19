#pragma once

#include "desktop_battle_panels.hpp"
#include "desktop_battle_sights.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/canvas.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/glyph_font.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// What the HUD shows besides the battle itself.
struct BattleHudInfo {
  BattlePanelsInfo panels;
  std::optional<float> progress; // the lesson goal, 0..1
  std::string counter;           // beside the goal bar ("2 / 3")
  bool sights{true};             // only in the first-person and behind views
};

// The battle HUD: the original's sight, reticle, lock boxes and robot
// markers over the world (BattleSights) and its panels (BattlePanels).
// The lesson goal bar in the top left is the port's own: the original
// leaves the lessons' goals to Pepper's messages.
class BattleHud final {
public:
  // `viewer` is the robot the HUD is for: 0, the player's, or a split
  // screen's robot.
  BattleHud(TextureCache &textures, const GlyphFont &font, std::optional<BattleHudStyle> sights,
            std::optional<BattlePanelsStyle> panels, int viewer = 0);

  // The player's AI chip (see BattleSights::set_fire_control).
  void set_fire_control(const ChipStats &chip);
  // See BattleSights::set_screen.
  void set_screen(std::array<float, 2> canvas, float pieces);
  void tick(const Battle &battle, float seconds);
  // See BattleSights::set_marked_tags.
  void set_marked_tags(std::set<std::string> tags) {
    if (sights_) {
      sights_->set_marked_tags(std::move(tags));
    }
  }
  // `clock` is the battle's time; the reticle moves on by the time since
  // the last draw.
  void draw(const Viewport &viewport, const Battle &battle, const Mat4 &view_projection,
            const BattleHudInfo &info, double clock);
  // The boxes `chip` gives with this HUD's sizes, for a robot that locks
  // without a drawn reticle (Battle::set_sight).
  [[nodiscard]] SightBoxes sight_boxes(const ChipStats &chip) const {
    return sights_ ? phantomcrash::sight_boxes(sights_->style(), chip) : SightBoxes{};
  }
  // The reticle's boxes as last drawn (BattleSights::lock_boxes).
  [[nodiscard]] std::optional<LockBoxes> lock_boxes() const {
    return sights_ ? sights_->lock_boxes() : std::nullopt;
  }

private:
  void draw_goal(const Viewport &viewport, const BattleHudInfo &info) const;

  TextureCache &textures_;
  const GlyphFont &font_;
  std::optional<BattleSights> sights_; // none when battle_hud.json failed to load
  std::optional<BattlePanels> panels_; // none when battle_panels.json failed to load
  std::optional<double> last_clock_;
};

// The HUD for `viewer`'s screen of `screens` (ui/battle_hud.json, and
// ui/battle_panels.json or its split layouts); a style that fails to load
// is logged and left out.
[[nodiscard]] std::unique_ptr<BattleHud> make_battle_hud(TextureCache &textures,
                                                         const GlyphFont &font,
                                                         const DataRoots &roots, int screens,
                                                         int viewer);

} // namespace phantomcrash
