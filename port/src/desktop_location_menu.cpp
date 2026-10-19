#include "desktop_location_menu.hpp"

#include "desktop_draw_list.hpp"

#include <utility>

namespace phantomcrash {

LocationMenuScreen::LocationMenuScreen(TextureCache &textures, const GlyphFont &font,
                                       LocationMenuScript script, const std::string &from,
                                       const FrameStyle &frame,
                                       const std::vector<MusicCredit> &credits, QuestSave quest)
    : textures_(textures), font_(font), menu_(std::move(script), from),
      frame_(font, frame, credits, menu_.script().frame), quest_(std::move(quest)) {
  const LocationMenuScript &data = menu_.script();
  for (const auto &item : data.items) {
    help_runs_.push_back(glyph_run(font_, item.help.text));
    static_cast<void>(textures_.get(item.wedge.image));
  }
  for (const auto &layer : data.layers) {
    static_cast<void>(textures_.get(layer.image));
  }
  frame_.preload(textures_);
}

void LocationMenuScreen::tick(const InputFrame &input, const double tick_seconds) {
  menu_.update(tick_seconds, input);
  pointer_input(input);
  if (menu_.faded_in()) {
    frame_.start_riding_plate();
  }
  frame_.update(tick_seconds);
}

void LocationMenuScreen::draw(const Viewport &viewport) {
  clear_black(textures_);
  const LocationMenuScript &script = menu_.script();
  DrawList list(textures_, font_, viewport, menu_.seconds());
  // The wedges go in before the fixed layers, so the help panel, on the same
  // layer, covers them. The original's picture ends at the canvas edge, so
  // they are cut there rather than showing in the widescreen margin.
  for (const auto &[item, angle] : menu_.wedges()) {
    Sprite wedge = script.items[static_cast<std::size_t>(item)].wedge;
    wedge.position = script.wheel.position;
    wedge.pivot = script.wheel.pivot;
    wedge.angle = angle;
    list.sprite(wedge, std::array<float, 4>{0.0F, 0.0F, canvas_width, canvas_height});
  }
  for (const auto &layer : script.layers) {
    list.sprite(layer);
  }
  const auto cursor = static_cast<std::size_t>(menu_.cursor());
  const LocationItem &item = script.items[cursor];
  if (!menu_.turning()) {
    list.sprite(item.highlight);
  }
  list.sprite(item.label);
  list.ticker(help_style(script.help, item.help), help_runs_[cursor], menu_.help_seconds());
  frame_.draw(list, quest_);
  list.flush();
  fill_black(textures_, menu_.black_alpha());
}

} // namespace phantomcrash
