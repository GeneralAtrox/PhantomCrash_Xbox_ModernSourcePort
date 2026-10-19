#pragma once

#include "phantomcrash/battle_music_script.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/music_library.hpp"
#include "phantomcrash/option_menu.hpp"
#include "phantomcrash/town_logic.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

enum class BattleMusicView {
  menu,        // PROGRAM SET, PROGRAM EDIT, PLAY MODE, PROGRAM NAME, EXIT (0x38607)
  choose,      // a program for PROGRAM SET or PROGRAM EDIT (0x38247, 0x38aaa)
  confirm_set, // SET? over the chosen program
  stock,       // the owned tracks to add to the program (0x38b7f)
  play_mode,   // SEQUENTIAL or RANDOM (0x38522)
};

// BATTLE MUSIC (original scene 33): the programs of owned tracks battles
// play, which program they play and in which order. The music it edits is
// taken back by the host once the screen is done.
class BattleMusic final : public TownLogic {
public:
  BattleMusic(BattleMusicScript script, MusicLibrary music, std::vector<MusicCredit> credits,
              const std::string &from);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const BattleMusicScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const MusicLibrary &music() const noexcept {
    return music_;
  }
  [[nodiscard]] const std::vector<MusicCredit> &credits() const noexcept {
    return credits_;
  }
  // The music to play: the screen's own, or the track being previewed.
  [[nodiscard]] std::string music_track() const;

  [[nodiscard]] BattleMusicView view() const noexcept {
    return view_;
  }
  // Seconds since the view opened (its panel slides in over slide_seconds).
  [[nodiscard]] double view_seconds() const noexcept {
    return view_seconds_;
  }
  // The view slid away from, while the new one slides in.
  [[nodiscard]] BattleMusicView view_before() const noexcept {
    return before_;
  }
  [[nodiscard]] int menu_cursor() const noexcept {
    return cursor_;
  }
  [[nodiscard]] bool on_exit() const noexcept {
    return cursor_ == static_cast<int>(script_.items.size());
  }
  // Seconds since the television started for the cursor's item, and
  // whether it came up from below.
  [[nodiscard]] double arrive_seconds() const noexcept {
    return arrive_seconds_;
  }
  [[nodiscard]] bool arrived_upwards() const noexcept {
    return arrived_up_;
  }
  [[nodiscard]] bool editing() const noexcept {
    return editing_;
  }
  // The program the discs show and the programs list points at.
  [[nodiscard]] int browsed() const noexcept {
    return browsed_;
  }
  [[nodiscard]] int mode_choice() const noexcept {
    return mode_choice_;
  }
  [[nodiscard]] double ok_seconds() const noexcept {
    return ok_seconds_;
  }

  // The stock: its pages, the cursor (row == rows is the OK row) and the
  // track under it, or -1.
  [[nodiscard]] int pages() const;
  [[nodiscard]] int page() const noexcept {
    return page_;
  }
  [[nodiscard]] int column() const noexcept {
    return column_;
  }
  [[nodiscard]] int row() const noexcept {
    return row_;
  }
  [[nodiscard]] bool on_ok() const noexcept {
    return row_ == script_.stock.rows;
  }
  [[nodiscard]] int stock_track(int page, int slot) const;
  [[nodiscard]] int cursor_track() const;
  [[nodiscard]] int previewing() const noexcept {
    return preview_;
  }
  // The help line: a track's credit on the stock, otherwise the screen's.
  [[nodiscard]] std::string help_text() const;
  [[nodiscard]] double help_seconds() const noexcept {
    return help_seconds_;
  }

private:
  void open(BattleMusicView view);
  void menu_input(double tick_seconds, const InputFrame &input);
  void choose_input(double tick_seconds, const InputFrame &input);
  void confirm_input(const InputFrame &input);
  void stock_input(double tick_seconds, const InputFrame &input);
  void mode_input(double tick_seconds, const InputFrame &input);
  void turn_page(int step);
  void restart_help();

  BattleMusicScript script_;
  MusicLibrary music_;
  std::vector<MusicCredit> credits_;
  std::vector<int> owned_;
  std::string own_music_;
  CursorRepeat repeat_; // up and down
  CursorRepeat side_;   // left and right
  BattleMusicView view_{BattleMusicView::menu};
  BattleMusicView before_{BattleMusicView::menu};
  double view_seconds_{1.0e9};
  int cursor_{};
  double arrive_seconds_{1.0e9};
  bool arrived_up_{};
  bool editing_{};
  int browsed_{};
  int mode_choice_{};
  double ok_seconds_{1.0e9};
  int page_{};
  int column_{};
  int row_{};
  int preview_{-1};
  std::string help_;
  double help_seconds_{};
};

} // namespace phantomcrash
