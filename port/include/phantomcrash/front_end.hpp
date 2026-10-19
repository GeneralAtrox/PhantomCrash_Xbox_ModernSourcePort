#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// data/ui/title.json (original scene 40).
struct TitleScript {
  std::vector<Sprite> layers;
  std::string music;
  double fade_in{};
  Buttons start_buttons{};
  std::string start_sound;
  double idle_seconds{};
  double idle_fade_out{};
  std::string idle_goto;
};

struct MenuItem {
  std::string id;
  std::string label; // Code-drawn button label; empty retains a mod's sprite artwork.
  Sprite normal;
  Sprite selected; // drawn over `normal` while the cursor is on the item
  std::string submenu;
  std::string go_to;
  int requires_controllers{};
  std::uint32_t unavailable_colour{0x404040};
};

struct Menu {
  std::vector<MenuItem> items;
  std::vector<Sprite> hints;
  int default_cursor{};
};

// data/ui/main_menu.json (original scene 21).
struct MainMenuScript {
  std::string music;
  Menu main;
  std::map<std::string, Menu> submenus;
  double fade_in{};
  double fade_out{};
  double idle_seconds{};
  bool idle_resets_on_input{};
  Buttons confirm_buttons{};
  Buttons cancel_buttons{};
  double repeat_delay{};
  double repeat_interval{};
  std::string move_sound;
  std::string select_sound;
  std::string cancel_sound;
  std::string unavailable_sound;
};

// Throw std::runtime_error naming the document and entry at fault.
[[nodiscard]] TitleScript parse_title(const json::JsonValue &document);
[[nodiscard]] MainMenuScript parse_main_menu(const json::JsonValue &document);

// The title screen and main menu as one screen, so the background keeps
// moving between them and the music carries on (the original also goes from
// one to the other without a fade). Runs on the logic tick; the host draws
// from the accessors and plays take_sounds().
class FrontEnd final {
public:
  enum class Mode { title, menu };

  // Starts on the title, fading in. Use `open_menu_from` to start on the main
  // menu instead, returning from the screen an item led to: the menu fades in
  // with the cursor on that item.
  FrontEnd(TitleScript title, MainMenuScript menu);
  void open_menu_from(const std::string &screen);

  // `pointed` is the menu item under the mouse, if any (a port addition): the
  // pointer moving onto an item moves the cursor there, and a click confirms
  // it; a click anywhere on the title opens the menu, as START does.
  void update(double tick_seconds, const InputFrame &input, std::optional<int> pointed = {});

  [[nodiscard]] bool done() const noexcept {
    return next_.has_value();
  }
  // The screen to show next, once done.
  [[nodiscard]] const std::string &next() const {
    return *next_;
  }
  [[nodiscard]] std::vector<std::string> take_sounds();

  [[nodiscard]] Mode mode() const noexcept {
    return mode_;
  }
  [[nodiscard]] const std::string &music() const noexcept {
    return mode_ == Mode::title ? title_.music : menu_.music;
  }
  [[nodiscard]] const TitleScript &title() const noexcept {
    return title_;
  }
  // The open menu level, or null on the title.
  [[nodiscard]] const Menu *menu() const noexcept;
  [[nodiscard]] int cursor() const noexcept;
  [[nodiscard]] bool available(const MenuItem &item) const noexcept {
    return controllers_ >= item.requires_controllers;
  }
  // Seconds since the screen started, for scrolling and blinking.
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  // Black drawn over everything (fades).
  [[nodiscard]] float black_alpha() const noexcept;

private:
  enum class Phase { fade_in, active, fade_out };

  void enter(Mode mode, double fade_in);
  void update_title(const InputFrame &input);
  void update_menu(double tick_seconds, const InputFrame &input, std::optional<int> pointed);
  void leave(const std::string &target, double fade_out);
  void sound(const std::string &name);

  TitleScript title_;
  MainMenuScript menu_;
  Mode mode_{Mode::title};
  Phase phase_{Phase::fade_in};
  std::string submenu_; // empty on the main level
  int main_cursor_{};
  int sub_cursor_{};
  int controllers_{1};
  double seconds_{};
  double phase_elapsed_{};
  double fade_seconds_{};
  double idle_{};
  CursorRepeat cursor_repeat_;
  std::optional<std::string> next_;
  std::optional<std::string> pending_; // target while fading out
  std::vector<std::string> sounds_;
};

} // namespace phantomcrash
