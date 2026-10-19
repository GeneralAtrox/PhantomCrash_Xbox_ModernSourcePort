#pragma once

#include "desktop_sv_purchase.hpp"
#include "desktop_town_screen.hpp"
#include "phantomcrash/chip_shop.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// Wild Machines' BUY CHIP and SELL CHIP (ChipShop): the window popping
// open, the scanlines rolling over it, the cards fading in and sliding, the
// chosen CHIP's details, the dialog and the notices. The money and stock it
// changes go back into the quest as they change.
class ChipShopScreen final : public TownScreen {
public:
  [[nodiscard]] std::optional<MenuPresence> menu_presence() const override;
  ChipShopScreen(TextureCache &textures, const GlyphFont &font, ChipShop logic,
                 const MessageBoxStyle &box, const std::string &from, const FrameStyle &frame,
                 const std::vector<MusicCredit> &credits, QuestSave quest, int &remembered);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::vector<std::string> take_sounds() override;
  [[nodiscard]] bool done() const override {
    return logic_.done();
  }
  [[nodiscard]] std::string next() const override {
    return logic_.next();
  }
  [[nodiscard]] const QuestSave *quest_save() const override {
    return logic_.done() ? &quest() : nullptr;
  }
  [[nodiscard]] int naming_stock() const override {
    return logic_.done() ? logic_.naming_stock() : -1;
  }

private:
  void draw_window(DrawList &list) const;
  void draw_cards(DrawList &list) const;
  void draw_details(DrawList &list) const;
  void draw_counter(DrawList &list) const;

  const NumberFont &number_font_;
  ChipShop logic_;
  ShopNotice notice_;
  int &remembered_; // the CHIP chosen, to come back to from NAME ENTRY
};

} // namespace phantomcrash
