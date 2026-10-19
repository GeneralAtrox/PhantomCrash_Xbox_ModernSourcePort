#pragma once

#include "desktop_robot_preview.hpp"
#include "desktop_town_screen.hpp"
#include "phantomcrash/dealer.hpp"
#include "desktop_sv_purchase.hpp"
#include "phantomcrash/message_box.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// A SV dealer (Dealer): the panorama, the logo, the menu of the day's SVs,
// the profile, the trade window, the hangar carousel and the notices. The
// money and garage it changes go back into the quest as they change.
class DealerScreen final : public TownScreen {
public:
  [[nodiscard]] std::optional<MenuPresence> menu_presence() const override;
  DealerScreen(TextureCache &textures, const GlyphFont &font, Dealer logic,
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
  [[nodiscard]] int naming_stock() const override {
    return logic_.done() ? logic_.naming_stock() : -1;
  }

private:
  void draw_backdrop(DrawList &list, const Viewport &viewport) const;
  void draw_logo(DrawList &list) const;
  void draw_menu(DrawList &list) const;
  void draw_offer(DrawList &list) const;
  void draw_profile(DrawList &list) const;

  const NumberFont &number_font_;
  Dealer logic_;
  PurchaseOverlay purchase_;
  int &remembered_; // the menu item chosen, to come back to from SV SPEC or NAME ENTRY
  RobotPreview preview_;
};

} // namespace phantomcrash
