#pragma once

#include "desktop_frame.hpp"
#include "desktop_screen.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/event_player.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/pepper_guide.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

// Pepper's guide (PepperGuide): the question window and its list, Pepper
// (played by an EventPlayer, so she fades in and blinks as in a talk scene)
// and the town frame. The talk tables, frame style, credits and memory
// outlive the screen.
class PepperGuideScreen final : public Screen {
public:
  PepperGuideScreen(TextureCache &textures, const GlyphFont &font, PepperGuideScript script,
                    GuideMemory &memory, const std::string &from, const TalkTables &talk,
                    const EventTuning &tuning, const FrameStyle &frame,
                    const std::vector<MusicCredit> &credits, QuestSave quest);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::string music() const override {
    return guide_.script().music;
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
  [[nodiscard]] std::vector<std::string> take_sounds() override {
    return guide_.take_sounds();
  }
  [[nodiscard]] bool done() const override {
    return guide_.done();
  }
  [[nodiscard]] std::string next() const override {
    return guide_.next();
  }

private:
  TextureCache &textures_;
  const GlyphFont &font_;
  PepperGuide guide_;
  StoryEvent pepper_; // her one character command, held there
  EventPlayer player_;
  LocationFrame frame_;
  QuestSave quest_;
};

} // namespace phantomcrash
