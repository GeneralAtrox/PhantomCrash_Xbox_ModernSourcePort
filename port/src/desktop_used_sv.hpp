#pragma once

#include "desktop_robot_preview.hpp"
#include "desktop_sv_purchase.hpp"
#include "desktop_town_screen.hpp"
#include "phantomcrash/used_sv.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// Wild Machines' used SVs (UsedSv): the turning gears, the USED-SV plate,
// the SVs' icons sliding in, the SV chosen standing on the right, the
// info panel over it, the trade
// window, the hangar carousel and the notices. The money and garage it
// changes go back into the quest as they change.
class UsedSvScreen final : public TownScreen {
public:
  UsedSvScreen(TextureCache &textures, const GlyphFont &font, UsedSv logic,
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
  void draw_backdrop(DrawList &list) const;
  void draw_slots(DrawList &list) const;
  void draw_info(DrawList &list) const;

  const NumberFont &number_font_;
  UsedSv logic_;
  PurchaseOverlay purchase_;
  int &remembered_; // the SV chosen, to come back to from SV SPEC or NAME ENTRY
  RobotPreview preview_;
};

} // namespace phantomcrash
