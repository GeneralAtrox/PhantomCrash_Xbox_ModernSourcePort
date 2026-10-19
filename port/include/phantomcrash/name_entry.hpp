#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/message_box.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <map>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace phantomcrash {

// One page of the keyboard: a grid of characters (none on the End page).
struct NameEntryPage {
  std::string id;
  HelpLine help;
  std::vector<std::vector<std::string>> rows; // each row's characters, as UTF-8
};

// The screen's timed effects; rates are per second of the 60 Hz original.
struct NameEntryAnimation {
  double intro_per_second{3.0};
  double slide_after{0.5};
  float slide_from{640.0F};
  float slide_to{289.0F};
  float slide_per_second{1800.0F};
  double hints_per_second{6.0};
  float grow_below_x{350.0F};
  int grow_steps{10};
  int grow_widen_steps{5};
  float grow_overshoot{1.2F};
  float grow_start_height{0.05F};
  double page_per_second{6.0};
  double pulse_per_second{3.0};
  float pulse_alpha{0.502F};
};

// Where things are drawn.
struct NameEntryLayout {
  std::vector<Sprite> intro_layers; // the grid frame and tabs, faded in
  Sprite slide_box;
  std::vector<Sprite> hints;
  Sprite name_box;
  Sprite label;
  // The label naming something other than the player (0x1f96c0, by NAME
  // ENTRY's mode): "chip" (mode 2), "program" (mode 3).
  std::map<std::string, Sprite> labels;
  std::array<float, 2> name_position{};
  float name_advance{18.0F};
  std::uint32_t name_colour{0xffffff};
  int name_layer{4};
  std::array<float, 2> grid_position{};
  std::array<float, 2> grid_step{};
  std::uint32_t grid_colour{0xc0c0c0};
  int grid_layer{2};
  Sprite grid_cursor;
  Sprite tab_cursor;
  Sprite name_cursor;
  float tab_step{};
};

// data/ui/name_entry.json (original scene 22).
struct NameEntryScript : MenuBasics {
  TickerStyle help;
  FramePlacement frame; // the day, money, music meter and credit
  Buttons delete_buttons{};
  Buttons space_buttons{};
  Buttons page_back_buttons{};
  Buttons page_forward_buttons{};
  std::string move_x_sound;
  std::string page_sound;
  std::string delete_sound;
  int max_length{8};
  // The keyboard (a port addition): a typed character with no key of its
  // own types the key named here, ahead of its full-width form.
  std::map<std::string, std::string> typed_aliases;
  std::vector<NameEntryPage> pages; // the last is End
  int columns{};
  int rows{};
  std::vector<std::string> no_name;
  NameEntryAnimation animation;
  NameEntryLayout layout;
};

// Throws std::runtime_error naming the document and entry at fault.
[[nodiscard]] NameEntryScript parse_name_entry(const json::JsonValue &document);

// UTF-8 text as its characters.
[[nodiscard]] std::vector<std::string> split_characters(std::string_view text);

// NAME ENTRY: a cursor on a grid of characters with a row of page tabs
// below it, typing into a name of up to max_length characters. Ending with
// a name of only spaces shows the no-name notice; otherwise the screen fades
// out to exit_to with the name in the save. Follows the original's tasks
// 0x2cd21 (controls), 0x2d194 (page buttons) and 0x2d5f9 (end).
class NameEntry final {
public:
  NameEntry(NameEntryScript script, MessageBoxStyle box, QuestSave save);

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
  // The save, with the name once the entry has ended.
  [[nodiscard]] const QuestSave &save() const noexcept {
    return save_;
  }

  [[nodiscard]] const NameEntryScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const MessageBox &box() const noexcept {
    return box_;
  }
  [[nodiscard]] const std::vector<std::string> &name() const noexcept {
    return name_;
  }
  [[nodiscard]] int page() const noexcept {
    return page_;
  }
  // The page shown before this one, fading out, or -1.
  [[nodiscard]] int previous_page() const noexcept {
    return previous_ == page_ ? -1 : previous_;
  }
  [[nodiscard]] int row() const noexcept {
    return row_;
  }
  [[nodiscard]] int column() const noexcept {
    return column_;
  }
  [[nodiscard]] bool on_tabs() const noexcept {
    return tabs_;
  }
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] double help_seconds() const {
    return help_.seconds(page_);
  }
  [[nodiscard]] float black_alpha() const noexcept {
    return fade_.black_alpha();
  }

  // The animation now: the grid frame and tabs' opacity, the controls box's
  // x, its hints' opacity, the name box's scale (x, y), whether the label
  // and name show, the page characters' opacity and the cursors' opacity.
  [[nodiscard]] float intro() const noexcept {
    return static_cast<float>(intro_);
  }
  [[nodiscard]] float slide_x() const noexcept {
    return slide_x_;
  }
  [[nodiscard]] float hints_alpha() const noexcept {
    return static_cast<float>(hints_);
  }
  [[nodiscard]] std::array<float, 2> name_box_scale() const noexcept;
  [[nodiscard]] bool name_shown() const noexcept;
  [[nodiscard]] float page_alpha() const noexcept {
    return static_cast<float>(page_alpha_);
  }
  [[nodiscard]] float cursor_alpha() const noexcept;
  // Whether the keyboard types into the name now: faded in, not leaving and
  // without the notice open.
  [[nodiscard]] bool takes_text() const noexcept {
    return !done() && !fade_.fading_in() && !fade_.leaving() && !box_.is_open();
  }

private:
  [[nodiscard]] int end_page() const noexcept {
    return static_cast<int>(script_.pages.size()) - 1;
  }
  [[nodiscard]] int tab_row() const noexcept {
    return script_.rows;
  }
  void animate(double tick_seconds);
  void controls(double tick_seconds, const InputFrame &input);
  void move_grid(double tick_seconds, const InputFrame &input);
  void move_tabs(double tick_seconds, const InputFrame &input);
  void page_buttons(double tick_seconds, const InputFrame &input);
  void change_page(int page);
  void keyboard(const InputFrame &input);
  void type_key(const std::string &character);
  void type(const std::string &character);
  void finish();
  void sound(const std::string &name);

  NameEntryScript script_;
  MessageBox box_;
  QuestSave save_;
  ScreenFade fade_;
  HelpClocks help_;
  CursorRepeat repeat_x_;
  CursorRepeat repeat_y_;
  CursorRepeat repeat_page_;
  CursorRepeat repeat_edit_; // delete back, space forward
  std::vector<std::string> name_;
  int page_{};
  int previous_{};
  int row_{};
  int column_{};
  bool tabs_{};
  int tab_row_cursor_{}; // the tab row's vertical cursor; moving it up leaves the tabs
  int saved_row_{};      // the grid row to go back to from End
  double intro_{};
  float slide_x_{};
  double hints_{};
  double grow_{}; // seconds the name box has grown
  double page_alpha_{};
  double pulse_{};
  double seconds_{};
  std::vector<std::string> sounds_;
};

} // namespace phantomcrash
