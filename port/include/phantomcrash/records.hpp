#pragma once

// RECORDS (the original's scene 29): the menu of RUMBLING RECORD,
// CONVENTION RECORD and GAME PLAY RECORD, the record pages whose rows
// appear one by one, and CONVENTION RECORD's venues and regulation
// contests with their champions. See data/ui/records.json.

#include "phantomcrash/contests.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/records_script.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/story_event.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace phantomcrash {

// How far the story has got, for the achievement rate: the story events
// there are (0x0d14f7) and those played (0x0d14c8), less the `unscored`.
struct StoryProgress {
  int events{};
  int seen{};
};

[[nodiscard]] StoryProgress story_progress(const std::vector<StoryEvent> &events,
                                           const QuestSave &quest,
                                           const std::vector<int> &unscored = {});

// A row's value; a count the save lacks is 0.
[[nodiscard]] std::int64_t record_value(const RecordRow &row, const RecordsScript &script,
                                        const QuestSave &quest, StoryProgress progress);

// A value as drawn: the digits with commas, or hh:mm:ss, and the unit that
// hangs past the right edge.
struct RecordText {
  std::string digits;
  std::string unit;
};

[[nodiscard]] RecordText record_text(RecordFormat format, std::int64_t value,
                                     const RecordNumbers &numbers);

// How a page's row looks `seconds` after the page opened.
struct RowLook {
  bool shown{};
  float alpha{};
  std::uint32_t colour{0xffffff};
  bool bar{};
  float bar_scale{1.0F};
  float bar_alpha{};
};

[[nodiscard]] double row_start(const RowReveal &reveal, int row);
[[nodiscard]] RowLook row_look(const RowReveal &reveal, int row, double seconds);

// How far left CONVENTION RECORD's item still is as it slides in (0x34451).
[[nodiscard]] float convention_slide(const ConventionStyle &style, int item, double seconds);

enum class RecordsView { menu, page, convention, venue, regulation };

class Records final {
public:
  // `back` is where EXIT returns to: the place it was opened from.
  Records(RecordsScript script, const ContestTables &contests, QuestSave quest,
          StoryProgress progress, std::string back);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const RecordsScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const QuestSave &quest() const noexcept {
    return quest_;
  }
  [[nodiscard]] RecordsView view() const noexcept {
    return view_;
  }
  // Since the view opened (a page's rows, the slides and growths).
  [[nodiscard]] double view_seconds() const noexcept {
    return view_seconds_;
  }
  [[nodiscard]] int menu_cursor() const noexcept {
    return menu_cursor_;
  }
  // The record page open, or null.
  [[nodiscard]] const RecordPage *page() const;
  [[nodiscard]] std::int64_t value(const RecordRow &row) const {
    return record_value(row, script_, quest_, progress_);
  }
  [[nodiscard]] int convention_cursor() const noexcept {
    return convention_cursor_;
  }
  // The venue or REGULATION shown.
  [[nodiscard]] const ConventionVenue &venue() const;
  [[nodiscard]] int grid_column() const noexcept {
    return column_;
  }
  [[nodiscard]] int grid_row() const noexcept {
    return row_;
  }
  [[nodiscard]] int venue_index() const noexcept {
    return venue_;
  }
  [[nodiscard]] int regulation_page() const noexcept {
    return regulation_page_;
  }
  // A page turned just now lights its arrow: -1 left, 1 right, 0 none.
  [[nodiscard]] int flipping() const noexcept {
    return flip_left_ > 0.0 ? flip_direction_ : 0;
  }
  // The cursor's slot in the venue's contests, and its contest.
  [[nodiscard]] int slot() const;
  [[nodiscard]] int selected_contest() const;
  [[nodiscard]] int slot_contest(int slot) const;
  [[nodiscard]] bool won(int contest) const;
  // Its champion stays hidden until a story event has played.
  [[nodiscard]] bool champion_hidden(int contest) const;
  [[nodiscard]] bool faded_in() const noexcept {
    return !fade_.fading_in();
  }
  [[nodiscard]] float black_alpha() const noexcept {
    return fade_.black_alpha();
  }
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
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
  void open(RecordsView view);
  void menu_input(double tick_seconds, const InputFrame &input);
  void page_input(const InputFrame &input);
  void convention_input(double tick_seconds, const InputFrame &input);
  void grid_input(double tick_seconds, const InputFrame &input);
  void play_row_sounds(double before);
  [[nodiscard]] int grid_columns() const;
  [[nodiscard]] int grid_rows() const;

  RecordsScript script_;
  const ContestTables &contests_;
  QuestSave quest_;
  StoryProgress progress_;
  std::string back_;
  ScreenFade fade_;
  CursorRepeat rows_repeat_;
  CursorRepeat columns_repeat_;
  SoundQueue sounds_;
  RecordsView view_{RecordsView::menu};
  double view_seconds_{};
  double seconds_{};
  int menu_cursor_{};
  std::string page_;
  int convention_cursor_{};
  int venue_{};
  int column_{};
  int row_{};
  int regulation_page_{};
  double flip_left_{};
  int flip_direction_{};
};

} // namespace phantomcrash
