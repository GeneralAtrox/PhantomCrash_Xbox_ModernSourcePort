#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

enum class ButtonRowKind {
  choice,  // left and right pick one of the row's values
  binding, // pressing a button assigns the row's action to it
  reset,   // confirm restores the default assignments
  exit,
};

struct ButtonRow {
  std::string id;
  std::array<float, 2> bar{};       // where the row's bar is drawn
  std::optional<Sprite> bar_normal; // instead of the screen's bars (EXIT)
  std::optional<Sprite> bar_selected;
  Sprite label;
  HelpLine help;
  ButtonRowKind kind{ButtonRowKind::exit};
  std::string option;                 // choice: the GameOptions setting
  std::vector<Sprite> values;         // choice: one sprite per value
  std::array<float, 2> right_arrow{}; // choice: where the arrows sit
  std::array<float, 2> left_arrow{};
  int action{-1};                       // binding: index into control_actions
  std::array<float, 2> icon_position{}; // binding: the assigned button's icon
};

// data/ui/button_config.json (original scene 16).
struct ButtonConfigScript : MenuBasics, MenuTickers {
  Sprite bar_normal; // dimmed and below the selected bar (0x27d74)
  Sprite bar_selected;
  // Per bindable button (bindable_button_names order): its icon, placed by
  // the row, and its marker on the controller picture.
  std::array<Sprite, control_action_count> icons;
  std::array<Sprite, control_action_count> markers;
  std::vector<Sprite> right_arrow; // drawn together at the row's arrow place
  std::vector<Sprite> left_arrow;
  std::vector<ButtonRow> rows;
  int default_cursor{};
};

// Throws std::runtime_error naming the document and entry at fault.
[[nodiscard]] ButtonConfigScript parse_button_config(const json::JsonValue &document);

// BUTTON CONFIG (task 0x28189): the control type, view change and beginner
// mode, the button for each of eight actions, DEFAULT and EXIT. A button
// pressed on an action's row is assigned to it, and the action that had the
// button takes the row's old one, so the assignments stay one to one. The
// cancel button is one of the assignable buttons, so it does nothing else;
// EXIT is the only way out and keeps the changes.
class ButtonConfigMenu final {
public:
  ButtonConfigMenu(ButtonConfigScript script, GameOptions options);

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

  [[nodiscard]] const ButtonConfigScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] int cursor() const noexcept {
    return cursor_;
  }
  // A choice row's value.
  [[nodiscard]] int choice(int row) const;
  // The bindable button index an action row's action is assigned to.
  [[nodiscard]] int binding(int row) const;
  [[nodiscard]] double help_seconds() const {
    return help_.seconds(cursor_);
  }
  [[nodiscard]] const GameOptions &options() const noexcept {
    return options_;
  }
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] float black_alpha() const noexcept {
    return fade_.black_alpha();
  }

private:
  // The row whose bar the mouse is over.
  [[nodiscard]] std::optional<int> pointed_row(const InputFrame &input) const;
  void assign(int action, int button_index);
  void sound(const std::string &name);

  ButtonConfigScript script_;
  GameOptions options_;
  ScreenFade fade_;
  int cursor_{};
  HelpClocks help_;
  CursorRepeat repeat_;
  CursorRepeat choice_repeat_;
  double seconds_{};
  std::vector<std::string> sounds_;
};

} // namespace phantomcrash
