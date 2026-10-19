#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/message_box.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <string>
#include <vector>

namespace phantomcrash {

// data/ui/screen_adjust.json (original scene 0).
struct ScreenAdjustScript : MenuBasics {
  Buttons reset_buttons{};      // put the picture back in the middle
  std::array<int, 2> minimum{}; // x, y
  std::array<int, 2> maximum{};
  std::string move_x_sound; // the horizontal step's sound (the vertical uses move_sound)
  // Drawn where they are whatever the offset, as the original draws them at
  // their place minus the offset. `layers` move with the picture.
  std::vector<Sprite> fixed;
  std::vector<std::string> question;
  int default_choice{1}; // 0 YES, 1 NO
};

// Throws std::runtime_error naming the document and entry at fault.
[[nodiscard]] ScreenAdjustScript parse_screen_adjust(const json::JsonValue &document);

// SCREEN ADJUST: the D-pad moves the whole picture (the game options'
// screen offset, applied as it changes), X centres it, and confirm asks
// whether to keep it; NO puts back the offset the screen started with.
// Either answer fades out to the option screen. Follows the original's tasks
// 0x131d0 (adjust) and 0x130ee (question).
class ScreenAdjustMenu final {
public:
  ScreenAdjustMenu(ScreenAdjustScript script, MessageBoxStyle box, GameOptions options);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] bool done() const noexcept {
    return fade_.done();
  }
  [[nodiscard]] const std::string &next() const {
    return fade_.next();
  }
  [[nodiscard]] std::vector<std::string> take_sounds();
  [[nodiscard]] const std::string &music() const noexcept {
    return script_.music;
  }

  [[nodiscard]] const ScreenAdjustScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const GameOptions &options() const noexcept {
    return options_;
  }
  [[nodiscard]] const std::array<int, 2> &offset() const noexcept {
    return options_.screen_offset;
  }
  [[nodiscard]] const MessageBox &box() const noexcept {
    return box_;
  }
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] float black_alpha() const noexcept {
    return fade_.black_alpha();
  }

private:
  void adjust(double tick_seconds, const InputFrame &input);
  void answer(double tick_seconds, const InputFrame &input);
  void sound(const std::string &name);

  ScreenAdjustScript script_;
  MessageBox box_;
  GameOptions options_;
  std::array<int, 2> original_{};
  ScreenFade fade_;
  CursorRepeat repeat_x_;
  CursorRepeat repeat_y_;
  CursorRepeat repeat_choice_;
  double seconds_{};
  std::vector<std::string> sounds_;
};

} // namespace phantomcrash
