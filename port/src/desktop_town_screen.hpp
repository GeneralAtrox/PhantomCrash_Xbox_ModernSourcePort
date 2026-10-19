#pragma once

#include "desktop_frame.hpp"
#include "desktop_screen.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/shop_goods.hpp"
#include "phantomcrash/town_screen.hpp"

#include <functional>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// The base of the town's own screens (TownBasics): the music chosen by the
// screen it was opened from, the town frame (day, money, RIDING SV plate,
// music meter and credit) following that music, and a frame drawn as
// black, the fixed layers, the screen's own sprites, the frame and the
// fade. The font, frame style and credits outlive the screen.
class TownScreen : public Screen {
public:
  [[nodiscard]] const QuestSave *session_save() const override {
    return &quest_;
  }
  [[nodiscard]] const QuestSave *pending_quest() const override {
    return &quest_;
  }
  [[nodiscard]] std::string music() const override {
    return music_;
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

protected:
  TownScreen(TextureCache &textures, const GlyphFont &font, const TownBasics &basics,
             const std::string &from, const FrameStyle &frame,
             const std::vector<MusicCredit> &credits, QuestSave quest);

  // Advances the frame; the RIDING SV plate slides in once faded in.
  void tick_frame(double tick_seconds, bool faded_in);
  // Draws the frame: `content` goes between the fixed layers and the town
  // frame, all in one layered list, and the fade covers the lot.
  void draw_town(const Viewport &viewport, double seconds, float black_alpha,
                 const std::function<void(DrawList &)> &content);

  [[nodiscard]] const QuestSave &quest() const noexcept {
    return quest_;
  }
  void set_quest(QuestSave quest) {
    quest_ = std::move(quest);
  }
  // The day and money's style: its font and "NY" write the shops' prices.
  [[nodiscard]] const HudStyle &hud() const noexcept {
    return frame_.style().hud;
  }
  // X SORT NAME and Y SORT PRICE over a goods list taking input (0x0178e0).
  void draw_sort_hints(DrawList &list, const GoodsList &goods) const;
  InputFrame sort_pointer(InputFrame input, Buttons name, Buttons price) const;

  TextureCache &textures_;
  const GlyphFont &font_;

private:
  LocationFrame frame_;
  QuestSave quest_;
  std::string music_;
  std::vector<Sprite> layers_;
};

} // namespace phantomcrash
