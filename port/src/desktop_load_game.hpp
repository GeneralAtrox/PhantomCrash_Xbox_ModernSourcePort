#pragma once

#include "desktop_frame.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "desktop_screen.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/load_game.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

// The LOAD GAME screen (LoadGame): the background, the three slots with
// each save's date and time, EXIT, the chosen save's details, the help line,
// the CURRENT MUSIC meter and credit, and its notices. The frame style and
// credits outlive the screen.
class LoadGameScreen final : public Screen {
public:
  LoadGameScreen(TextureCache &textures, const GlyphFont &font, const PartTables &parts,
                 LoadGameScript script, std::vector<SaveSlot> slots, const MessageBoxStyle &box,
                 const FrameStyle &frame, const std::vector<MusicCredit> &credits,
                 QuestSave quest = {}, bool hud = false, SaveLoadHooks hooks = {},
                 bool unreadable = false);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::string music() const override {
    return menu_.script().music;
  }
  [[nodiscard]] bool day_clicked(const InputFrame &input) const override {
    return frame_.day_clicked(input, quest_);
  }
  void set_music(const std::string &file) override {
    frame_.set_music(file);
  }
  void hear_music(const std::vector<float> &levels) override {
    frame_.hear_music(levels);
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

private:
  std::optional<Sprite> profile_image(const QuestSave &quest, const SaveSlotStyle &slot) const;
  void draw_details(DrawList &list, const QuestSave &quest) const;

  TextureCache &textures_;
  const GlyphFont &font_;
  const PartTables &parts_;
  const FrameStyle &style_;
  LoadGame menu_;
  QuestSave quest_; // the day and money, drawn with `hud`
  LocationFrame frame_;
  std::vector<int> help_run_;
};

} // namespace phantomcrash
