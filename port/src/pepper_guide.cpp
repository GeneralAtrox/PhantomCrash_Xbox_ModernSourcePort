#include "phantomcrash/pepper_guide.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {

// Whether a question in the tree leads to `screen` (an answer or a lesson).
bool answers(const std::vector<GuideQuestion> &questions, const std::string &screen) {
  return std::ranges::any_of(questions, [&](const GuideQuestion &question) {
    return (!question.next.empty() && question.next == screen) ||
           (!question.cleared_next.empty() && question.cleared_next == screen) ||
           answers(question.questions, screen);
  });
}

int wrapped(const int value, const int count) {
  return count > 0 ? ((value % count) + count) % count : 0;
}

} // namespace

PepperGuide::PepperGuide(PepperGuideScript script, GuideMemory &memory, const std::string &from,
                         const bool cleared)
    : script_(std::move(script)), memory_(memory), cleared_(cleared),
      fade_(script_.fade_in, script_.fade_out),
      repeat_(script_.repeat_delay, script_.repeat_interval) {
  // Back from an answer the guide carries on where it was; otherwise it
  // remembers where it was opened from, for End (0x299fe).
  if (!from.empty() && !answers(script_.questions, from)) {
    memory_.origin = from;
  }
  // Memory that does not fit the questions (none yet) starts at the top.
  const std::vector<GuideQuestion> *list = &script_.questions;
  bool fits = memory_.levels.size() > memory_.open.size();
  for (const int topic : memory_.open) {
    if (!fits || topic < 0 || topic >= static_cast<int>(list->size()) ||
        (*list)[static_cast<std::size_t>(topic)].questions.empty()) {
      fits = false;
      break;
    }
    list = &(*list)[static_cast<std::size_t>(topic)].questions;
  }
  if (!fits) {
    memory_.open.clear();
    memory_.levels.assign(1, {});
  }
  GuideMemory::Level &at = level();
  at.row = std::clamp(at.row, 0, std::max(shown() - 1, 0));
  at.scroll = wrapped(at.scroll, static_cast<int>(questions().size()));
}

const std::vector<GuideQuestion> &PepperGuide::questions() const {
  const std::vector<GuideQuestion> *list = &script_.questions;
  for (const int topic : memory_.open) {
    list = &(*list)[static_cast<std::size_t>(topic)].questions;
  }
  return *list;
}

int PepperGuide::shown() const {
  return std::min(script_.rows, static_cast<int>(questions().size()));
}

std::vector<int> PepperGuide::rows() const {
  std::vector<int> result;
  const int count = static_cast<int>(questions().size());
  for (int row = 0; row < shown(); ++row) {
    result.push_back(wrapped(level().scroll + row, count));
  }
  return result;
}

int PepperGuide::selected() const {
  return wrapped(level().scroll + level().row, static_cast<int>(questions().size()));
}

void PepperGuide::sound(const std::string &name) {
  if (!name.empty()) {
    sounds_.push_back(name);
  }
}

// The cursor moves down the rows shown, then the list scrolls round under it;
// a list no longer than the rows wraps the cursor instead (0xdf6d0).
void PepperGuide::move(const int step) {
  const int count = static_cast<int>(questions().size());
  const int visible = shown();
  GuideMemory::Level &at = level();
  int row = at.row + step;
  if (visible < count) {
    int scrolled = 0;
    if (row > visible - 1) {
      scrolled = row - (visible - 1);
      row = visible - 1;
    } else if (row < 0) {
      scrolled = row;
      row = 0;
    }
    at.scroll = wrapped(at.scroll + scrolled, count);
  } else if (row > count - 1) {
    row = 0;
  } else if (row < 0) {
    row = count - 1;
  }
  at.row = row;
  sound(script_.move_sound);
}

void PepperGuide::confirm() {
  const int index = selected();
  const GuideQuestion &question = questions()[static_cast<std::size_t>(index)];
  sound(script_.select_sound);
  if (question.end) {
    const auto exit = script_.end_exits.find(memory_.origin);
    fade_.leave(exit == script_.end_exits.end() ? script_.exit_to : exit->second);
  } else if (!question.questions.empty()) {
    memory_.open.push_back(index);
    if (memory_.levels.size() <= memory_.open.size()) {
      memory_.levels.emplace_back();
    }
  } else {
    // After the staff roll some answers change (0x6dfc1).
    fade_.leave(cleared_ && !question.cleared_next.empty() ? question.cleared_next : question.next);
  }
}

void PepperGuide::cancel() {
  if (in_topic()) {
    sound(script_.back_sound);
    level() = {};
    memory_.open.pop_back();
    return;
  }
  // At the top, cancel puts the cursor on the last question, End, at the
  // bottom row.
  const int count = static_cast<int>(questions().size());
  const GuideMemory::Level end{shown() - 1, wrapped(count - shown(), count)};
  if (level().row != end.row || level().scroll != end.scroll) {
    sound(script_.move_sound);
    level() = end;
  }
}

void PepperGuide::update(const double tick_seconds, const InputFrame &input) {
  seconds_ += tick_seconds;
  const bool ready = fade_.update(tick_seconds);
  if (!ready || fade_.leaving() || questions().empty()) {
    return;
  }
  bool fresh = false;
  const int step =
      repeat_.step(tick_seconds, input, script_.up_buttons, script_.down_buttons, fresh);
  if (step != 0) {
    move(step);
  } else if (input.any_pressed(script_.confirm_buttons)) {
    confirm();
  } else if (input.any_pressed(script_.cancel_buttons)) {
    cancel();
  }
}

} // namespace phantomcrash
