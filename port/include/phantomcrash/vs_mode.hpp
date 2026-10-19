#pragma once

#include "phantomcrash/card_row.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/town_screen.hpp"
#include "phantomcrash/vs_mode_script.hpp"
#include "phantomcrash/vs_scoobee.hpp"

#include <array>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace phantomcrash {

// One part of a player's panel opening or closing over `seconds`, after an
// optional delay (0x03ffc9's states: 0 hidden, 1 opening, 2 shown, 3
// closing).
class VsReveal final {
public:
  explicit VsReveal(double seconds = 0.0) : seconds_(seconds) {}

  void open(double delay = 0.0);
  void close(double delay = 0.0);
  // Shown or hidden at once.
  void set(bool shown) noexcept;
  void update(double tick_seconds);

  // How far open, 0 to 1.
  [[nodiscard]] float along() const noexcept {
    return static_cast<float>(amount_);
  }
  [[nodiscard]] bool opening() const noexcept {
    return open_;
  }
  [[nodiscard]] bool shown() const noexcept {
    return open_ && amount_ >= 1.0 && delay_ <= 0.0;
  }
  [[nodiscard]] bool hidden() const noexcept {
    return !open_ && amount_ <= 0.0 && delay_ <= 0.0;
  }

private:
  double seconds_;
  double amount_{};
  double delay_{};
  bool open_{};
};

enum class VsPhase { entry, select, area };

// What the VS mode keeps between its screen and the battle: who entered
// and with which Scoobee, their wins and losses, and the stage chosen.
struct VsSession {
  std::array<bool, controller_ports> joined{};
  std::array<int, controller_ports> chosen{};
  std::array<int, controller_ports> wins{};
  std::array<int, controller_ports> losses{};
  std::array<std::optional<RobotRecord>, controller_ports> robots;
  int stage{-1};
  int area_cursor{};
  // Where the battle's menu returns: the area select, or (SELECT SV) the
  // Scoobee select.
  VsPhase resume{VsPhase::area};
};

// How the VS mode is entered: from the main menu afresh, or back from its
// battle at the area select.
struct VsModeEntry {
  bool from_battle{};
};

// What the phase is doing: its opening, taking the players' input, asking
// Are you ready? (and closing on NO), the abandon question, the panels
// closing before the next phase or leaving, the black between phases, and
// the area select's time-zone window.
enum class VsStep {
  intro,
  input,
  question,
  answered_no,
  abandon,
  leaving,
  exiting,
  blackout,
  zone
};

struct VsPlayer {
  bool joined{};
  bool decided{};
  int first{};
  int row{};
  bool started{};       // the phase has opened its panel
  bool wants_content{}; // its contents open once the panel is open
  VsReveal panel;       // the panel, then its PLAYER label
  VsReveal content;     // ENTER NOW / entered, or the Scoobee strip
  VsReveal extra;       // the next-step hint, or the your SV! window
  VsReveal overlay;     // No Controller, or No Entry
  CardRow cards;
  CursorRepeat repeat;

  [[nodiscard]] int chosen() const noexcept {
    return first + row;
  }
};

