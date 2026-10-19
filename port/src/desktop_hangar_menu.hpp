#pragma once

#include "desktop_robot_preview.hpp"
#include "desktop_sv_purchase.hpp"
#include "desktop_town_screen.hpp"
#include "phantomcrash/hangar_menu.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// The garage's HANGAR (HangarMenu): the cards on their wheel, the numbers'
// plates, a hangar's menu and the notices. The money, the hangars bought
// and the Scoobee ridden go back into the quest; `remembered` keeps the
// hangar chosen for coming back from the screens its menu opens. The
// Scoobees near the chosen card stand on their cards.
class HangarMenuScreen final : public TownScreen {
public:
  [[nodiscard]] std::optional<MenuPresence> menu_presence() const override;
  HangarMenuScreen(TextureCache &textures, const GlyphFont &font, HangarMenu logic,
                   const MessageBoxStyle &box, const std::string &from, const FrameStyle &frame,
                   const std::vector<MusicCredit> &credits, QuestSave quest, int &remembered,
                   PreviewWorld world, const RobotPreviewScript &preview);

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
  [[nodiscard]] int kept_hangar() const override {
    return logic_.done() ? logic_.kept_hangar() : -1;
  }

private:
  void draw_card(DrawList &list, int card) const;
  void draw_hangar(DrawList &list, int hangar, float x) const;
  void draw_plates(DrawList &list) const;
  void draw_menu(DrawList &list) const;
  void tick_robots(double tick_seconds);

  const NumberFont &number_font_;
  HangarMenu logic_;
  ShopNotice notice_;
  int &remembered_;
  PreviewPlacement home_; // the preview's own place (robot_preview.json's hangar)
  std::vector<std::unique_ptr<RobotPreview>> robots_; // by hangar
};

} // namespace phantomcrash
