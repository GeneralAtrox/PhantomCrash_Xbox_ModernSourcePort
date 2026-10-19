#pragma once

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/music_spectrum.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/sort_hints.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/sprite_shapes.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// The sprites the HUD writes numbers with (the original's number font 3,
// 0x27dc18 + 3 * 0x40): the ten digits and the comma, each with its advance.
struct NumberFont {
  std::array<Sprite, 10> digits;
  float advance{11.0F};
  Sprite comma;
  float comma_advance{5.0F};
  // The decimal point, for numbers with decimals; none in fonts without.
  Sprite point;
  float point_advance{5.0F};
};

// How a number is written (0xbc769): right to left, each glyph one advance
// left of the one before it, from `right`; at most `digits` digits (all
// nines past that), zero-padded or not, a comma every three digits or not.
struct NumberStyle {
  std::array<float, 2> right{}; // the right edge, and the top
  int digits{1};
  bool pad{};
  bool commas{};
  std::uint32_t colour{0xffffff};
  // Digits after the point: the value is then in hundredths for 2, and so
  // on, and `digits` counts the digits before the point.
  int decimals{};
};

[[nodiscard]] std::vector<Sprite> number_sprites(const NumberFont &font, const NumberStyle &style,
                                                 std::int64_t value, int layer);
// A number that starts at `left` instead (0xb1e39: the money on the
// details panels), with `width` set to how far it reaches.
[[nodiscard]] std::vector<Sprite> number_sprites_from(const NumberFont &font, NumberStyle style,
                                                      float left, std::int64_t value, int layer,
                                                      float &width);

// Throw std::runtime_error naming the entry at fault.
[[nodiscard]] NumberFont parse_number_font(const json::JsonValue &value);
[[nodiscard]] NumberStyle parse_number_style(const json::JsonValue &value);

// The HUD's SAVE button (a port addition): drawn just after the day, in the
// bar's black, `offset` from the "DAY-" label's top-left so it moves with it, as
// vector outlines over a `size` box in canvas units; `lit` while the mouse
// is over it. A click opens SAVE over the screen.
struct HudSaveButton {
  std::array<float, 2> offset{};
  std::array<float, 2> size{};
  std::vector<ShapeOutline> normal;
  std::vector<ShapeOutline> lit;
};

// The day and money in the top left of the town screens (0xb1ba5 and
// 0xb1d37): "DAY-" and the day from 01, the money and "NY".
struct HudStyle {
  NumberFont font;
  int layer{0x108};
  Sprite day_label;
  NumberStyle day;
  NumberStyle money;
  Sprite money_unit;
  std::optional<HudSaveButton> save_button;
};

[[nodiscard]] std::vector<Sprite> hud_sprites(const HudStyle &style, const QuestSave &quest);
// The SAVE button's box (canvas left, top, right, bottom), when it has one.
[[nodiscard]] std::optional<std::array<float, 4>> hud_save_box(const HudStyle &style);
// An amount as the money writer puts it (0xb1d37, the shops' prices and
// fees): `shown` NY in `style`, then "NY" one past its right edge.
[[nodiscard]] std::vector<Sprite> money_sprites(const HudStyle &hud, const NumberStyle &style,
                                                std::int64_t shown, int layer);

// Where a mod puts the day and money (the original has no such place): the
// "DAY-" label's top-left, the day's digits keeping their distance from it, and
// the money's right edge, "NY" keeping its distance from that.
struct HudPlacement {
  std::array<float, 2> day{};
  std::array<float, 2> money{};
};

[[nodiscard]] HudPlacement parse_hud_placement(const json::JsonValue &value);
// `style` moved to `placement`.
[[nodiscard]] HudStyle placed_hud(HudStyle style, const HudPlacement &placement);

// The CURRENT MUSIC meter: a row of bars beside the credit that light from
// the bottom with the music heard (MusicSpectrum, the port's own). Each bar
// rises at once to its band's level and falls back at fall_per_second.
struct MeterStyle {
  Sprite unlit; // a swatch stretched over each bar's dark part
  Sprite lit;   // and over its lit part
  int bars{7};
  float step{7.0F};
  std::array<float, 2> bar_size{4.0F, 24.0F};
  float level_step{2.0F}; // the lit height moves in whole steps of this
  float fall_per_second{3.0F};
  SpectrumStyle music; // how the levels are heard
};

class MusicMeter final {
public:
  explicit MusicMeter(MeterStyle style);

  // `heard` is each bar's level now (MusicSpectrum::levels()).
  void update(double tick_seconds, const std::vector<float> &heard);
  [[nodiscard]] const std::vector<float> &levels() const noexcept {
    return levels_;
  }

private:
  MeterStyle style_;
  std::vector<float> levels_;
};

// The meter's bars from `origin`, the first bar's top-left.
[[nodiscard]] std::vector<Sprite> meter_sprites(const MeterStyle &style,
                                                std::array<float, 2> origin,
                                                const std::vector<float> &levels);

// The RIDING SV plate (0x6dd74, 0x6db03): the plate, sliding in from the
// left once the screen has faded in, with the ridden Scoobee's name on it.
struct RidingPlateStyle {
  Sprite plate;                             // where it comes to rest
  std::array<float, 2> name_offset{106, 5}; // the name, from the plate's top-left
  std::uint32_t name_colour{0xf0f0f0};
  int name_layer{259};
  int max_glyphs{9};
  float slide_from{-320.0F}; // the plate starts this far left of its place
  double slide_seconds{8.0 / 60.0};
};

// How far from its place the plate is, `seconds` after it started.
[[nodiscard]] float riding_plate_offset(const RidingPlateStyle &style, double seconds);

// data/ui/frame.json: what the town screens share.
struct FrameStyle {
  HudStyle hud;
  MeterStyle meter;
  RidingPlateStyle riding_plate;
  SortHintStyle sort_hints; // over the goods lists (ASSEMBLE, the shops)
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] FrameStyle parse_frame_style(const json::JsonValue &document);

// Where one screen puts the frame (its data file's "frame"): the HUD or not,
// the RIDING SV plate or not, the meter's first bar (none: no meter), and
// the music's credit ticker.
struct FramePlacement {
  bool hud{true};
  // "hud_at": {"day": [x, y], "money": [x, y]}: a mod's own place. The original
  // has none: 0xb1b56 and 0xb1cdd ignore the coordinates they are passed.
  std::optional<HudPlacement> hud_at;
  // "save_offset": [x, y]: the SAVE button's offset from the day here, where
  // the screen's bar keeps its black elsewhere (a port addition).
  std::optional<std::array<float, 2>> save_offset;
  bool riding_plate{};
  std::optional<float> riding_plate_y; // the plate's height when not the style's (0x6dd74's y)
  // A plate of the screen's own in place of the style's (RUMBLING's, 0x6db03's last argument 1).
  std::optional<RidingPlateStyle> riding_plate_style;
  std::optional<std::array<float, 2>> meter;
  TickerStyle now_playing;
  std::string now_playing_format; // "{title}" and "{artist}" are replaced
};

[[nodiscard]] FramePlacement parse_frame_placement(const json::JsonValue &value);

} // namespace phantomcrash
