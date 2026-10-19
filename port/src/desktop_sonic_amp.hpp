#pragma once

#include "desktop_town_screen.hpp"
#include "phantomcrash/sonic_amp.hpp"
#include "phantomcrash/story_event.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// SONIC AMP (SonicAmp): the menu along the bottom, the chart on the
// television, the grids of jackets to buy from, the actions and the clerk's
// dialog, and the preview of a track. The tracks bought and the money paid
// go back into the quest as they change, so the frame shows the money left.
class SonicAmpScreen final : public TownScreen {
public:
  [[nodiscard]] std::optional<MenuPresence> menu_presence() const override;
  SonicAmpScreen(TextureCache &textures, const GlyphFont &font, SonicAmpScript script,
                 const std::string &from, const FrameStyle &frame,
                 const std::vector<MusicCredit> &credits, QuestSave quest);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::string music() const override {
    return logic_.music_track();
  }
  [[nodiscard]] std::vector<std::string> take_sounds() override {
    return logic_.take_sounds();
  }
  [[nodiscard]] bool done() const override {
    return logic_.done();
  }
  [[nodiscard]] std::string next() const override {
    return logic_.next();
  }
  [[nodiscard]] const QuestSave *quest_save() const override {
    return logic_.done() ? &quest() : nullptr;
  }

private:
  void draw_menu(DrawList &list) const;
  void draw_chart(DrawList &list) const;
  void draw_grid(DrawList &list) const;
  void draw_pages(DrawList &list) const;
  void draw_actions(DrawList &list) const;
  void draw_preview(DrawList &list) const;
  void draw_number(DrawList &list, int value, float left, float top, int layer) const;
  [[nodiscard]] const MusicCredit *credit(int track) const;

  const NumberFont &number_font_;
  SonicAmp logic_;
  std::vector<EventText> profiles_;
};

} // namespace phantomcrash
