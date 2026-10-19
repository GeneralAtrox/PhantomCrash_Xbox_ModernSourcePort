#pragma once

#include "desktop_frame.hpp"
#include "desktop_screen.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/location_menu.hpp"
#include "phantomcrash/glyph_font.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

// A location's menu (LocationMenu: Club Wired, the garage): the background, the turning wheel, the
// help panel and its scrolling line, the lit wedge and the item's name, and
// the town frame (day, money, music meter and credit). The frame style and
// credits outlive the screen.
class LocationMenuScreen final : public Screen {
public:
  [[nodiscard]] std::optional<MenuPresence> menu_presence() const override;
  [[nodiscard]] std::optional<MenuPresence> peer_presence(const MenuPresence &peer) const override;
  LocationMenuScreen(TextureCache &textures, const GlyphFont &font, LocationMenuScript script,
                     const std::string &from, const FrameStyle &frame,
                     const std::vector<MusicCredit> &credits, QuestSave quest);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::string music() const override {
    return menu_.script().music;
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
    return menu_.take_sounds();
  }
  [[nodiscard]] bool done() const override {
    return menu_.done();
  }
  [[nodiscard]] std::string next() const override {
    return menu_.next();
  }

private:
  void pointer_input(const InputFrame &input);
  int item_at(const InputFrame &input);
  std::optional<std::array<float, 2>> drag_start_;
  float drag_angle_{};
  float drag_turn_{};
  int drag_steps_{};
  float wheel_steps_{};
  int pressed_item_{-1};
  bool dragged_{};
  TextureCache &textures_;
  const GlyphFont &font_;
  LocationMenu menu_;
  LocationFrame frame_;
  QuestSave quest_;
  std::vector<std::vector<int>> help_runs_;
};

} // namespace phantomcrash
