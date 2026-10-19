#include "desktop_talk.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_event_text.hpp"

#include <utility>

namespace phantomcrash {

std::optional<SceneProgress> TalkScreen::scene_progress() const {
  const StoryEvent &event = scene_.event();
  return SceneProgress{"Story event (scene " + std::to_string(event.scene) + ")",
                       static_cast<int>(scene_.command()), static_cast<int>(event.commands.size()),
                       "command"};
}

std::string talk_music(const StoryEvent &event) {
  if (event.music.empty() || event.music == "keep") {
    return event.music;
  }
  return "sound/" + event.music;
}

TalkScreen::TalkScreen(TextureCache &textures, const GlyphFont &font, const StoryEvent &event,
                       const TalkTables &talk, EventTuning tuning, std::string next,
                       const std::string &player_name, const FrameStyle &frame,
                       const std::vector<MusicCredit> &credits, QuestSave quest)
    : textures_(textures), font_(font),
      scene_(event, talk, tuning, std::move(next), static_cast<std::uint32_t>(event.scene)),
      frame_(font, frame, credits, tuning.frame), quest_(std::move(quest)),
      player_name_run_(glyph_run(font, player_name)) {
  // Load the textures now rather than on the first frame that needs them.
  frame_.preload(textures_);
  if (const TalkBackground *background = scene_.background()) {
    static_cast<void>(textures_.get(background->back.image));
    static_cast<void>(textures_.get(background->front.image));
  }
  for (const auto &command : event.commands) {
    if (command.op == EventOp::sprite && !command.sprite.image.empty()) {
      static_cast<void>(textures_.get(command.sprite.image));
    }
  }
  for (const int member : event.cast) {
    if (member >= 0 && static_cast<std::size_t>(member) < talk.characters.size()) {
      for (const auto &image : talk.characters[static_cast<std::size_t>(member)].images) {
        static_cast<void>(textures_.get(image));
      }
    }
  }
}

std::string TalkScreen::music() const {
  return talk_music(scene_.event());
}

void TalkScreen::tick(const InputFrame &input, const double tick_seconds) {
  scene_.update(tick_seconds, input);
  frame_.update(tick_seconds);
}

void TalkScreen::draw_text(DrawList &list, const TalkText &shown) const {
  draw_event_text(list, font_, shown, scene_.seconds(), player_name_run_);
}

void TalkScreen::draw(const Viewport &viewport) {
  clear_black(textures_);
  DrawList list(textures_, font_, viewport, scene_.seconds());
  if (const TalkBackground *background = scene_.background()) {
    list.sprite(background->back);
    list.sprite(background->front);
  }
  if (const TalkFrame *frame = scene_.frame()) {
    for (const auto &sprite : frame->sprites) {
      list.sprite(sprite.sprite, sprite.alpha);
    }
    for (const auto &text : frame->texts) {
      draw_text(list, text);
    }
  }
  frame_.draw(list, quest_);
  list.flush();
  fill_black(textures_, scene_.black_alpha());
}

} // namespace phantomcrash
