#include "phantomcrash/location_menu.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {

int item_index(const std::vector<LocationItem> &items, const std::string &id) {
  const auto found =
      std::ranges::find_if(items, [&](const LocationItem &item) { return item.id == id; });
  return found == items.end() ? -1 : static_cast<int>(found - items.begin());
}

} // namespace

LocationMenu::LocationMenu(LocationMenuScript script, const std::string &from)
    : script_(std::move(script)), fade_(script_.fade_in, script_.fade_out),
      repeat_(script_.repeat_delay, script_.repeat_interval) {
  if (const auto start = script_.start_items.find(from); start != script_.start_items.end()) {
    cursor_ = std::max(item_index(script_.items, start->second), 0);
  }
  shown_ = cursor_;
  if (const auto arrive = script_.arrive_sounds.find(from); arrive != script_.arrive_sounds.end()) {
    sound(arrive->second);
  }
}

void LocationMenu::sound(const std::string &name) {
  if (!name.empty()) {
    sounds_.push_back(name);
  }
}

void LocationMenu::move_to(const int item) {
  cursor_ = item;
  turn_ = 0.0;
  help_seconds_ = 0.0;
  sound(script_.move_sound);
}

void LocationMenu::choose(const int item) {
  if (item < 0 || item >= static_cast<int>(script_.items.size()) || fade_.fading_in() ||
      fade_.leaving()) {
    return;
  }
  const auto &chosen = script_.items[static_cast<std::size_t>(item)];
  cursor_ = shown_ = item;
  sound(chosen.select_sound.empty() ? script_.select_sound : chosen.select_sound);
  fade_.leave(chosen.next);
}

void LocationMenu::rotate(const int steps) {
  if (turning() || fade_.fading_in() || fade_.leaving() || steps == 0) {
    return;
  }
  move_to(move_cursor(cursor_, steps, true, static_cast<int>(script_.items.size())));
}

void LocationMenu::update(const double tick_seconds, const InputFrame &input) {
  seconds_ += tick_seconds;
  help_seconds_ += tick_seconds;
  if (turning()) {
    turn_ += tick_seconds;
    if (turn_ + 1e-9 >= script_.wheel.turn_seconds) {
      shown_ = cursor_;
      turn_ = 0.0;
    }
  }
  const bool ready = fade_.update(tick_seconds);
  if (!ready || fade_.leaving()) {
    return;
  }
  // The wheel takes no input while it turns.
  bool fresh = false;
  const int step =
      repeat_.step(tick_seconds, input, script_.back_buttons, script_.forward_buttons, fresh);
  if (turning()) {
    return;
  }
  const int count = static_cast<int>(script_.items.size());
  if (step != 0) {
    move_to(move_cursor(cursor_, step, true, count));
  } else if (input.any_pressed(script_.confirm_buttons)) {
    const LocationItem &item = script_.items[static_cast<std::size_t>(cursor_)];
    sound(item.select_sound.empty() ? script_.select_sound : item.select_sound);
    fade_.leave(item.next);
  } else if (input.any_pressed(script_.cancel_buttons)) {
    const int exit = item_index(script_.items, script_.cancel_item);
    if (exit >= 0 && exit != cursor_) {
      move_to(exit);
    }
  } else {
    for (const auto &[buttons, id] : script_.shortcuts) {
      const int item = item_index(script_.items, id);
      if (input.any_pressed(buttons) && item != cursor_) {
        move_to(item);
        break;
      }
    }
  }
}

// The original draws the eight slots from the one before the wheel's item
// to six past it, each the item at that slot modulo the count. Turning, the
// wheel sweeps from the old item to the cursor's, the short way round across
// the first and last items (0x1b847, 0x1c809); a shortcut sweeps the whole
// way at once.
std::vector<std::pair<int, float>> LocationMenu::wedges() const {
  const LocationWheel &wheel = script_.wheel;
  const int count = static_cast<int>(script_.items.size());
  float sweep = static_cast<float>(shown_ - cursor_) * wheel.step_degrees;
  if (shown_ == 0 && cursor_ == count - 1) {
    sweep = wheel.step_degrees;
  } else if (shown_ == count - 1 && cursor_ == 0) {
    sweep = -wheel.step_degrees;
  }
  const auto progress =
      turning() ? static_cast<float>(std::min(turn_ / wheel.turn_seconds, 1.0)) : 0.0F;
  std::vector<std::pair<int, float>> result;
  for (int slot = shown_ - 1; slot < shown_ - 1 + wheel.slots; ++slot) {
    const int item = ((slot % count) + count) % count;
    const float angle = progress * sweep + static_cast<float>(slot - shown_) * wheel.step_degrees;
    result.emplace_back(item, angle);
  }
  return result;
}

} // namespace phantomcrash
