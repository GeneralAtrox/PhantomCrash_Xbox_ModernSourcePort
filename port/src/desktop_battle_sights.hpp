#pragma once

#include <set>
#include <utility>

#include "desktop_textures.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/battle_hud_state.hpp"
#include "phantomcrash/battle_hud_style.hpp"
#include "phantomcrash/canvas.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/robot_parts.hpp"

#include <array>
#include <optional>

namespace phantomcrash {

// The battle HUD drawn over the world with the original's game_tex
// pieces (data/ui/battle_hud.json): the sight at the aim point with its
// camouflage warning and arrows to robots out of view, the reticle
// following the arms' target, the lock boxes closing on locked robots and
// the robots' markers with their armour and pilots' name tags.
class BattleSights final {
public:
  BattleSights(TextureCache &textures, const GlyphFont &font, BattleHudStyle style);

  // The robot's AI chip: the reticles' shapes and sizes, the tracking
  // speed and the lock time.
  void set_fire_control(const ChipStats &chip);
  void record_defeats(const Battle &battle);
  // Name tags the live RUMBLING's checklist has marked: their markers keep
  // the skull from earlier battles and saves too.
  void set_marked_tags(std::set<std::string> tags) {
    marked_tags_ = std::move(tags);
  }
  // The robot whose sights these are: 0, the player's, or a split
  // screen's robot.
  void set_viewer(const int robot) noexcept {
    viewer_ = robot;
  }
  // One screen of a split, its canvas in 640x480 units: the sights' offsets
  // are fractions of the screen and shrink with it, and 0x4faad, 0x503b9,
  // 0x51499 and 0x5218b draw their pieces at `pieces` (0.5 with more than
  // one screen). The markers (0x4de50) keep their size.
  void set_screen(std::array<float, 2> canvas, float pieces);

  // Moves the reticle and the lock boxes on by `elapsed` seconds, then
  // draws; `clock` runs the pulses.
  void draw(const Viewport &viewport, const Battle &battle, const Mat4 &view_projection,
            float elapsed, double clock);
  // The reticle and its boxes as last drawn, for the battle's locks.
  [[nodiscard]] const std::optional<LockBoxes> &lock_boxes() const noexcept {
    return boxes_;
  }
  [[nodiscard]] const BattleHudStyle &style() const noexcept {
    return style_;
  }
  // Not drawn this frame (a fixed camera view): no boxes.
  void hide() noexcept {
    boxes_.reset();
  }

private:
  struct CanvasPoint {
    float x{}, y{};
  };
  // The view this frame: where projected points land on the canvas.
  struct View {
    const Viewport &viewport;
    const Mat4 &view_projection;
    [[nodiscard]] std::optional<CanvasPoint> project(Vec3 point) const;
    [[nodiscard]] bool on_screen(Vec3 point) const;
  };

  // Draws `piece` centred (or by its top-left) at canvas `x`, `y`.
  void put(const Viewport &viewport, const HudPiece &piece, float x, float y, bool centered,
           float angle = 0.0F, bool flip = false, std::optional<std::uint32_t> colour = {},
           float alpha = 1.0F, float width_scale = 1.0F) const;
  void corners(const Viewport &viewport, const HudPiece &piece, CanvasPoint centre,
               std::array<float, 2> half, float pad) const;
  [[nodiscard]] const Combatant &viewer(const Battle &battle) const;
  [[nodiscard]] CanvasPoint sight_point(const View &view, const Battle &battle) const;
  void track(const View &view, const Battle &battle, CanvasPoint sight, float frames);
  [[nodiscard]] LockBoxes boxes_on(const Viewport &viewport) const;
  void draw_sight(const View &view, const Battle &battle, CanvasPoint sight, double clock) const;
  void draw_reticle(const Viewport &viewport, CanvasPoint centre) const;
  void draw_locks(const View &view, const Battle &battle);
  void draw_markers(const View &view, const Battle &battle) const;
  // Returns the tag's half glyph count, or nothing when it drew none.
  std::optional<float> draw_tag(const Viewport &viewport, const std::string &tag, CanvasPoint point,
                                std::uint32_t colour) const;
  void draw_marker_extras(const Viewport &viewport, const Combatant &robot, CanvasPoint point,
                          std::uint32_t colour) const;
  [[nodiscard]] std::array<float, 2> inner_half() const;
  // The safe area's scale (battle_hud.json) on this screen.
  [[nodiscard]] std::array<float, 2> offset_scale() const;
  [[nodiscard]] std::array<float, 2> outer_half() const;

  std::set<std::pair<std::string, std::string>> defeated_; // identities, never reusable slots
  std::set<std::string> marked_tags_;
  TextureCache &textures_;
  const GlyphFont &font_;
  BattleHudStyle style_;
  ReticleTracker tracker_;
  CanvasPoint reticle_{};
  std::optional<LockBoxes> boxes_;
  // Right shoulder, left shoulder, right arm melee, left arm melee.
  std::array<LockBox, 4> locks_{};
  int viewer_{};
  std::array<float, 2> screen_{1.0F, 1.0F}; // the screen's canvas / 640x480
  float split_pieces_{1.0F};
  float pieces_{1.0F}; // the scale put() draws at
};

} // namespace phantomcrash
