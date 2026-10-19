#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/saved_time.hpp"
#include "phantomcrash/message_box.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// One save slot on the LOAD GAME screen (0x28b25, 0x6b76b): its plate, the
// plate lit while chosen, the EMPTY cover, where the save's date and time
// start, and the CLEAR mark of a cleared game.
struct SaveSlotStyle {
  Sprite plate;
  Sprite lit;
  Sprite empty;
  std::array<float, 2> date{}; // "YYYY/MM/DD", left to right from here
  std::array<float, 2> time{}; // "HH:MM:SS"
  Sprite clear;
};

// The chosen slot's details in the panel below the slots (0x6b65a): the
// player's name, the Scoobee ridden, "DAY-" and the day, and the money
// with "NY" after it.
struct SaveDetailsStyle {
  std::array<float, 2> name{};
  std::array<float, 2> riding{};
  int max_glyphs{9};
  std::uint32_t text_colour{0xffffff};
  int text_layer{258};
  Sprite day_label;
  NumberStyle day;              // right-aligned, as the town frame's
  std::array<float, 2> money{}; // the money's left edge and top
  int money_digits{9};
  std::uint32_t money_colour{0xf0f0f0};
  Sprite money_unit; // placed one unit after the money
  int number_layer{0x102};
};

// data/ui/load_game.json (original scene 17), and data/ui/save_game.json
// (SAVE, scene 31) with the same slots.
struct LoadGameScript : MenuBasics {
  bool saving{}; // "mode": "save"
  TickerStyle help;
  std::string help_text;
  FramePlacement frame;
  std::vector<SaveSlotStyle> slots;
  std::array<float, 2> profile_offset{32, 32};
  std::array<float, 2> profile_size{64, 64};
  int profile_layer{258};
  ChoiceSprites exit; // the EXIT button, and its lit form
  SaveDetailsStyle details;
  SavedTimeStyle saved_time;
  Buttons slot_back{}; // the slot cursor moves (left, right)
  Buttons slot_forward{};
  Buttons row_back{}; // between the slots and EXIT (up, down)
  Buttons row_forward{};
  std::string slot_sound;
  std::string row_sound;
  std::string to_no_sound;             // cancel moves a question to NO
  std::vector<std::string> no_records; // no save to load
  std::vector<std::string> unreadable; // the saves could not be read (0x21ccdc)
  std::vector<std::string> loaded;     // after a slot is chosen
  std::string loaded_to;
  std::vector<std::string> overwrite; // asks before a save is written over
  std::vector<std::string> saved;
  std::vector<std::string> failed;
};

// Throws std::runtime_error naming the document and entry at fault.
[[nodiscard]] LoadGameScript parse_load_game(const json::JsonValue &document);

// A slot's save, when it holds one.
struct SaveSlot {
  std::optional<QuestSave> quest;
  bool selectable{true};
  std::string owner;
};

// The slot's save's date and time (0xbcfe2, 0xbd255).
[[nodiscard]] std::vector<Sprite> saved_time_sprites(const LoadGameScript &script,
                                                     const SaveSlotStyle &slot,
                                                     const std::array<int, 6> &time);

// The money left-aligned at the details' place, then "NY" (0xb1e39).
[[nodiscard]] std::vector<Sprite>
details_money_sprites(const NumberFont &font, const SaveDetailsStyle &style, std::int64_t money);

// What choosing a slot does: `load` takes the slot's game, `save` writes
// the game to the slot and gives back the save written, or none when it
// failed.
struct SaveLoadHooks {
  std::function<void(int slot)> load;
  std::function<std::optional<QuestSave>(int slot)> save;
};

// The LOAD GAME screen (0x28533, 0x28842). With no save it shows only the
// "no records" notice and confirm leaves. Otherwise the cursor starts on the
// latest save; left/right choose a slot and up/down move between the slots
// and EXIT, whose cancel also goes to EXIT. Confirm on a save loads it and
// shows the "loaded" notice, then leads on; on EXIT it leaves.
//
// SAVE (0x035097, 0x035242) is the same screen writing instead: confirm on
// an empty slot saves, on a used one asks first (NO chosen; left/right
// choose, cancel moves to NO), then tells it was saved or failed, and
// confirm goes back to the slots.
//
// When the saves could not be read, either screen shows only the
// "unreadable" notice, and confirm leaves (0x2877a, 0x351b9).
class LoadGame final {
public:
  enum class Phase { no_records, slots, exit, loaded, question, saved };

  LoadGame(LoadGameScript script, std::vector<SaveSlot> slots, MessageBoxStyle box,
           SaveLoadHooks hooks = {}, bool unreadable = false);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] bool done() const noexcept {
    return fade_.done();
  }
  [[nodiscard]] const std::string &next() const {
    return fade_.next();
  }
  [[nodiscard]] std::vector<std::string> take_sounds() {
    return std::exchange(sounds_, {});
  }
  [[nodiscard]] const LoadGameScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const std::vector<SaveSlot> &slots() const noexcept {
    return slots_;
  }
  [[nodiscard]] Phase phase() const noexcept {
    return phase_;
  }
  [[nodiscard]] int slot() const noexcept {
    return slot_;
  }
  [[nodiscard]] const MessageBox &box() const noexcept {
    return box_;
  }
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] float black_alpha() const noexcept {
    return fade_.black_alpha();
  }

private:
  void sound(const std::string &name);
  void confirm();
  void write();
  void answer(double tick_seconds, const InputFrame &input);
  void move(double tick_seconds, const InputFrame &input);
  // The selectable slot or EXIT under the mouse (a port addition).
  static constexpr int exit_choice = -1;
  [[nodiscard]] std::optional<int> pointed_choice(const InputFrame &input) const;
  void point_at(int choice);

  LoadGameScript script_;
  std::vector<SaveSlot> slots_;
  MessageBox box_;
  SaveLoadHooks hooks_;
  ScreenFade fade_;
  CursorRepeat slot_repeat_;
  CursorRepeat row_repeat_;
  Phase phase_{Phase::slots};
  int slot_{};
  double seconds_{};
  std::vector<std::string> sounds_;
};

} // namespace phantomcrash