// VS MODE (0x042428): up to four players enter on the controller ports,
// each chooses a Scoobee, then the area and its time of day are chosen and
// the battle starts. `kept` are KEEP SV's Scoobees; `session` outlives the
// screen.
class VsMode final {
public:
  VsMode(VsModeScript script, const PartTables &parts, const GarageTables &tables,
         const std::vector<VsScoobee> &kept, VsSession &session, VsModeEntry entry);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const VsModeScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] VsPhase phase() const noexcept {
    return phase_;
  }
  [[nodiscard]] VsStep step() const noexcept {
    return step_;
  }
  [[nodiscard]] double phase_seconds() const noexcept {
    return phase_seconds_;
  }
  [[nodiscard]] const VsPlayer &player(int port) const {
    return players_.at(static_cast<std::size_t>(port));
  }
  [[nodiscard]] bool connected(int port) const {
    return connected_.at(static_cast<std::size_t>(port));
  }
  [[nodiscard]] int joined_count() const;
  [[nodiscard]] const VsSession &session() const noexcept {
    return session_;
  }

  // The Scoobees along each strip: the presets, then the kept ones.
  [[nodiscard]] int count() const;
  [[nodiscard]] int preset(int index) const; // its preset, or -1 for a kept one
  [[nodiscard]] const VsScoobee *kept(int index) const;
  [[nodiscard]] bool usable(int index) const; // a preset, or an undamaged kept one
  [[nodiscard]] int maker(int index) const;   // a kept one's body's maker
  [[nodiscard]] std::string name(int index) const;
  [[nodiscard]] std::optional<RobotRecord> robot(int index) const;

  // Are you ready?: the window's growth and YES (0) or NO (1).
  [[nodiscard]] const VsReveal &popup() const noexcept {
    return popup_;
  }
  [[nodiscard]] int choice() const noexcept {
    return choice_;
  }
  // The message box's message ("abandon"), empty for none.
  [[nodiscard]] const std::string &message() const noexcept {
    return message_;
  }

  // The area select: the items shown (indices into the script's), the
  // cursor among them, how far they have slid in, the pictures cross-fading
  // (the one shown and the one before, by item and time of day), RANDOM's
  // stage picture, and the time-zone window with DAY (0) or NIGHT (1).
  struct Picture {
    int item{-1};
    bool night{};
    bool operator==(const Picture &) const = default;
  };
  [[nodiscard]] const std::vector<int> &items() const noexcept {
    return items_;
  }
  [[nodiscard]] int area_cursor() const noexcept {
    return area_cursor_;
  }
  [[nodiscard]] float slide_along() const;
  [[nodiscard]] const Picture &picture() const noexcept {
    return picture_;
  }
  [[nodiscard]] const Picture &previous_picture() const noexcept {
    return previous_;
  }
  [[nodiscard]] float picture_along() const;
  [[nodiscard]] int random_stage() const noexcept {
    return random_stage_;
  }
  [[nodiscard]] const VsReveal &zone() const noexcept {
    return zone_;
  }
  [[nodiscard]] int zone_choice() const noexcept {
    return zone_choice_;
  }

  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] const std::string &help_text() const;
  [[nodiscard]] double help_seconds() const noexcept {
    return help_seconds_;
  }
  [[nodiscard]] bool faded_in() const noexcept {
    return !fade_.fading_in();
  }
  [[nodiscard]] float black_alpha() const noexcept;
  [[nodiscard]] std::vector<std::string> take_sounds() {
    return sounds_.take();
  }
  [[nodiscard]] bool done() const noexcept {
    return fade_.done();
  }
  [[nodiscard]] std::string next() const {
    return fade_.done() ? fade_.next() : std::string{};
  }

private:
  static constexpr int yes_choice = 0;
  static constexpr int no_choice = 1;
  static constexpr int players_to_start = 2; // 0x040aec's count before Are you ready?

  void start_phase(VsPhase phase);
  void open_content(int port);
  void update_players(double tick_seconds);
  void close_contents(bool panels_too);
  void entry_input(double tick_seconds, const InputFrame &input);
  void select_input(double tick_seconds, const InputFrame &input);
  void question_input(double tick_seconds, const InputFrame &input);
  void abandon_input(double tick_seconds, const InputFrame &input);
  void answer(bool yes);
  void ask();
  void step_timers(double tick_seconds);
  // The area select (vs_mode_area.cpp).
  void enter_area();
  void update_area(double tick_seconds);
  void area_input(double tick_seconds, const InputFrame &input);
  void zone_input(double tick_seconds, const InputFrame &input);
  void confirm_area();
  void start_battle(int stage);
  [[nodiscard]] Picture wanted_picture() const;
  [[nodiscard]] bool crowd() const;
  [[nodiscard]] InputFrame joined_input(const InputFrame &input) const;

  VsModeScript script_;
  const PartTables &parts_;
  const GarageTables &tables_;
  const std::vector<VsScoobee> &kept_;
  VsSession &session_;
  ScreenFade fade_;
  SoundQueue sounds_;
  std::array<VsPlayer, controller_ports> players_;
  std::array<bool, controller_ports> connected_{};
  CursorRepeat across_;
  VsPhase phase_{VsPhase::entry};
  VsStep step_{VsStep::intro};
  double phase_seconds_{};
  double step_seconds_{};
  VsPhase after_{VsPhase::entry}; // the phase the leaving or the black leads to
  double black_seconds_{-1.0};    // through the black between phases; -1 for none
  VsReveal popup_;
  int choice_{};
  std::string message_;
  std::vector<int> items_;
  int area_cursor_{};
  double area_seconds_{};
  Picture picture_;
  Picture previous_;
  double picture_seconds_{1.0e9};
  double random_seconds_{};
  int random_stage_{};
  VsReveal zone_;
  int zone_choice_{};
  std::minstd_rand random_{0x5c00beeu};
  double seconds_{};
  double help_seconds_{};
};

} // namespace phantomcrash
