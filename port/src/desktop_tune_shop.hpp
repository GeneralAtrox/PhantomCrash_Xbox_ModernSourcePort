#pragma once

#include "desktop_robot_preview.hpp"
#include "desktop_sv_purchase.hpp"
#include "desktop_town_screen.hpp"
#include "phantomcrash/tune_shop.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// PlusTech's CUSTOMIZE (TuneShop): the drawings scrolling behind the plate,
// the row of icons sliding in, the parts list with the chosen part's stats
// at the level chosen, the level meter and the notices. The money and the
// levels it changes go back into the quest.
class TuneShopScreen final : public TownScreen {
public:
  [[nodiscard]] std::optional<MenuPresence> menu_presence() const override;
  TuneShopScreen(TextureCache &textures, const GlyphFont &font, TuneShop logic,
                 const MessageBoxStyle &box, const std::string &from, const FrameStyle &frame,
                 const std::vector<MusicCredit> &credits, QuestSave quest, PreviewWorld world,
                 const RobotPreviewScript &preview);

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

private:
  void draw_backdrop(DrawList &list) const;
  void draw_items(DrawList &list) const;
  void draw_list(DrawList &list) const;
  void draw_chosen(DrawList &list) const;
  void draw_meter(DrawList &list) const;
  void draw_number(DrawList &list, const NumberStyle &style, std::int64_t value, int layer) const;

  const NumberFont &number_font_;
  TuneShop logic_;
  ShopNotice notice_;
  RobotPreview preview_;
};

} // namespace phantomcrash
