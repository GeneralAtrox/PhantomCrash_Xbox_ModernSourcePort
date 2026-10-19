#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace phantomcrash {

// An atlas piece of the battle HUD and how opaque it is drawn.
struct HudPiece {
  Sprite sprite;
  float alpha{1.0F};
};

// A reticle's half size in 640x480 units, x from x_min to x_max and y from
// y_min to y_max as the robot's value runs 0..value_max (0x04db69, tables
// 0x203c48 and 0x203c78, one row per shape).
using ReticleSizes = std::array<float, 4>;
inline constexpr int reticle_shapes = 3; // square, wide, tall

// The world-space battle HUD's pieces and layout, from
// data/ui/battle_hud.json (see its notes for the original's drawers).
struct BattleHudStyle {
  float sense_range{75.0F}; // markers, arrows and the camouflage warning
  // The original maps its HUD into this part of the 640x480 screen
  // (0x4e6b8), which shrinks every offset measured about a projected point.
  std::array<float, 2> safe_area{576.0F, 448.0F};
  float aim_distance{64.0F}; // the sight marks the aim this far ahead

  struct Sight {
    HudPiece ring;
    HudPiece outline;
    HudPiece fill;
    std::vector<std::array<float, 3>> markers; // x, y from the sight; degrees
    float arrow_radius{96.0F};
    float arrow_alpha{0.5F};
  } sight;

  // The robot's fire control: the reticles' shape (0 square, 1 wide,
  // 2 tall) and size value, and how fast the reticle follows a target.
  struct Control {
    int inner_shape{};
    float inner_value{500.0F};
    int outer_shape{};
    float outer_value{500.0F};
    float track_value{500.0F};
    float value_max{999.0F};
    // NDC per frame: base + scale * min(track_value / track_full, 1). The
    // original (0x4f6a0) divides by value_max (999) with a base of 0.02;
    // see track_speed.
    float track_base{0.02F};
    float track_scale{0.08F};
    float track_full{999.0F};
    float track_range{75.0F}; // the reticle follows enemies this close
    float lock_frames{30.0F}; // a lock box closes in this long
  } control;

  struct Reticle {
    // Each piece is the top-left corner, mirrored and turned for the rest.
    std::array<HudPiece, 3> inner; // by shape
    std::array<HudPiece, 3> outer;
    std::array<ReticleSizes, 3> inner_sizes{};
    std::array<ReticleSizes, 3> outer_sizes{};
    float inner_pad{2.0F}; // added to a corner's half size, inward
    float outer_pad{14.0F};
    HudPiece diamond;
  } reticle;

  // Lock box corners at the locked robot, drawn turned k * 90 degrees
  // clockwise from the top-left one.
  struct Locks {
    HudPiece right_shoulder;
    HudPiece left_shoulder;
    HudPiece melee;
    std::array<float, 2> corner{9.0F, 9.0F}; // from the robot's point
    std::uint32_t colour{0x00b496};
  } locks;

  struct Markers {
    HudPiece frame;
    std::array<float, 2> frame_offset{-72.0F, -18.0F};
    HudPiece bar;
    std::array<float, 2> bar_offset{-66.0F, -13.0F};
    // Member, leader, champion and reserve in RUMBLING; 0..2 also serve
    // the original team palette in training/versus.
    std::vector<std::uint32_t> team_colours;
    // The pilot's name tag (0x0bdc9b): glyphs of `cell` square every
    // `advance`, centred on the robot's point + `offset`, in the team
    // colour; tags of more than max_glyphs (under 30 Shift-JIS bytes) are
    // not drawn.
    struct Tag {
      std::array<float, 2> offset{-35.0F, -30.0F};
      float cell{12.0F};
      float advance{12.0F};
      int max_glyphs{14};
    } tag;
    // A team leader's icon (sprite 0x567) left of its tag: at the robot's
    // point + leader_offset, moved left by the tag's half advance as the
    // tag is. Only a tagged robot shows it.
    std::optional<HudPiece> defeated;
    std::array<float, 2> defeated_offset{-62.0F, -30.0F};
    std::optional<HudPiece> leader;
    std::array<float, 2> leader_offset{-62.0F, -30.0F};
    // Versus mode's 1P-4P labels (sprites 0x5b8 + n), by their top-left at
    // the point + number_offset, on robots without a tag.
    std::vector<HudPiece> player_numbers;
    std::array<float, 2> number_offset{-54.0F, -34.0F};
  } markers;

  [[nodiscard]] std::uint32_t team_colour(int team) const;
  // Canvas units per 640x480 unit about a projected point.
  [[nodiscard]] std::array<float, 2> safe_scale() const;
};

// Throws std::runtime_error when a piece is missing or malformed.
[[nodiscard]] BattleHudStyle parse_battle_hud_style(const json::JsonValue &document);

// A robot's fire control boxes in the 640x480 canvas: the half sizes of
// the inner box (arm weapons) and the outer box (shoulder and melee
// weapons); by default shape 0 at value 0.
struct SightBoxes {
  std::array<float, 2> inner_half{40.0F, 40.0F};
  std::array<float, 2> outer_half{70.0F, 70.0F};
};

struct ChipStats; // robot_parts.hpp

// The boxes `chip` gives, by its shapes and values (0x04db69).
[[nodiscard]] SightBoxes sight_boxes(const BattleHudStyle &style, const ChipStats &chip);

// How far the reticle moves toward its target a frame, in NDC, for the
// control's track value (a CHIP's Aim Speed).
[[nodiscard]] float track_speed(const BattleHudStyle::Control &control);

// A reticle's half width and height in 640x480 units for `value`.
[[nodiscard]] std::array<float, 2> reticle_half_size(const ReticleSizes &sizes, float value,
                                                     float value_max);

// How far inward from its box corner a corner piece's centre sits, in
// 640x480 units: the original adds (size / 2 + pad) / 640 and / 480 in NDC.
[[nodiscard]] std::array<float, 2> reticle_corner_inset(const HudPiece &corner, float pad);

} // namespace phantomcrash
