#pragma once

#include "desktop_sv_purchase.hpp"
#include "desktop_town_screen.hpp"
#include "phantomcrash/vs_mode.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// VS MODE (VsMode): the players' panels through the entry and the Scoobee
// select, the Are you ready? window, the abandon question, and the area
// select with its time-zone window (desktop_vs_mode_area.cpp).
class VsModeScreen final : public TownScreen {
public:
  VsModeScreen(TextureCache &textures, const GlyphFont &font, VsMode logic,
               const MessageBoxStyle &box, const std::string &from, const FrameStyle &frame,
               const std::vector<MusicCredit> &credits, QuestSave quest);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::vector<std::string> take_sounds() override;
  [[nodiscard]] bool done() const override {
    return logic_.done();
  }
  [[nodiscard]] std::string next() const override {
    return logic_.next();
  }

private:
  void draw_popup(DrawList &list) const;
  void draw_player(DrawList &list, int port) const;
  void draw_entry(DrawList &list, int port) const;
  void draw_strip(DrawList &list, int port) const;
  void draw_cards(DrawList &list, int port) const;
  void draw_chosen(DrawList &list, int port) const;
  void draw_area(DrawList &list) const;
  void draw_area_items(DrawList &list) const;
  void draw_pictures(DrawList &list) const;
  void draw_zone(DrawList &list) const;
  void draw_name(DrawList &list, const std::string &name, const std::array<float, 2> &at,
                 int layer) const;
  [[nodiscard]] const ChoiceSprites &card(int index) const;
  [[nodiscard]] const Sprite *icon(int index) const;

  VsMode logic_;
  ShopNotice notice_;
};

} // namespace phantomcrash
