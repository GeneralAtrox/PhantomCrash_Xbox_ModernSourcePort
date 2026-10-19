#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

// What a cockpit piece shows.
enum class CockpitGauge {
  none,
  climb,      // the smoothed vertical speed (robot +0x2568)
  speed,      // the smoothed ground speed over the dash speed
  armour,     // armour over its maximum, in the armour colours
  camouflage, // the Optic Camouflage gauge, in the camouflage colours
};

// A strip of cells in the cockpit texture that a piece's UVs step through,
// in texels (0x205620, 0x205708, 0x205698, 0x2056d0).
struct CockpitCells {
  std::vector<std::array<float, 2>> offsets;
  float per_second{}; // cycles on its own at this rate; 0: picked by a gauge
};

// One mesh of the cockpit model (object slot 0x4dd + mesh).
struct CockpitPiece {
  int mesh{};
  std::string group;
  CockpitGauge scale{CockpitGauge::none}; // stretches the mesh along x
  std::string cells;                      // steps its UVs, empty for none
  CockpitGauge cell_source{CockpitGauge::none};
};

// Where a group of pieces sits: `slide` units up per unit of the slide
// (negative: down) and `bob` times the bob.
struct CockpitGroup {
  float slide{};
  float bob{};
};

// data/ui/cockpit.json: the first-person cockpit (0x04c459). The defaults
// are the original's.
struct CockpitStyle {
  std::string model{"models/binlink/216.gltf"};
  float texture_size{512.0F};
  float distance{8.0F}; // ahead of the eye
  float height{-0.1F};
  std::uint32_t colour{0x64ff6e}; // every piece but the gauges with their own
  float ease_per_second{3.0F};    // the gauges' 0.05 a 60 Hz frame
  // The climb gauge is full at the speed gravity gives over this many
  // battle ticks, falling and rising.
  float climb_fall_ticks{6.0F};
  float climb_rise_ticks{2.5F};
  float tick_rate{30.0F};
  std::uint32_t armour_colour{0xffc823};
  std::uint32_t armour_low_colour{0xc04040};
  std::uint32_t armour_flash_colour{0x8080e1};
  float armour_low{0.25F};
  std::uint32_t camouflage_colour{0x3f77c1};
  std::uint32_t camouflage_low_colour{0xc040c0};
  float camouflage_low{0.25F};
  float flash_frames{10.0F};
  // The visor tint over the view while camouflaged: mesh 0 scaled 1.5 wide,
  // one unit ahead, added in this colour at the camouflage's strength.
  int visor_mesh{0};
  std::uint32_t visor_colour{0x1e0f64};
  float visor_distance{1.0F};
  float visor_width{1.5F};
  float visor_fade_ticks{60.0F};
  std::map<std::string, CockpitGroup> groups;
  std::map<std::string, CockpitCells> cells;
  std::vector<CockpitPiece> pieces;
};

// Throws std::runtime_error on a malformed style.
[[nodiscard]] CockpitStyle parse_cockpit_style(const json::JsonValue &document);

// The player's robot as the cockpit reads it.
struct CockpitInput {
  float vertical_speed{}; // units a second, up positive
  float gravity{};        // units a second squared
  float speed_ratio{};    // ground speed over the dash speed
  float armour{};         // shown, 0..1 (robot +0x50)
  bool filling{};         // the shown armour is filling: its gauge flashes
  float camouflage{};     // gauge, 0..1
  bool camouflaged{};
  float sway{};  // the aim's heading less the camera's yaw
  float slide{}; // 0 in place, 1 out of view
  float bob{};
};

// One mesh to draw, placed in the eye's space (+z ahead).
struct CockpitDraw {
  int mesh{};
  Mat4 place;
  std::uint32_t colour{}; // 0xRRGGBB
  float alpha{1.0F};
  float scale_x{1.0F};
  std::array<float, 2> uv_offset{};
  bool additive{};
};

class Cockpit final {
public:
  explicit Cockpit(CockpitStyle style) : style_(std::move(style)) {}

  [[nodiscard]] const CockpitStyle &style() const noexcept {
    return style_;
  }
  // Eases the gauges and the visor tint and runs the animations.
  void tick(const CockpitInput &input, float seconds);
  // The pieces to draw at `clock` seconds, the visor tint first.
  [[nodiscard]] std::vector<CockpitDraw> draws(const CockpitInput &input, double clock) const;
  [[nodiscard]] float climb() const noexcept {
    return climb_;
  }
  [[nodiscard]] float speed() const noexcept {
    return speed_;
  }

private:
  [[nodiscard]] float value(CockpitGauge gauge, const CockpitInput &input) const;
  [[nodiscard]] std::uint32_t colour(CockpitGauge gauge, const CockpitInput &input,
                                     double clock) const;

  CockpitStyle style_;
  float climb_{};
  float speed_{};
  float visor_{};
  float animation_{}; // seconds
};

} // namespace phantomcrash
