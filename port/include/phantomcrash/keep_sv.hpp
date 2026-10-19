#pragma once

#include "phantomcrash/card_row.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/keep_sv_script.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/town_screen.hpp"
#include "phantomcrash/vs_scoobee.hpp"

#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// Where KEEP SV's cursor is, kept for coming back from SV SPEC and the
// hangar (0x04373d): the list's first visible row, the row among the
// visible ones and the column (the list, SAVE, EXIT).
struct KeepSvCursor {
  int first{};
  int row{};
  int column{1};
};

enum class KeepSvView { list, save, exit, popup, question, notice };

// How KEEP SV is entered.
struct KeepSvEntry {
  bool fresh{true};              // from the SYSTEM menu: on SAVE, with the opening
  std::optional<VsScoobee> kept; // the Scoobee the hangar's KEEP chose
};

// KEEP SV (0x042f2d): the kept Scoobees in a row of cards, SAVE and EXIT.
// `kept` is the list it changes; `cursor` is kept across visits.
class KeepSv final {
public:
  KeepSv(KeepSvScript script, const PartTables &parts, QuestSave quest,
         std::vector<VsScoobee> &kept, KeepSvCursor &cursor, KeepSvEntry entry);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const KeepSvScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const QuestSave &quest() const noexcept {
    return quest_;
  }
  [[nodiscard]] KeepSvView view() const noexcept {
    return view_;
  }
  [[nodiscard]] const KeepSvCursor &cursor() const noexcept {
    return cursor_;
  }
  // The rows along the list: the kept Scoobees, and empty rows up to the
  // visible slots.
  [[nodiscard]] int count() const;
  [[nodiscard]] const VsScoobee *scoobee(int index) const;
  [[nodiscard]] bool damaged(int index) const;
  [[nodiscard]] int maker(int index) const;
  // The row at the cursor.
  [[nodiscard]] int chosen() const noexcept {
    return cursor_.first + cursor_.row;
  }
  // While the list scrolls, the first row it shows and how far it is
  // pushed along; the first half still shows the rows before.
  [[nodiscard]] bool scrolling() const noexcept {
    return row_.scrolling();
  }
  [[nodiscard]] int shown_first() const;
  [[nodiscard]] float scroll_offset() const {
    return row_.offset();
  }
  // Whether the SV NAME and DATE show the chosen Scoobee (0x0426a4).
  [[nodiscard]] bool shows_details() const;
  // The message box's message, empty for none; the popup whose options go
  // with it, and the option or YES/NO chosen.
  [[nodiscard]] const std::string &message() const noexcept {
    return message_;
  }
  [[nodiscard]] const std::vector<KeepSvPopupOption> *popup() const;
  [[nodiscard]] int choice() const noexcept {
    return choice_;
  }
  [[nodiscard]] double intro_seconds() const noexcept {
    return intro_seconds_;
  }
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] double help_seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] bool faded_in() const noexcept {
    return !fade_.fading_in();
  }
  [[nodiscard]] float black_alpha() const noexcept {
    return fade_.black_alpha();
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
  void list_input(double tick_seconds, const InputFrame &input);
  void column_input(double tick_seconds, const InputFrame &input);
  void popup_input(double tick_seconds, const InputFrame &input);
  void question_input(double tick_seconds, const InputFrame &input);
  void move_row(int step);
  void confirm_row();
  void confirm_save();
  void choose(KeepSvOption option);
  void answer(bool yes);
  void ask(const std::string &message, int choice);
  void open_popup(const std::string &popup);
  void tell(const std::string &message);
  void to_column();
  void keep(VsScoobee scoobee);
  [[nodiscard]] bool can_save() const;

  KeepSvScript script_;
  const PartTables &parts_;
  QuestSave quest_;
  std::vector<VsScoobee> &kept_;
  KeepSvCursor &cursor_;
  ScreenFade fade_;
  CursorRepeat across_;
  CursorRepeat down_;
  SoundQueue sounds_;
  KeepSvView view_{KeepSvView::save};
  std::string message_;
  std::string popup_;
  int choice_{};
  std::optional<VsScoobee> pending_; // waiting for OVERWRITE's answer
  CardRow row_;
  double intro_seconds_{};
  double seconds_{};
};

} // namespace phantomcrash
