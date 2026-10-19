#include "desktop_pepper_guide.hpp"

#include "desktop_draw_list.hpp"

#include <limits>
#include <utility>

namespace phantomcrash {
namespace {

// A script that shows the guide and then waits for good.
StoryEvent guide_event(const EventCharacter &guide) {
  StoryEvent event;
  event.cast = {guide.character};
  EventCommand show;
  show.op = EventOp::character;
  show.character = guide;
  EventCommand wait;
  wait.op = EventOp::wait;
  wait.frames = std::numeric_limits<int>::max();
  event.commands = {show, wait};
  return event;
}

} // namespace

PepperGuideScreen::PepperGuideScreen(TextureCache &textures, const GlyphFont &font,
                                     PepperGuideScript script, GuideMemory &memory,
                                     const std::string &from, const TalkTables &talk,
                                     const EventTuning &tuning, const FrameStyle &frame,
                                     const std::vector<MusicCredit> &credits, QuestSave quest)
    : textures_(textures), font_(font), guide_(std::move(script), memory, from, quest.cleared),
      pepper_(guide_event(guide_.script().guide)), player_(pepper_, talk, tuning, 19U),
      frame_(font, frame, credits, guide_.script().frame), quest_(std::move(quest)) {
  const PepperGuideScript &data = guide_.script();
  for (const auto &layer : data.layers) {
    static_cast<void>(textures_.get(layer.image));
  }
  for (const auto &layer : data.topic_layers) {
    static_cast<void>(textures_.get(layer.image));
  }
  const auto member = static_cast<std::size_t>(data.guide.character);
  if (member < talk.characters.size()) {
    for (const auto &image : talk.characters[member].images) {
      static_cast<void>(textures_.get(image));
    }
  }
  frame_.preload(textures_);
}

void PepperGuideScreen::tick(const InputFrame &input, const double tick_seconds) {
  guide_.update(tick_seconds, input);
  // Pepper takes no input: she only shows, fades in and blinks.
  player_.update(tick_seconds, InputFrame{});
  if (guide_.faded_in()) {
    frame_.start_riding_plate();
  }
  frame_.update(tick_seconds);
}

void PepperGuideScreen::draw(const Viewport &viewport) {
  clear_black(textures_);
  const PepperGuideScript &script = guide_.script();
  DrawList list(textures_, font_, viewport, guide_.seconds());
  for (const auto &layer : script.layers) {
    list.sprite(layer);
  }
  if (guide_.in_topic()) {
    for (const auto &layer : script.topic_layers) {
      list.sprite(layer);
    }
  }
  const auto rows = guide_.rows();
  if (static_cast<int>(guide_.questions().size()) > script.rows) {
    list.sprite(script.more_above);
    list.sprite(script.more_below);
  }
  Sprite cursor = script.cursor;
  cursor.position[1] += static_cast<float>(guide_.cursor_row()) * script.row_step;
  list.sprite(cursor);
  for (std::size_t row = 0; row < rows.size(); ++row) {
    const GuideQuestion &question = guide_.questions()[static_cast<std::size_t>(rows[row])];
    const float y = script.text[1] + static_cast<float>(row) * script.row_step;
    list.text(script.text_layer,
              layout_run(font_, glyph_run(font_, question.text), script.text[0], y),
              script.text_colour);
  }
  for (const auto &sprite : player_.frame().sprites) {
    list.sprite(sprite.sprite, sprite.alpha);
  }
  frame_.draw(list, quest_);
  list.flush();
  fill_black(textures_, guide_.black_alpha());
}

} // namespace phantomcrash
