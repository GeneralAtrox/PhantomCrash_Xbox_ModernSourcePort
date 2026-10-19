#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/story_event.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// A question on Pepper's list: a topic opening its own questions, the end of
// the talk, or the screen (a talk scene) that answers it, another once the
// game has been cleared.
struct GuideQuestion {
  std::string text;
  std::string next;
  std::string cleared_next;
  bool end{};
  std::vector<GuideQuestion> questions;
};

// data/ui/pepper_guide.json (original scene 19).
struct PepperGuideScript : MenuBasics {
  FramePlacement frame;
  EventCharacter guide;        // Pepper, shown as a talk scene shows her
  int rows{5};                 // questions shown at once
  float row_step{30.0F};       // between the rows
  std::array<float, 2> text{}; // the first question's text
  int text_layer{8};
  std::uint32_t text_colour{0xffffff};
  Sprite cursor;     // the bar under the cursor's row, at its first row
  Sprite more_above; // the arrows, while a list is longer than the rows
  Sprite more_below;
  std::vector<Sprite> topic_layers; // shown inside a topic (the back button hint)
  Buttons up_buttons{};
  Buttons down_buttons{};
  std::string back_sound;                       // cancel inside a topic
  std::map<std::string, std::string> end_exits; // screen the guide opened from -> End's exit
  std::vector<GuideQuestion> questions;
};

// Throws std::runtime_error naming the document and entry at fault.
[[nodiscard]] PepperGuideScript parse_pepper_guide(const json::JsonValue &document);

// What the guide keeps between visits, as the original keeps it in globals
// (0x415e80-0x415e9c): the topics open, each level's cursor row and scroll,
// and the screen it was opened from.
struct GuideMemory {
  struct Level {
    int row{};
    int scroll{};
  };
  std::vector<int> open;                             // the topic opened at each level
  std::vector<Level> levels = std::vector<Level>(1); // one more than the topics open
  std::string origin;
};

// Pepper's guide (0x298a4, controls 0x29587, draw 0x292da): a list of
// questions that scrolls round, five rows at a time. Confirm opens a topic's
// questions or fades out to the answer's talk scene, which comes back here;
// cancel closes the topic, or at the top moves the cursor to End. End fades
// out to where the guide leads from the screen it was opened from.
class PepperGuide final {
public:
  // `cleared`: the game has been cleared, for the answers that change then.
  PepperGuide(PepperGuideScript script, GuideMemory &memory, const std::string &from,
              bool cleared = false);

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
  [[nodiscard]] const PepperGuideScript &script() const noexcept {
    return script_;
  }
  // The questions of the open topic (the top list when none is).
  [[nodiscard]] const std::vector<GuideQuestion> &questions() const;
  [[nodiscard]] bool in_topic() const noexcept {
    return !memory_.open.empty();
  }
  // The question at each row shown, top to bottom.
  [[nodiscard]] std::vector<int> rows() const;
  [[nodiscard]] int cursor_row() const noexcept {
    return level().row;
  }
  // The question under the cursor.
  [[nodiscard]] int selected() const;
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] float black_alpha() const noexcept {
    return fade_.black_alpha();
  }
  [[nodiscard]] bool faded_in() const noexcept {
    return !fade_.fading_in();
  }

private:
  [[nodiscard]] const GuideMemory::Level &level() const {
    return memory_.levels[memory_.open.size()];
  }
  [[nodiscard]] GuideMemory::Level &level() {
    return memory_.levels[memory_.open.size()];
  }
  [[nodiscard]] int shown() const;
  void move(int step);
  void confirm();
  void cancel();
  void sound(const std::string &name);

  PepperGuideScript script_;
  GuideMemory &memory_;
  bool cleared_;
  ScreenFade fade_;
  CursorRepeat repeat_;
  double seconds_{};
  std::vector<std::string> sounds_;
};

} // namespace phantomcrash
