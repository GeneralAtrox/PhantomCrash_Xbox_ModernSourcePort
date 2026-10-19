#pragma once

#include "desktop_records_trophies.hpp"
#include "desktop_town_screen.hpp"
#include "phantomcrash/records.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// RECORDS (Records): the menu, the record pages and CONVENTION RECORD
// (desktop_records_convention.cpp), over the town frame. The contest
// tables outlive the screen.
class RecordsScreen final : public TownScreen {
public:
  RecordsScreen(TextureCache &textures, const GlyphFont &font, RecordsScript script,
                const ContestTables &contests, StoryProgress progress, const std::string &from,
                const std::string &back, const FrameStyle &frame,
                const std::vector<MusicCredit> &credits, QuestSave quest, WorldRenderer *world);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::vector<std::string> take_sounds() override {
    return records_.take_sounds();
  }
  [[nodiscard]] bool done() const override {
    return records_.done();
  }
  [[nodiscard]] std::string next() const override {
    return records_.next();
  }

private:
  // Behind a page, faint and under its panel.
  void draw_menu(DrawList &list, bool behind) const;
  void draw_page(DrawList &list) const;
  void draw_row(DrawList &list, const RecordPage &page, int row) const;
  // CONVENTION RECORD's menu, and a venue's or REGULATION's grid.
  void draw_convention(DrawList &list) const;
  void draw_convention_items(DrawList &list, bool sliding) const;
  void draw_grid(DrawList &list) const;
  void draw_champion(DrawList &list, int contest, int slot) const;
  // A grid's won cells darkened and its empty ones marked, and the turning
  // trophies of the view shown (0x334fc).
  void draw_cells(DrawList &list) const;
  [[nodiscard]] std::vector<TrophyPlace> trophy_places() const;
  // Text left-aligned at (x, y), or right-aligned when `right`.
  void draw_text(DrawList &list, const std::string &text, float x, float y, int layer,
                 std::uint32_t colour, float alpha, bool right = false) const;

  const ContestTables &contests_;
  Records records_;
  std::vector<int> help_run_;
  std::optional<RecordsTrophyView> trophies_;
};

} // namespace phantomcrash
