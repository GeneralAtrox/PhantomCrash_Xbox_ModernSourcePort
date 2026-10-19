#pragma once

#include "desktop_frame.hpp"
#include "desktop_screen.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/name_entry.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// NAME ENTRY (NameEntry): the keyboard page and the one fading out, the
// cursors, the name in its box, the controls box and its hints, the help
// line, the town frame (day, money, music meter and credit) and the no-name
// notice, in the original's layers. The frame style and credits outlive it.
class NameEntryScreen final : public Screen {
public:
  NameEntryScreen(TextureCache &textures, const GlyphFont &font, NameEntryScript script,
                  MessageBoxStyle box, QuestSave save, const FrameStyle &frame,
                  const std::vector<MusicCredit> &credits);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::string music() const override {
    return entry_.music();
  }
  [[nodiscard]] bool day_clicked(const InputFrame &input) const override {
    return frame_.day_clicked(input, entry_.save());
  }
  bool save_clicked(const InputFrame &input) override {
    return frame_.save_clicked(input);
  }
  void set_music(const std::string &file) override {
    frame_.set_music(file);
  }
  void hear_music(const std::vector<float> &levels) override {
    frame_.hear_music(levels);
  }
  [[nodiscard]] std::vector<std::string> take_sounds() override {
    return entry_.take_sounds();
  }
  [[nodiscard]] bool wants_text() const override {
    return entry_.takes_text();
  }
  [[nodiscard]] bool done() const override {
    return entry_.done();
  }
  [[nodiscard]] std::string next() const override {
    return entry_.next();
  }
  [[nodiscard]] const QuestSave *quest_save() const override {
    return &entry_.save();
  }

private:
  void draw_page(DrawList &list, int page, float alpha) const;
  void draw_cursors(DrawList &list) const;

  TextureCache &textures_;
  const GlyphFont &font_;
  NameEntry entry_;
  LocationFrame frame_;
  std::vector<std::vector<int>> page_glyphs_; // per page, row by row
  std::vector<std::vector<int>> help_runs_;
};

} // namespace phantomcrash
