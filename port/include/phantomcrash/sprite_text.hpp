#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// A shadow under the replaced lettering, in canvas units; none at alpha 0.
struct TextShadow {
  std::array<float, 2> offset{1.0F, 1.0F};
  std::uint32_t colour{};
  float alpha{};
};

// Lettering in an atlas sprite drawn as text instead; the atlas rectangle
// stays the layout. region is source-relative LTRB: the frame art outside
// it is still drawn from the atlas. Without a font the text is the game's
// own atlas font; with one, a display font (SpriteTextTable::fonts) whose
// ink fills the region, over the art with its lettering erased when erase
// is set (each row of the region filled from its two ends).
struct SpriteTextStyle {
  Sprite source;
  std::string text;
  std::optional<std::array<float, 4>> region;
  std::optional<std::uint32_t> colour;
  float opacity{1.0F};  // the ink's own alpha, for translucent lettering
  float stroke_width{}; // extra glyph weight in canvas units
  bool centered{};
  // The original lettering's line pitch in source pixels; 0 fits the rect.
  float line_pitch{};
  std::string font;
  bool erase{};
  bool art{true}; // false: the sprite is lettering alone, none of its art drawn
  bool stretch{}; // the ink fills the region in both directions (condensed lettering)
  // Sized by the font's capitals and set on their line, so a family's lettering
  // keeps one size whatever its words (lowercase stays smaller).
  bool capitals{};
  // The box the lettering is set in, where it differs from region: region still
  // marks the art that is erased or left to the frame.
  std::optional<std::array<float, 4>> text_box;
  // How far the lettering leans, in x per y: the region and erase areas are
  // parallelograms whose bottom edge is the box's and whose top edge is
  // shifted right by slant times their height, and the text leans with them.
  float slant{};
  // The erase area's own lean, where upright or italic lettering sits on a
  // leaning plate; slant when absent.
  std::optional<float> erase_slant;
  // What erase fills when it reaches past the lettering (a painted shadow).
  std::optional<std::array<float, 4>> erase_region;
  std::optional<std::uint32_t> erase_fill; // a flat fill instead of each row's ends
  std::optional<std::uint32_t> outline;
  float outline_width{1.0F}; // canvas units
  float shadow_blur{};       // canvas units the shadow spreads
  std::optional<TextShadow> shadow;
};

// An installed outline font for display lettering: the first face found is
// used, after loading `files` (data-root relative) privately.
struct DisplayFont {
  std::vector<std::string> faces;
  std::vector<std::string> files;
  int weight{700};
  bool italic{};
};

// data/ui/sprite_text.json: the plain bitmap digits and labels the menus
// draw from their atlases, drawn as sharp text while sharp text is on.
struct SpriteTextTable {
  TextShadow shadow;
  std::map<std::string, DisplayFont> fonts;
  std::vector<SpriteTextStyle> replacements;
};

[[nodiscard]] SpriteTextStyle parse_sprite_text_style(const json::JsonValue &entry);
[[nodiscard]] TextShadow parse_text_shadow(const json::JsonValue &value);
[[nodiscard]] SpriteTextTable parse_sprite_text_table(const json::JsonValue &document);

} // namespace phantomcrash
