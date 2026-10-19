#pragma once

#include "desktop_town_screen.hpp"
#include "phantomcrash/schedule.hpp"

#include <array>
#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// The contest schedule (Schedule): the calendar's cells, the cursor or
// EXIT, the contest's name running along the top, the INFORMATION popup and
// the town frame. The contest tables outlive the screen.
class ScheduleScreen final : public TownScreen {
public:
  ScheduleScreen(TextureCache &textures, const GlyphFont &font, ScheduleScript script,
                 const ContestTables &contests, const std::string &from, const std::string &back,
                 const FrameStyle &frame, const std::vector<MusicCredit> &credits, QuestSave quest,
                 const MessageBoxStyle &box);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::vector<std::string> take_sounds() override {
    return schedule_.take_sounds();
  }
  [[nodiscard]] bool done() const override {
    return schedule_.done();
  }
  [[nodiscard]] std::string next() const override {
    return schedule_.next();
  }
  // Set only once SKIP TO DAY leaves to sleep the last night.
  [[nodiscard]] const QuestSave *quest_save() const override {
    return schedule_.skipped_quest();
  }

private:
  void draw_cells(DrawList &list) const;
  void draw_days(DrawList &list) const;
  void draw_cursor(DrawList &list) const;
  void draw_popup(DrawList &list) const;
  // SKIP TO DAY beside EXIT, its question, and the opponents' level raise.
  void draw_skip(DrawList &list) const;
  // The top-left of a cell on the canvas.
  [[nodiscard]] std::array<float, 2> cell(int column, int row) const;

  const ContestTables &contests_;
  const NumberFont &fee_font_;
  Schedule schedule_;
};

} // namespace phantomcrash
