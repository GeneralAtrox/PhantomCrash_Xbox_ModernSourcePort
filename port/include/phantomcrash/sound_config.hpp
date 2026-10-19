#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// The gain for a 0-255 volume setting. The original turns it into
// DirectSound millibels through a table (0x410400, converted as
// tables/music.json volume_millibels); without the table a curve that
// matches it to a few millibels stands in. Level 0 is silence.
[[nodiscard]] float volume_gain(int level, const std::vector<int> &millibels = {});

// A number drawn with digit sprites, right-aligned to a point and padded
// with zeros (the original's number routine 0xbc6d2 with flags 0x18).
struct DigitStyle {
  std::string image;
  std::array<std::array<float, 4>, 10> rects{}; // 0-9
  float advance{11.0F};
  int digits{3};
  int layer{0x108};
  std::uint32_t colour{0xffffff};
};

// A value's bar: `rect` stretched to `per_step` units a step (0x3b260).
struct ValueBar {
  Sprite sprite;
  float per_step{1.0F};
};

enum class SoundAction {
  volume, // left and right change a volume, heard at once
  test,   // left and right pick an effect, confirm plays it
  exit,
};

struct SoundItem {
  std::string id;
  Sprite normal;
  Sprite selected;
  HelpLine help;
  SoundAction action{SoundAction::exit};
  std::string option;            // volume: the GameOptions member
  int count{1};                  // test: effects to pick from (the volumes are 0-255)
  bool wrap{true};               // a new press wraps around the ends (flags 0x17)
  std::array<float, 2> number{}; // the right end of the value's digits
  std::optional<ValueBar> bar;
};

// Where an effect number is found: groups of numbered files in order.
struct TestGroup {
  std::string folder; // e.g. "sound/sfx/sys"
  int count{};
};

// data/ui/sound_config.json (original scene 35).
struct SoundConfigScript : MenuBasics, MenuTickers {
  double value_delay{};
  double value_interval{};
  DigitStyle digits;
  std::vector<TestGroup> test_groups;
  std::vector<SoundItem> items;
  int default_cursor{};
  int cancel_cursor{};
};

// Throws std::runtime_error naming the document and entry at fault.
[[nodiscard]] SoundConfigScript parse_sound_config(const json::JsonValue &document);

// The file of the test effect with a number ("sound/sfx/sys/05.wav"), or
// empty past the last group.
[[nodiscard]] std::string test_sound_file(const std::vector<TestGroup> &groups, int number);

// SOUND CONFIG: BGM and SE volume, an SE test and EXIT (tasks 0x3b52d and
// 0x3b426). Up and down move the cursor, left and right change the item's
// value; the volumes take effect as they change and are kept on EXIT, the
// only way out. Cancel moves the cursor to EXIT.
class SoundConfigMenu final {
public:
  SoundConfigMenu(SoundConfigScript script, GameOptions options);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] bool done() const noexcept {
    return fade_.done();
  }
  [[nodiscard]] const std::string &next() const {
    return fade_.next();
  }
  // Effect names, or a test effect's file (a name with '/').
  [[nodiscard]] std::vector<std::string> take_sounds();
  [[nodiscard]] const std::string &music() const noexcept {
    return script_.music;
  }

  [[nodiscard]] const SoundConfigScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] int cursor() const noexcept {
    return cursor_;
  }
  // The value an item shows (a volume, the test effect, or 0 for EXIT).
  [[nodiscard]] int value(int item) const;
  [[nodiscard]] double help_seconds() const {
    return help_.seconds(cursor_);
  }
  [[nodiscard]] const GameOptions &options() const noexcept {
    return options_;
  }
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] float black_alpha() const noexcept {
    return fade_.black_alpha();
  }

private:
  [[nodiscard]] int *value_of(const SoundItem &item);
  void sound(const std::string &name);

  SoundConfigScript script_;
  GameOptions options_;
  ScreenFade fade_;
  int cursor_{};
  int test_{};
  HelpClocks help_;
  CursorRepeat repeat_;
  CursorRepeat value_repeat_;
  double seconds_{};
  std::vector<std::string> sounds_;
};

} // namespace phantomcrash
