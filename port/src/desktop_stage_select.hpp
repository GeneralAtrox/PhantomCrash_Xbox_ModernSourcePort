#pragma once

#ifdef MessageBox
#undef MessageBox
#endif

#include "desktop_draw_list.hpp"
#include "desktop_screen.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/menu_cursor.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {

// Optional mod-provided arenas for the hangar's TEST DRIVE.
class StageSelectScreen final : public Screen {
public:
  StageSelectScreen(TextureCache &textures, const GlyphFont &font,
                    std::vector<std::pair<std::string, std::string>> choices, std::string cancel)
      : textures_(textures), font_(font), choices_(std::move(choices)), cancel_(std::move(cancel)) {
  }

  void tick(const InputFrame &input, double seconds) override {
    bool fresh = false;
    const int step = cursor_.step(seconds, input, button::up, button::down, fresh);
    selected_ = std::clamp(selected_ + step, 0, static_cast<int>(choices_.size()) - 1);
    if (input.any_pressed(button::cancel)) {
      next_ = cancel_;
    } else if (input.any_pressed(button::confirm)) {
      next_ = "test_drive/" + choices_[static_cast<std::size_t>(selected_)].first;
    }
  }
  void draw(const Viewport &viewport) override {
    clear_black(textures_);
    DrawList list(textures_, font_, viewport, 0);
    const auto text = [&](const std::string &line, float y, std::uint32_t colour) {
      list.text(1, layout_run(font_, glyph_run(font_, line), 75, y), colour);
    };
    text("TEST DRIVE - SELECT STAGE", 55, 0xffffff);
    const int first =
        std::clamp(selected_ - 4, 0, std::max(0, static_cast<int>(choices_.size()) - 9));
    for (int i = first; i < std::min(first + 9, static_cast<int>(choices_.size())); ++i) {
      text((i == selected_ ? "> " : "  ") + choices_[static_cast<std::size_t>(i)].second,
           105.F + (i - first) * 32.F, i == selected_ ? 0xfeb000 : 0xffffff);
    }
    text("A: DRIVE    B: BACK    UP / DOWN: STAGE", 425, 0xaaaaaa);
    list.flush();
  }
  [[nodiscard]] bool done() const override {
    return !next_.empty();
  }
  [[nodiscard]] std::string next() const override {
    return next_;
  }

private:
  TextureCache &textures_;
  const GlyphFont &font_;
  std::vector<std::pair<std::string, std::string>> choices_;
  std::string cancel_, next_;
  CursorRepeat cursor_{0.35, 0.08};
  int selected_{};
};

} // namespace phantomcrash
