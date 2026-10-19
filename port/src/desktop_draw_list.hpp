#pragma once

#include "desktop_display_text.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/canvas.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/message_box.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/sprite_shapes.hpp"
#include "phantomcrash/sprite_text.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// Sprites and text collected for one frame and drawn lowest layer first, in
// the order added within a layer, as the original's layered 2D lists draw.
class DrawList final {
public:
  DrawList(TextureCache &textures, const GlyphFont &font, const Viewport &viewport, double seconds)
      : textures_(textures), font_(font), viewport_(viewport), seconds_(seconds) {}

  void sprite(const Sprite &source, float alpha = 1.0F);
  // A sprite cut to `clip` (left, top, right, bottom on the canvas), for
  // turned sprites that would otherwise show beyond the picture.
  void sprite(const Sprite &source, const std::array<float, 4> &clip, float alpha = 1.0F);
  void text(int layer, std::vector<GlyphQuad> glyphs, std::uint32_t colour,
            const std::optional<std::array<float, 4>> &clip = std::nullopt, float alpha = 1.0F);
  // Text standing in for the atlas's digits and labels, over the shadow
  // data/ui/sprite_text.json gives them while sharp text is on.
  void shaded_text(int layer, std::vector<GlyphQuad> glyphs, std::uint32_t colour);
  // A line of lettering in a data/ui/sprite_text.json display font, its ink
  // fitted to `box` (canvas left, top, right, bottom) as `layout` places
  // it (from the box's left by default); in the game's own font there when
  // the display font is not installed.
  void label(int layer, std::string text, const std::string &font, const std::array<float, 4> &box,
             std::uint32_t colour, const std::optional<TextShadow> &shadow, float shadow_blur,
             DisplayLayout layout = {});
  // A ticker `seconds` after its text was set.
  void ticker(const TickerStyle &style, const std::vector<int> &run, double seconds);
  // A message box as it is now: the growing window, then its lines and choices.
  void message_box(const MessageBox &box);
  // Vector outlines (data/ui/sprite_shapes.json's format) over `box`
  // (canvas left, top, right, bottom), their texels stretched onto it.
  void shape(int layer, std::vector<ShapeOutline> outlines, const std::array<float, 4> &box);
  // `draw` run in its turn on `layer` (a menu's 3D robot, between the
  // backdrop and the window over it).
  void custom(int layer, std::function<void()> draw) {
    commands_.push_back({layer, std::move(draw)});
  }
  // Draws everything collected, then forgets it.
  void flush();

private:
  struct Command {
    int layer{};
    std::function<void()> draw;
  };

  TextureCache &textures_;
  const GlyphFont &font_;
  const Viewport &viewport_;
  double seconds_;
  std::vector<Command> commands_;
};

// Clears the frame to black.
void clear_black(TextureCache &textures);
// Covers the whole window with black at `alpha` (the screens' fades).
void fill_black(TextureCache &textures, float alpha);

} // namespace phantomcrash
