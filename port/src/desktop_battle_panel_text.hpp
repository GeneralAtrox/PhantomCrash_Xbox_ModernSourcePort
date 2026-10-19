#pragma once

#include "desktop_textures.hpp"
#include "phantomcrash/battle_panels_style.hpp"
#include "phantomcrash/glyph_font.hpp"

#include <map>
#include <utility>

namespace phantomcrash {

// Atlas coordinates remain layout metadata; lettering uses the menu's outline font.
class BattlePanelText final {
public:
  BattlePanelText(TextureCache &textures, const GlyphFont &font, const BattlePanelsStyle &style);
  bool draw(const Sprite &sprite, const Viewport &viewport, float alpha) const;

private:
  TextureCache &textures_;
  const GlyphFont &font_;
  using Key = std::pair<std::string, std::array<float, 4>>;
  std::map<Key, BattlePanelTextStyle> replacements_;
};

} // namespace phantomcrash
