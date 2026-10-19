// RECORDS' values, row reveal and menus (0x34336, 0x33d99, 0x34451,
// 0x33660, 0x3472d).

#include "phantomcrash/records.hpp"
#include "phantomcrash/money.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace phantomcrash {
namespace {

constexpr std::int64_t percent_full = 100;
constexpr std::int64_t records_money_cap = 99'999'999; // stored, before it is shown in NY
constexpr int seconds_per_minute = 60;
constexpr int seconds_per_hour = 3600;
// The area ranker's cell spans the venue grid's rows (0x330d1).
constexpr int venue_class_columns = 4;
constexpr int area_ranker_slot = 8;

bool contains(const std::vector<int> &values, const int value) {
  return std::ranges::find(values, value) != values.end();
}

std::int64_t count_of(const QuestSave &quest, const std::string &stat) {
  if (stat == "days_passed") {
    return quest.total_days;
  }
  const auto found = quest.records.find(stat);
  return found != quest.records.end() ? found->second : 0;
}

std::string with_commas(std::int64_t value) {
  const bool negative = value < 0;
  std::string digits = std::to_string(negative ? -value : value);
  for (auto at = static_cast<std::ptrdiff_t>(digits.size()) - 3; at > 0; at -= 3) {
    digits.insert(static_cast<std::size_t>(at), ",");
  }
  return negative ? "-" + digits : digits;
}

std::string two_digits(const std::int64_t value) {
  return (value < 10 ? "0" : "") + std::to_string(value);
}

std::uint32_t mix(const std::uint32_t from, const std::uint32_t to, const float amount) {
  std::uint32_t out = 0;
  for (int shift = 0; shift <= 16; shift += 8) {
    const float a = static_cast<float>((from >> shift) & 0xffU);
    const float b = static_cast<float>((to >> shift) & 0xffU);
    out |= static_cast<std::uint32_t>(std::lround(a + (b - a) * amount)) << shift;
  }
  return out;
}

float clamp01(const double value) {
  return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

} // namespace

StoryProgress story_progress(const std::vector<StoryEvent> &events, const QuestSave &quest,
                             const std::vector<int> &unscored) {
  StoryProgress progress;
  for (const StoryEvent &event : events) {
    if (event.enabled && !contains(unscored, event.scene)) {
      ++progress.events;
      progress.seen += contains(quest.seen_events, event.scene) ? 1 : 0;
    }
  }
  return progress;
}

// 0x33556 and 0x33d99: the rates are whole percentages.
std::int64_t record_value(const RecordRow &row, const RecordsScript &script, const QuestSave &quest,
                          const StoryProgress progress) {
  if (row.completion) {
    const auto won = std::ranges::count_if(script.completion_contests, [&](const int contest) {
      return contains(quest.won_contests, contest);
    });
    const auto total =
        static_cast<std::int64_t>(script.completion_contests.size()) + progress.events;
    return total > 0 ? (won + progress.seen) * percent_full / total : 0;
  }
  const std::int64_t count = count_of(quest, row.stat);
  if (row.ratio_of.empty()) {
    return count;
  }
  const std::int64_t whole = count_of(quest, row.ratio_of);
  return whole > 0 ? count * percent_full / whole : 0;
}

RecordText record_text(const RecordFormat format, const std::int64_t value,
                       const RecordNumbers &numbers) {
  switch (format) {
  case RecordFormat::percent:
    return {with_commas(value), numbers.percent};
  case RecordFormat::money:
    // Capped before it is shown in NY (0x033216).
    return {with_commas(shown_ny(std::min(value, records_money_cap))), numbers.money};
  case RecordFormat::time: {
    const std::int64_t seconds = std::max<std::int64_t>(value, 0);
    const std::int64_t hours =
        std::min<std::int64_t>(seconds / seconds_per_hour, numbers.hours_max);
    return {two_digits(hours) + ":" + two_digits(seconds / seconds_per_minute % 60) + ":" +
                two_digits(seconds % seconds_per_minute),
            {}};
  }
  case RecordFormat::count:
    break;
  }
  return {with_commas(value), {}};
}

double row_start(const RowReveal &reveal, const int row) {
  return reveal.first_seconds + reveal.step_seconds * row;
}

// 0x33b2c: the text fades in and whitens; the bar shows for the start of
// the fade, narrowing towards its end.
RowLook row_look(const RowReveal &reveal, const int row, const double seconds) {
  RowLook look;
  const double since = seconds - row_start(reveal, row);
  if (since < 0.0) {
    return look;
  }
  look.shown = true;
  const float fade = clamp01(since / reveal.fade_seconds);
  look.alpha = fade;
  look.colour = mix(reveal.start_colour, 0xffffff, clamp01(since / reveal.whiten_seconds));
  if (fade > 0.0F && fade < reveal.bar_until) {
    look.bar = true;
    look.bar_alpha = 1.0F - fade / reveal.bar_until;
    if (fade > reveal.bar_shrink_from) {
      look.bar_scale =
          1.0F - (fade - reveal.bar_shrink_from) / (reveal.bar_until - reveal.bar_shrink_from);
    }
  }
  return look;
}

// 0x34451: the items start `distance` plus a stagger to the left, the lowest
// furthest, and all arrive by the end of the slide.
float convention_slide(const ConventionStyle &style, const int item, const double seconds) {
  const auto count = static_cast<float>(style.items.size());
  const float lead = style.slide_stagger * (count - 1.0F - static_cast<float>(item));
  const float travel = style.slide_distance * (1.0F - clamp01(seconds / style.slide_seconds));
  return std::min(0.0F, lead - travel);
}

Records::Records(RecordsScript script, const ContestTables &contests, QuestSave quest,
                 const StoryProgress progress, std::string back)
    : script_(std::move(script)), contests_(contests), quest_(std::move(quest)),
      progress_(progress), back_(std::move(back)), fade_(script_.fade_in, script_.fade_out),
      rows_repeat_(script_.repeat_delay, script_.repeat_interval),
      columns_repeat_(script_.repeat_delay, script_.repeat_interval) {}

const RecordPage *Records::page() const {
  const auto found = script_.pages.find(page_);
  return view_ == RecordsView::page && found != script_.pages.end() ? &found->second : nullptr;
}

const ConventionVenue &Records::venue() const {
  const auto &venues = script_.convention.venues;
  return view_ == RecordsView::regulation || venues.empty()
             ? script_.convention.regulation.contests
             : venues.at(static_cast<std::size_t>(
                   std::clamp(venue_, 0, static_cast<int>(venues.size()) - 1)));
}

int Records::grid_columns() const {
  return view_ == RecordsView::regulation ? script_.convention.regulation.columns
                                          : venue_class_columns + 1;
}

int Records::grid_rows() const {
  return view_ == RecordsView::regulation ? script_.convention.regulation.rows : 2;
}

int Records::slot() const {
  if (view_ == RecordsView::regulation) {
    const ConventionRegulation &regulation = script_.convention.regulation;
    return column_ + (row_ + regulation_page_ * regulation.rows) * regulation.columns;
  }
  return column_ < venue_class_columns ? column_ + row_ * venue_class_columns : area_ranker_slot;
}

int Records::slot_contest(const int slot) const {
  const auto &contests = venue().contests;
  return slot >= 0 && slot < static_cast<int>(contests.size())
             ? contests[static_cast<std::size_t>(slot)]
             : no_contest;
}

int Records::selected_contest() const {
  return view_ == RecordsView::venue || view_ == RecordsView::regulation ? slot_contest(slot())
                                                                         : no_contest;
}

bool Records::won(const int contest) const {
  return contest != no_contest && contains(quest_.won_contests, contest);
}

bool Records::champion_hidden(const int contest) const {
  const HiddenChampion &hidden = script_.convention.hidden;
  return contest == hidden.contest && !contains(quest_.seen_events, hidden.until_event);
}

void Records::open(const RecordsView view) {
  view_ = view;
  view_seconds_ = 0.0;
  rows_repeat_.reset();
  columns_repeat_.reset();
}

void Records::play_row_sounds(const double before) {
  if (const RecordPage *shown = page()) {
    for (int row = 0; row < static_cast<int>(shown->rows.size()); ++row) {
      const double start = row_start(script_.reveal, row);
      if (before < start && start <= view_seconds_) {
        sounds_.play(script_.row_sound);
      }
    }
  }
}

void Records::update(const double tick_seconds, const InputFrame &input) {
  seconds_ += tick_seconds;
  const double before = view_seconds_;
  view_seconds_ += tick_seconds;
  flip_left_ -= tick_seconds;
  play_row_sounds(before);
  if (!fade_.update(tick_seconds) || fade_.leaving()) {
    return;
  }
  switch (view_) {
  case RecordsView::menu:
    menu_input(tick_seconds, input);
    break;
  case RecordsView::page:
    page_input(input);
    break;
  case RecordsView::convention:
    convention_input(tick_seconds, input);
    break;
  case RecordsView::venue:
  case RecordsView::regulation:
    grid_input(tick_seconds, input);
    break;
  }
}

void Records::menu_input(const double tick_seconds, const InputFrame &input) {
  const int exit_item = static_cast<int>(script_.menu.items.size()) - 1;
  if (input.any_pressed(script_.confirm_buttons)) {
    sounds_.play(script_.select_sound);
    const std::string &opens = script_.menu.items[static_cast<std::size_t>(menu_cursor_)].opens;
    if (opens == "exit") {
      fade_.leave(back_);
    } else if (opens == "convention") {
      convention_cursor_ = 0;
      open(RecordsView::convention);
    } else {
      page_ = opens;
      open(RecordsView::page);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    if (menu_cursor_ != exit_item) {
      sounds_.play(script_.exit_sound);
    }
    menu_cursor_ = exit_item;
    return;
  }
  bool fresh = false;
  const int step = rows_repeat_.step(tick_seconds, input, script_.up, script_.down, fresh);
  if (const int moved = move_cursor(menu_cursor_, step, fresh, exit_item + 1);
      moved != menu_cursor_) {
    sounds_.play(script_.menu_sound);
    menu_cursor_ = moved;
  }
}

void Records::page_input(const InputFrame &input) {
  const RecordPage *shown = page();
  if (shown != nullptr && view_seconds_ >= shown->growth.seconds &&
      input.any_pressed(script_.cancel_buttons)) {
    sounds_.play(script_.close_sound);
    open(RecordsView::menu);
  }
}

void Records::convention_input(const double tick_seconds, const InputFrame &input) {
  const ConventionStyle &style = script_.convention;
  if (view_seconds_ < style.slide_seconds) {
    return;
  }
  const int back_item = static_cast<int>(style.items.size()) - 1;
  if (input.any_pressed(script_.confirm_buttons)) {
    sounds_.play(script_.select_sound);
    const std::string &opens = style.item_opens[static_cast<std::size_t>(convention_cursor_)];
    column_ = row_ = regulation_page_ = 0;
    if (opens == "venue") {
      venue_ = convention_cursor_;
      open(RecordsView::venue);
    } else if (opens == "regulation") {
      open(RecordsView::regulation);
    } else {
      open(RecordsView::menu);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    if (convention_cursor_ != back_item) {
      sounds_.play(script_.exit_sound);
    }
    convention_cursor_ = back_item;
    return;
  }
  bool fresh = false;
  const int step = rows_repeat_.step(tick_seconds, input, script_.up, script_.down, fresh);
  if (const int moved = move_cursor(convention_cursor_, step, fresh, back_item + 1);
      moved != convention_cursor_) {
    sounds_.play(script_.move_sound);
    convention_cursor_ = moved;
  }
}

void Records::grid_input(const double tick_seconds, const InputFrame &input) {
  if (view_seconds_ < script_.convention.growth.seconds) {
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    sounds_.play(script_.close_sound);
    open(RecordsView::convention);
    return;
  }
  bool fresh = false;
  const int down = rows_repeat_.step(tick_seconds, input, script_.up, script_.down, fresh);
  if (const int moved = move_cursor(row_, down, fresh, grid_rows()); moved != row_) {
    sounds_.play(script_.move_sound);
    row_ = moved;
    return;
  }
  const int across = columns_repeat_.step(tick_seconds, input, script_.left, script_.right, fresh);
  if (const int moved = move_cursor(column_, across, fresh, grid_columns()); moved != column_) {
    sounds_.play(script_.column_sound);
    column_ = moved;
    return;
  }
  if (view_ != RecordsView::regulation) {
    return;
  }
  // L and R turn the page either way (0x3472d).
  const ConventionRegulation &regulation = script_.convention.regulation;
  const int per_page = regulation.rows * regulation.columns;
  const int pages = std::max(
      1, (static_cast<int>(regulation.contests.contests.size()) + per_page - 1) / per_page);
  const bool back = input.any_pressed(script_.page_back);
  if (back || input.any_pressed(script_.page_forward)) {
    sounds_.play(script_.page_sound);
    regulation_page_ = (regulation_page_ + (back ? pages - 1 : 1)) % pages;
    flip_direction_ = back ? -1 : 1;
    flip_left_ = script_.convention.regulation.flip_seconds;
  }
}

} // namespace phantomcrash
