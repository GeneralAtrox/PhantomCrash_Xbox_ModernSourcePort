#include "desktop_battle_panel_text.hpp"
#include "desktop_sprite_text.hpp"

namespace phantomcrash {

BattlePanelText::BattlePanelText(TextureCache &textures, const GlyphFont &font,
                                 const BattlePanelsStyle &style)
    : textures_(textures), font_(font) {
  const auto add = [&](const Sprite &source, const std::string &label) {
    if (!source.image.empty()) {
      replacements_[{source.image, source.rect}] = {source, label, {}, {}};
    }
  };
  for (std::size_t digit = 0; digit < 10; ++digit) {
    const auto label = std::to_string(digit);
    add(style.font.digits[digit], label);
    add(style.weapons.left.digits[digit], label);
    add(style.weapons.right.digits[digit], label);
  }
  add(style.font.comma, ",");
  add(style.colon, ":");
  add(style.weapons.left.percent, "%");
  add(style.weapons.right.percent, "%");
  for (const auto &replacement : style.text_replacements) {
    replacements_[{replacement.source.image, replacement.source.rect}] = replacement;
  }
}

bool BattlePanelText::draw(const Sprite &sprite, const Viewport &viewport,
                           const float alpha) const {
  const auto found = replacements_.find({sprite.image, sprite.rect});
  if (found == replacements_.end()) {
    return false;
  }
  // The menus' shadow (data/ui/sprite_text.json), while sharp text is on.
  const SpriteText *shared = textures_.sprite_text();
  return draw_sprite_text(textures_, font_, sprite, found->second, viewport, alpha,
                          shared != nullptr ? shared->shadow() : TextShadow{});
}

} // namespace phantomcrash
