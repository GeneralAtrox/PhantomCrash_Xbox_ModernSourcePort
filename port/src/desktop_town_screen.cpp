#include "desktop_town_screen.hpp"

#include "desktop_draw_list.hpp"
#include "phantomcrash/shop_pointer.hpp"

#include <utility>

namespace phantomcrash {

TownScreen::TownScreen(TextureCache &textures, const GlyphFont &font, const TownBasics &basics,
                       const std::string &from, const FrameStyle &frame,
                       const std::vector<MusicCredit> &credits, QuestSave quest)
    : textures_(textures), font_(font), frame_(font, frame, credits, basics.frame),
      quest_(std::move(quest)), music_(town_music(basics, from)), layers_(basics.layers) {
  for (const auto &layer : layers_) {
    static_cast<void>(textures_.get(layer.image));
  }
  frame_.preload(textures_);
}

void TownScreen::draw_sort_hints(DrawList &list, const GoodsList &goods) const {
  for (const Sprite &sprite :
       sort_hint_sprites(frame_.style().sort_hints, goods.sort_key(), goods.sort_fresh())) {
    list.sprite(sprite);
  }
}

InputFrame TownScreen::sort_pointer(InputFrame input, Buttons name, Buttons price) const {
  if (!input.clicked) {
    return input;
  }
  const auto &style = frame_.style().sort_hints;
  for (int i = 0; i < 2; ++i) {
    const auto &key = style.keys[i];
    Sprite arrow = style.up;
    arrow.position = key.arrow_at;
    if (shop_hit(input, key.button) || shop_hit(input, key.label) || shop_hit(input, arrow)) {
      input.pressed |= i == 0 ? name : price;
      SDL_Log("Shop sort: %s", i == 0 ? "name" : "price");
    }
  }
  return input;
}

void TownScreen::tick_frame(const double tick_seconds, const bool faded_in) {
  if (faded_in) {
    frame_.start_riding_plate();
  }
  frame_.update(tick_seconds);
}

void TownScreen::draw_town(const Viewport &viewport, const double seconds, const float black_alpha,
                           const std::function<void(DrawList &)> &content) {
  clear_black(textures_);
  DrawList list(textures_, font_, viewport, seconds);
  for (const auto &layer : layers_) {
    list.sprite(layer);
  }
  content(list);
  frame_.draw(list, quest_);
  list.flush();
  fill_black(textures_, black_alpha);
}

} // namespace phantomcrash
