#pragma once

#include "desktop_frame.hpp"
#include "desktop_screen.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/event_player.hpp"
#include "phantomcrash/glyph_font.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// A talk scene (TalkScene): the event's background, then the script's
// window, portraits, characters and typed text over it, the town frame (day,
// money, music meter and credit), and the fades. The event, tables, frame
// style and credits outlive the screen.
class TalkScreen final : public Screen {
public:
  TalkScreen(TextureCache &textures, const GlyphFont &font, const StoryEvent &event,
             const TalkTables &talk, EventTuning tuning, std::string next,
             const std::string &player_name, const FrameStyle &frame,
             const std::vector<MusicCredit> &credits, QuestSave quest);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::string music() const override;
  [[nodiscard]] float music_volume() const override {
    return scene_.event().music_volume;
  }
  [[nodiscard]] bool day_clicked(const InputFrame &input) const override {
    return frame_.day_clicked(input, quest_);
  }
  bool save_clicked(const InputFrame &input) override {
    return frame_.save_clicked(input);
  }
  void set_music(const std::string &file) override {
    frame_.set_music(file);
  }
  void hear_music(const std::vector<float> &levels) override {
    frame_.hear_music(levels);
  }
  [[nodiscard]] AudioBus sound_bus() const override {
    return AudioBus::dialogue;
  }
  [[nodiscard]] std::vector<std::string> take_sounds() override {
    return scene_.take_sounds();
  }
  [[nodiscard]] bool done() const override {
    return scene_.done();
  }
  // The event's command reached of its commands.
  [[nodiscard]] std::optional<SceneProgress> scene_progress() const override;
  [[nodiscard]] std::string next() const override {
    return scene_.next();
  }

private:
  void draw_text(DrawList &list, const TalkText &shown) const;

  TextureCache &textures_;
  const GlyphFont &font_;
  TalkScene scene_;
  LocationFrame frame_;
  QuestSave quest_;
  std::vector<int> player_name_run_;
};

// The music a talk scene plays: "sound/<file>", "keep", or empty.
[[nodiscard]] std::string talk_music(const StoryEvent &event);

} // namespace phantomcrash
