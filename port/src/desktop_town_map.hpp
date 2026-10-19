#pragma once

#include "desktop_frame.hpp"
#include "desktop_screen.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/town_map.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

// The town map (TownMap): the map, the chosen place's number, arrows and
// sign, the rider, the scrolling help line and the town frame (day, money,
// RIDING SV plate, music meter and credit). The mouse chooses and opens
// places and spins the rider. The frame style and credits outlive the
// screen.
class TownMapScreen final : public Screen {
public:
  [[nodiscard]] std::optional<MenuPresence> menu_presence() const override;
  TownMapScreen(TextureCache &textures, const GlyphFont &font, TownMapScript script,
                const std::string &from, const FrameStyle &frame,
                const std::vector<MusicCredit> &credits, QuestSave quest);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::string music() const override {
    return map_.script().music;
  }
  [[nodiscard]] bool day_clicked(const InputFrame &input) const override {
    return frame_.day_clicked(input, map_.quest());
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
    return map_.take_sounds();
  }
  [[nodiscard]] bool done() const override {
    return map_.done();
  }
  [[nodiscard]] std::string next() const override {
    return map_.next();
  }
  [[nodiscard]] const QuestSave *quest_save() const override {
    return map_.quest_changed() ? &map_.quest() : nullptr;
  }

private:
  TextureCache &textures_;
  const GlyphFont &font_;
  TownMap map_;
  LocationFrame frame_;
  std::vector<std::vector<int>> help_runs_;
};

} // namespace phantomcrash
