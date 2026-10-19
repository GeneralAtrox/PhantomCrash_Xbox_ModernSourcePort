#include "phantomcrash/load_game.hpp"
#include "phantomcrash/money.hpp"
#include "phantomcrash/shop_pointer.hpp"

#include <algorithm>

namespace phantomcrash {

std::vector<Sprite> saved_time_sprites(const LoadGameScript &script, const SaveSlotStyle &slot,
                                       const std::array<int, 6> &time) {
  return saved_time_sprites(script.saved_time, slot.date, slot.time, time);
}

std::vector<Sprite> details_money_sprites(const NumberFont &font, const SaveDetailsStyle &style,
                                          const std::int64_t money) {
  NumberStyle number;
  number.right = {0.0F, style.money[1]};
  number.digits = style.money_digits;
  number.commas = true;
  number.colour = style.money_colour;
  float width = 0.0F;
  std::vector<Sprite> sprites =
      number_sprites_from(font, number, style.money[0], shown_ny(money), style.number_layer, width);
  Sprite unit = style.money_unit;
  unit.position = {style.money[0] + width + 1.0F, style.money[1]};
  unit.layer = style.number_layer;
  sprites.push_back(std::move(unit));
  return sprites;
}

LoadGame::LoadGame(LoadGameScript script, std::vector<SaveSlot> slots, MessageBoxStyle box,
                   SaveLoadHooks hooks, const bool unreadable)
    : script_(std::move(script)), slots_(std::move(slots)), box_(std::move(box)),
      hooks_(std::move(hooks)), fade_(script_.fade_in, script_.fade_out),
      slot_repeat_(script_.repeat_delay, script_.repeat_interval),
      row_repeat_(script_.repeat_delay, script_.repeat_interval) {
  slots_.resize(script_.slots.size());
  // The cursor starts on the latest save (0x28533).
  int latest = -1;
  for (int index = 0; index < static_cast<int>(slots_.size()); ++index) {
    const auto &quest = slots_[static_cast<std::size_t>(index)].quest;
    if (slots_[index].selectable && quest &&
        (latest < 0 || quest->saved > slots_[static_cast<std::size_t>(latest)].quest->saved)) {
      latest = index;
    }
  }
  if (unreadable && !script_.unreadable.empty()) {
    phase_ = Phase::no_records;
    box_.open(MessageBox::Kind::notice, script_.unreadable);
    sound(box_.style().open_sound);
  } else if (latest < 0 && script_.saving) {
    const auto first = std::ranges::find(slots_, true, &SaveSlot::selectable);
    slot_ = first == slots_.end() ? 0 : static_cast<int>(first - slots_.begin());
  } else if (latest < 0) {
    phase_ = Phase::no_records;
    box_.open(MessageBox::Kind::notice, script_.no_records);
    sound(box_.style().open_sound);
  } else {
    slot_ = latest;
  }
}

void LoadGame::sound(const std::string &name) {
  if (!name.empty()) {
    sounds_.push_back(name);
  }
}

void LoadGame::update(const double tick_seconds, const InputFrame &input) {
  if (done()) {
    return;
  }
  seconds_ += tick_seconds;
  box_.update(tick_seconds);
  if (!fade_.update(tick_seconds) || fade_.leaving()) {
    return;
  }
  // The mouse (a port addition): a click on a notice's OK closes it as
  // confirm does.
  const bool confirmed =
      input.any_pressed(script_.confirm_buttons) ||
      (input.clicked && box_.is_open() && box_.kind() == MessageBox::Kind::notice && box_.grown() &&
       shop_hit(input, box_.style().ok.normal));
  switch (phase_) {
  case Phase::no_records:
    if (confirmed) {
      sound(script_.select_sound);
      fade_.leave(script_.exit_to);
    }
    return;
  case Phase::loaded:
    if (confirmed) {
      sound(script_.select_sound);
      fade_.leave(script_.loaded_to);
    }
    return;
  case Phase::question:
    answer(tick_seconds, input);
    return;
  case Phase::saved:
    if (confirmed) {
      sound(script_.select_sound);
      box_.close();
      phase_ = Phase::slots;
    }
    return;
  case Phase::slots:
  case Phase::exit:
    break;
  }
  // The cursor moves first, then confirm, then cancel (0x28842). Pointing at
  // a slot or EXIT moves the cursor there, and a click confirms it.
  move(tick_seconds, input);
  const std::optional<int> pointed = pointed_choice(input);
  if (pointed && (input.pointer_moved || input.clicked)) {
    point_at(*pointed);
  }
  if (confirmed || (input.clicked && pointed)) {
    sound(script_.select_sound);
    confirm();
  } else if (phase_ == Phase::slots && input.any_pressed(script_.cancel_buttons)) {
    phase_ = Phase::exit;
    sound(script_.row_sound);
  }
}

void LoadGame::move(const double tick_seconds, const InputFrame &input) {
  bool fresh = false;
  if (phase_ == Phase::slots) {
    const int step =
        slot_repeat_.step(tick_seconds, input, script_.slot_back, script_.slot_forward, fresh);
    std::vector<int> choices;
    for (int index = 0; index < static_cast<int>(slots_.size()); ++index) {
      if (slots_[index].selectable) {
        choices.push_back(index);
      }
    }
    const auto current = std::ranges::find(choices, slot_);
    const int at = current == choices.end() ? 0 : static_cast<int>(current - choices.begin());
    const int moved = choices.empty()
                          ? slot_
                          : choices[move_cursor(at, step, fresh, static_cast<int>(choices.size()))];
    if (moved != slot_) {
      slot_ = moved;
      sound(script_.slot_sound);
      return;
    }
  }
  const int step =
      row_repeat_.step(tick_seconds, input, script_.row_back, script_.row_forward, fresh);
  const int row = phase_ == Phase::exit ? 1 : 0;
  if (const int moved = move_cursor(row, step, fresh, 2); moved != row) {
    phase_ = moved == 1 ? Phase::exit : Phase::slots;
    sound(script_.row_sound);
  }
}

std::optional<int> LoadGame::pointed_choice(const InputFrame &input) const {
  if (!input.pointer) {
    return std::nullopt;
  }
  if (shop_hit(input, script_.exit.normal)) {
    return exit_choice;
  }
  for (int index = 0; index < static_cast<int>(slots_.size()); ++index) {
    if (slots_[index].selectable && shop_hit(input, script_.slots[index].plate)) {
      return index;
    }
  }
  return std::nullopt;
}

void LoadGame::point_at(const int choice) {
  if (choice == exit_choice) {
    if (phase_ != Phase::exit) {
      phase_ = Phase::exit;
      sound(script_.row_sound);
    }
    return;
  }
  if (phase_ == Phase::exit) {
    phase_ = Phase::slots;
    slot_ = choice;
    sound(script_.row_sound);
  } else if (choice != slot_) {
    slot_ = choice;
    sound(script_.slot_sound);
  }
}

void LoadGame::confirm() {
  if (phase_ == Phase::exit) {
    fade_.leave(script_.exit_to);
    return;
  }
  if (!slots_[static_cast<std::size_t>(slot_)].selectable) {
    return;
  }
  const bool used = slots_[static_cast<std::size_t>(slot_)].quest.has_value();
  if (script_.saving) {
    if (used) {
      phase_ = Phase::question;
      box_.open(MessageBox::Kind::question, script_.overwrite, 1);
      sound(box_.style().open_sound);
    } else {
      write();
    }
    return;
  }
  // An empty slot takes the confirm sound and nothing else (0x28db3).
  if (used) {
    if (hooks_.load) {
      hooks_.load(slot_);
    }
    phase_ = Phase::loaded;
    box_.open(MessageBox::Kind::notice, script_.loaded);
    sound(box_.style().open_sound);
  }
}

// 0x06b4bd writes the slot; the notice tells whether it could.
void LoadGame::write() {
  std::optional<QuestSave> written = hooks_.save ? hooks_.save(slot_) : std::nullopt;
  const bool saved = written.has_value();
  if (saved) {
    slots_[static_cast<std::size_t>(slot_)].quest = std::move(written);
  }
  phase_ = Phase::saved;
  box_.open(MessageBox::Kind::notice, saved ? script_.saved : script_.failed);
  sound(box_.style().open_sound);
}

// Overwrite?: YES writes, NO goes back to the slots.
void LoadGame::answer(const double tick_seconds, const InputFrame &input) {
  if (!box_.grown()) {
    return;
  }
  bool fresh = false;
  if (const int step =
          slot_repeat_.step(tick_seconds, input, script_.slot_back, script_.slot_forward, fresh)) {
    const int choice = std::clamp(box_.choice() + step, 0, 1);
    if (choice != box_.choice()) {
      box_.set_choice(choice);
      sound(script_.slot_sound);
    }
    return;
  }
  // A click on YES or NO answers with it.
  bool clicked = false;
  if (input.clicked) {
    const MessageBoxStyle &style = box_.style();
    const int choice = shop_hit(input, style.yes.normal)  ? 0
                       : shop_hit(input, style.no.normal) ? 1
                                                          : -1;
    if (choice >= 0) {
      clicked = true;
      if (choice != box_.choice()) {
        box_.set_choice(choice);
        sound(script_.slot_sound);
      }
    }
  }
  if (input.any_pressed(script_.confirm_buttons) || clicked) {
    sound(script_.select_sound);
    if (box_.choice() == 0) {
      write();
    } else {
      box_.close();
      phase_ = Phase::slots;
    }
  } else if (input.any_pressed(script_.cancel_buttons) && box_.choice() != 1) {
    box_.set_choice(1);
    sound(script_.to_no_sound);
  }
}

} // namespace phantomcrash
