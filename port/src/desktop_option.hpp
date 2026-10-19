#pragma once

#include "desktop_frame.hpp"
#include "desktop_screen.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/option_menu.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

// The OPTION screen (OptionMenu): its list, the scrolling help line, the
// CURRENT MUSIC band (meter and credit) and the message boxes, drawn in the
// original's layer order; with `hud`, also the day and money, and a menu
// with a riding plate height shows the RIDING SV plate. The garage's system
// menu is one too. The frame style and credits outlive the screen.
class OptionScreen final : public Screen {
public:
  OptionScreen(TextureCache &textures, const GlyphFont &font, OptionScript script,
               MessageBoxStyle box, GameOptions options, const FrameStyle &frame,
               const std::vector<MusicCredit> &credits, const std::string &returning_from,
               QuestSave quest = {}, bool hud = false);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::string music() const override {
    return menu_.music();
  }
  void set_music(const std::string &file) override {
    band_.set_music(file);
  }
  void hear_music(const std::vector<float> &levels) override {
    band_.hear_music(levels);
  }
  [[nodiscard]] std::vector<std::string> take_sounds() override {
    return menu_.take_sounds();
  }
  [[nodiscard]] bool done() const override {
    return menu_.done();
  }
  [[nodiscard]] std::string next() const override {
    return menu_.next();
  }
  [[nodiscard]] const GameOptions *game_options() const override {
    return &menu_.options();
  }

private:
  TextureCache &textures_;
  const GlyphFont &font_;
  OptionMenu menu_;
  QuestSave quest_;
  std::vector<std::vector<int>> help_runs_;
  LocationFrame band_;
};

} // namespace phantomcrash
