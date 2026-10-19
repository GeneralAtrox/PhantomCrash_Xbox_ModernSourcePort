#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

struct ChoiceSprites {
  Sprite normal;
  Sprite selected; // drawn over `normal` while chosen
};

// "normal" and "selected"; the selected one sits where the normal one does
// unless it gives a position. Throws std::runtime_error naming the member.
[[nodiscard]] ChoiceSprites parse_choice(const json::JsonValue &value);

// The look of the game's SYSTEM INFORMATION window (data/ui/message_box.json;
// the original's 0xbec0f, 0xbe09e and 0xbdfbd). It grows from a small scale,
// playing a sound, and shows its lines and buttons once it is full size.
struct MessageBoxStyle {
  Sprite window; // centred on its position; its size is scaled while it grows
  float start_scale{0.1F};
  std::array<float, 2> grow_per_second{}; // scale added per second, x and y
  std::string open_sound;
  std::array<float, 2> text_position{}; // first line's top-left
  float line_height{18.0F};
  std::uint32_t text_colour{0xffffff};
  int text_layer{0x108};
  ChoiceSprites yes;
  ChoiceSprites no;
  ChoiceSprites ok;
};

// Throws std::runtime_error naming the entry at fault.
[[nodiscard]] MessageBoxStyle parse_message_box(const json::JsonValue &document);

// One message box on screen. A question offers YES (choice 0) and NO
// (choice 1); a notice only OK; a plain box only its lines, for a screen
// that draws its own options over it (0xbdfac). The owner handles input
// and sounds.
class MessageBox final {
public:
  enum class Kind { question, notice, plain };

  explicit MessageBox(MessageBoxStyle style) : style_(std::move(style)) {}

  void open(Kind kind, std::vector<std::string> lines, int choice = 0);
  void close() noexcept {
    open_ = false;
  }
  // Grows the window; once per logic tick.
  void update(double tick_seconds);

  [[nodiscard]] bool is_open() const noexcept {
    return open_;
  }
  // The window's scale now (x, y), and whether it has reached full size and
  // shows its contents.
  [[nodiscard]] std::array<float, 2> scale() const noexcept;
  [[nodiscard]] bool grown() const noexcept;

  [[nodiscard]] Kind kind() const noexcept {
    return kind_;
  }
  [[nodiscard]] const std::vector<std::string> &lines() const noexcept {
    return lines_;
  }
  [[nodiscard]] int choice() const noexcept {
    return choice_;
  }
  void set_choice(int choice) noexcept {
    choice_ = choice;
  }
  [[nodiscard]] const MessageBoxStyle &style() const noexcept {
    return style_;
  }

private:
  MessageBoxStyle style_;
  Kind kind_{Kind::notice};
  std::vector<std::string> lines_;
  int choice_{};
  bool open_{};
  double elapsed_{};
};

} // namespace phantomcrash
