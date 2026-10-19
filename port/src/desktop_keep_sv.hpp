#pragma once

#include "desktop_sv_purchase.hpp"
#include "desktop_town_screen.hpp"
#include "phantomcrash/keep_sv.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// KEEP SV (KeepSv): the opening, the row of kept Scoobees' cards with the
// chosen one's name and date, SAVE and EXIT, and the message box with the
// popups' options over it.
class KeepSvScreen final : public TownScreen {
public:
  KeepSvScreen(TextureCache &textures, const GlyphFont &font, KeepSv logic,
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
  void draw_intro(DrawList &list) const;
  void draw_slots(DrawList &list) const;
  void draw_columns(DrawList &list) const;
  void draw_details(DrawList &list) const;
  void draw_popup(DrawList &list) const;
  [[nodiscard]] const ChoiceSprites *card(int index, int slot) const;
  [[nodiscard]] float title_alpha() const;

  KeepSv logic_;
  ShopNotice notice_;
};

} // namespace phantomcrash
