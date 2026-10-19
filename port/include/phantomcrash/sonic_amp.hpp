#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/music_library.hpp"
#include "phantomcrash/option_menu.hpp"
#include "phantomcrash/sonic_amp_script.hpp"
#include "phantomcrash/town_logic.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace phantomcrash {

enum class SonicAmpView {
  menu,    // SPOT, 01, 02, 03, EXIT along the bottom (0x2b1ec)
  browse,  // a grid of tracks: SPOT's chart or a catalogue (0x2af1a, 0x2b782)
  actions, // BUY, PREVIEW or CANCEL for the track chosen, and its price
  message, // what the clerk says after BUY (0x2ae92)
  preview, // the track playing with its jacket and artist (0x29fdc)
};

// SONIC AMP (original scene 20): the music shop. Its chart of ten tracks
// runs on the television; SPOT sells them, the three catalogues sell every
// track, a page at a time. The music and money it changes are taken back by
// the host once the screen is done.
class SonicAmp final : public TownLogic {
public:
  SonicAmp(SonicAmpScript script, MusicLibrary music, std::int64_t money,
           std::vector<MusicCredit> credits, const std::string &from);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const SonicAmpScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const MusicLibrary &music() const noexcept {
    return music_;
  }
  [[nodiscard]] std::int64_t money() const noexcept {
    return money_;
  }
  [[nodiscard]] const std::vector<MusicCredit> &credits() const noexcept {
    return credits_;
  }
  // The shop's own music, or the track being previewed.
  [[nodiscard]] std::string music_track() const;

  [[nodiscard]] SonicAmpView view() const noexcept {
    return view_;
  }
  [[nodiscard]] double view_seconds() const noexcept {
    return view_seconds_;
  }
  // Seconds since the grid opened from the menu (its band fades in).
  [[nodiscard]] double grid_seconds() const noexcept {
    return grid_seconds_;
  }
  [[nodiscard]] int menu_cursor() const noexcept {
    return cursor_;
  }
  // The menu item chosen: SPOT or a catalogue, while browsing.
  [[nodiscard]] const ShopIcon &item() const {
    return script_.items[static_cast<std::size_t>(cursor_)];
  }
  [[nodiscard]] bool spot() const {
    return item().id == "spot";
  }

  // The chart's rank on the television (0 is No. 1) and how long it has
  // shown; each shows chart.seconds, from No. 10 down.
  [[nodiscard]] int rank() const noexcept {
    return rank_;
  }
  [[nodiscard]] double rank_seconds() const noexcept {
    return rank_seconds_;
  }
  // The track at a rank, or -1 when the chart has none there.
  [[nodiscard]] int chart_track(int rank) const;

  // The grid: its columns and the rows on this page, its page of how many
  // (a catalogue's), the cursor and the track in a slot or under the cursor.
  [[nodiscard]] int columns() const;
  [[nodiscard]] int rows() const;
  [[nodiscard]] int page() const noexcept {
    return page_;
  }
  [[nodiscard]] int pages() const;
  [[nodiscard]] int column() const noexcept {
    return column_;
  }
  [[nodiscard]] int row() const noexcept {
    return row_;
  }
  [[nodiscard]] int slot_track(int row, int column) const;
  [[nodiscard]] int cursor_track() const {
    return slot_track(row_, column_);
  }

  [[nodiscard]] int action() const noexcept {
    return action_;
  }
  // What the clerk says: "price" over the actions, then "thanks", "poor"
  // or "owned" after BUY; empty when the dialog is shut.
  [[nodiscard]] const std::string &saying() const noexcept {
    return saying_;
  }

  [[nodiscard]] const std::string &help_text() const noexcept {
    return help_;
  }
  [[nodiscard]] double help_seconds() const noexcept {
    return help_seconds_;
  }

private:
  InputFrame pointer_input(InputFrame input);
  void open(SonicAmpView view);
  void menu_input(double tick_seconds, const InputFrame &input);
  void browse_input(double tick_seconds, const InputFrame &input);
  void actions_input(double tick_seconds, const InputFrame &input);
  void buy();
  void turn_page(int step);
  [[nodiscard]] int first_page() const;
  void restart_help();

  SonicAmpScript script_;
  MusicLibrary music_;
  std::int64_t money_{};
  std::vector<MusicCredit> credits_;
  std::string own_music_;
  CursorRepeat repeat_; // up and down
  CursorRepeat side_;   // left and right
  SonicAmpView view_{SonicAmpView::menu};
  double view_seconds_{1.0e9};
  double grid_seconds_{1.0e9};
  int cursor_{};
  int rank_{music_chart_size - 1};
  double rank_seconds_{};
  int page_{};
  int column_{};
  int row_{};
  int action_{};
  std::string saying_;
  bool opened_{};
  int preview_{-1};
  std::string help_;
  double help_seconds_{};
};

} // namespace phantomcrash
