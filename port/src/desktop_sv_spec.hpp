#pragma once

#include "desktop_robot_preview.hpp"
#include "desktop_town_screen.hpp"
#include "phantomcrash/sv_spec.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// SV SPEC (SvSpec): the window with the SV's and its CHIP's names and the
// maker's badge, and the page shown, the vehicle's or the weapons'; the SV
// itself stands beside the window (RobotPreview, 0x03ba94 calling 0x08371b).
class SvSpecScreen final : public TownScreen {
public:
  SvSpecScreen(TextureCache &textures, const GlyphFont &font, SvSpec logic, const std::string &from,
               const FrameStyle &frame, const std::vector<MusicCredit> &credits, QuestSave quest,
               PreviewWorld world, const RobotPreviewScript &preview);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::vector<std::string> take_sounds() override {
    return logic_.take_sounds();
  }
  [[nodiscard]] bool done() const override {
    return logic_.done();
  }
  [[nodiscard]] std::string next() const override {
    return logic_.next();
  }

private:
  void draw_window(DrawList &list) const;
  void draw_vehicle(DrawList &list) const;
  void draw_weapons(DrawList &list) const;
  void draw_combat(DrawList &list) const;

  SvSpec logic_;
  RobotPreview preview_;
};

} // namespace phantomcrash
