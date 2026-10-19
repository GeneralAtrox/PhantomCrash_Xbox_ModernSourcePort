#pragma once

#include "desktop_robot_preview.hpp"
#include "desktop_sv_purchase.hpp"
#include "desktop_town_screen.hpp"
#include "phantomcrash/module_shop.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// Wild Machines' BUY MODULE and SELL MODULE (ModuleShop): the gears and the
// plate, the category grid sliding in beside its picture panel, the goods
// list and the chosen goods' stats in the windows popping open, and the
// notices. The money and stock it changes go back into the quest. The
// garage's STOCK MODULE also shows the listed part under the cursor on its
// own (RobotPreview, 0x03cb50).
class ModuleShopScreen final : public TownScreen {
public:
  [[nodiscard]] std::optional<MenuPresence> menu_presence() const override;
  ModuleShopScreen(TextureCache &textures, const GlyphFont &font, ModuleShop logic,
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
  void draw_grid(DrawList &list) const;
  void draw_side(DrawList &list) const;
  void draw_list(DrawList &list) const;
  void draw_rows(DrawList &list) const;
  void draw_chosen(DrawList &list, const ShopGoods &line) const;
  [[nodiscard]] std::vector<ExtraStatLine> chosen_extras(const ShopGoods *line) const;
  // The details' second page while it shows; none otherwise.
  [[nodiscard]] std::vector<ExtraStatLine> second_page(const ShopGoods *line) const;
  // How the chosen goods' details sit with their extra lines.
  struct DetailsFit {
    float grow{};     // canvas pixels the details windows grow down
    float raise{};    // canvas pixels the windows and all in them move up
    float step{};     // the lines' pitch
    float squeeze{1}; // the labels' art's height against the original's
    float lift{};     // how much higher the values sit, as the separators do
  };
  [[nodiscard]] DetailsFit details_fit(const ShopGoods *line) const;

  const NumberFont &number_font_;
  ModuleShop logic_;
  ShopNotice notice_;
  RobotPreview preview_;
};

} // namespace phantomcrash
