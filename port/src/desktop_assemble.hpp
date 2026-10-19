#pragma once

#include "desktop_robot_preview.hpp"
#include "desktop_shop_list.hpp"
#include "desktop_sv_purchase.hpp"
#include "desktop_town_screen.hpp"
#include "phantomcrash/assemble.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// The hangar's ASSEMBLE (Assemble): the grid, the hangar's panel, the side
// choice, the list with a part's details, the notices. The fitting goes
// back into the quest; NAME ENTRY is told which hangar it names.
class AssembleScreen final : public TownScreen {
public:
  AssembleScreen(TextureCache &textures, const GlyphFont &font, Assemble logic,
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
  [[nodiscard]] int naming_hangar() const override {
    return logic_.done() ? logic_.naming_hangar() : -1;
  }

private:
  void draw_grid(DrawList &list) const;
  void draw_panel(DrawList &list) const;
  void draw_equipment(DrawList &list) const;
  void draw_sides(DrawList &list) const;
  void draw_list(DrawList &list) const;
  void draw_details(DrawList &list, const ShopGoods &line) const;
  void draw_picture(DrawList &list, const Goods &goods) const;
  void draw_second_page(DrawList &list, const std::vector<ExtraStatLine> &lines, const Sprite &art,
                        PartStatsStyle stats) const;
  void draw_totals(DrawList &list, const ShopGoods &line, float alpha) const;
  void draw_value(DrawList &list, double before, double after, int decimals, float right, float y,
                  float alpha) const;
  [[nodiscard]] std::vector<GoodsRowLook> row_looks() const;
  [[nodiscard]] GoodsRowLook row_look(const ShopGoods &line) const;
  void show_robot();

  Assemble logic_;
  ShopNotice notice_;
  RobotPreview preview_; // the hangar's robot turning in the panel (0x0836a1)
};

} // namespace phantomcrash
