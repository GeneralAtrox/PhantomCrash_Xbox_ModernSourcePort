#pragma once

#include "desktop_textures.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/sprite_text.hpp"

#include <map>
#include <string>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace phantomcrash {

// Draws `style`'s text over `sprite`'s quad in the game's font, the frame
// art outside its region still from the atlas, over `shadow`. False when
// the region does not fit the sprite.
bool draw_sprite_text(TextureCache &textures, const GlyphFont &font, const Sprite &sprite,
                      const SpriteTextStyle &style, const Viewport &viewport, float alpha,
                      const TextShadow &shadow);
// Several labels on one sprite (one alone is drawn as above): each in its
// display font, over the art with all of their lettering erased.
bool draw_sprite_text(TextureCache &textures, const GlyphFont &font, const Sprite &sprite,
                      std::span<const SpriteTextStyle> styles, const Viewport &viewport,
                      float alpha, const TextShadow &shadow);

// The atlas lettering every screen draws as text (data/ui/sprite_text.json),
// found by the sprite's image and rectangle.
class SpriteText final {
public:
  SpriteText(GlyphFont font, const SpriteTextTable &table);

  // Every label on the sprite, in the order listed; empty when none.
  [[nodiscard]] std::span<const SpriteTextStyle> find(const Sprite &sprite) const;
  [[nodiscard]] const GlyphFont &font() const noexcept {
    return font_;
  }
  [[nodiscard]] const TextShadow &shadow() const noexcept {
    return shadow_;
  }
  [[nodiscard]] const DisplayFont *display_font(const std::string &name) const {
    const auto found = fonts_.find(name);
    return found == fonts_.end() ? nullptr : &found->second;
  }

private:
  GlyphFont font_;
  TextShadow shadow_;
  std::map<std::string, DisplayFont> fonts_;
  // The source's image and rect, and its id: an entry naming one matches
  // only sprites with that id (art reused under other lettering).
  using Key = std::tuple<std::string, std::array<float, 4>, std::string>;
  std::map<Key, std::vector<SpriteTextStyle>> replacements_;
};

// Gives `textures` the lettering data/ui/sprite_text.json names, drawn in `font`.
void load_sprite_text(TextureCache &textures, const DataRoots &roots, const GlyphFont &font);

} // namespace phantomcrash
