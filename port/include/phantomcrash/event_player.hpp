#pragma once

// The original's event script player (0xc5a1a) and the talk scene around it.

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/story_event.hpp"

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace phantomcrash {

// Timings and controls of the talk scenes (data/story/talk.json). Frame
// counts are the original's, at 60 frames a second.
struct EventTuning {
  double frame_rate{60.0};
  double fade_in{0.5};  // the scene, before the script starts (0xd0ec8)
  double fade_out{0.5}; // after it ends (0xd0f3f)
  int character_fade_frames{30};
  int blink_wait_frames{60}; // eyes open for this, plus up to as long again
  int blink_frames{8};       // closed for this, plus up to blink_random more
  int blink_random{4};
  int mouth_hold_frames{5};      // frames a mouth shape holds before the next check
  int speaking_glyph_above{146}; // glyphs past the punctuation move the mouth
  int confirm_delay_frames{2};
  Buttons confirm_buttons{};
  Buttons fast_forward_buttons{};
  std::string type_sound;
  std::string page_sound;
  std::string skip_sound;
  // The day, money, music meter and CURRENT MUSIC line (0xd0fc8, 0x2a85a).
  FramePlacement frame;
};

[[nodiscard]] EventTuning parse_event_tuning(const json::JsonValue &document);

// What the script draws this frame. `visible` counts typing steps shown,
// or is -1 for the whole text; `alpha` fades sprites and characters.
struct TalkSprite {
  Sprite sprite;
  float alpha{1.0F};
};
struct TalkText {
  const EventText *text{};
  int visible{-1};
};
struct TalkFrame {
  std::vector<TalkSprite> sprites;
  std::vector<TalkText> texts;
};

// How many tokens of `text` are drawn with `visible` typing steps (all for
// -1), and whether that reaches the end of the text (0xbd720).
struct TextProgress {
  std::size_t tokens{};
  bool complete{};
};
[[nodiscard]] TextProgress text_progress(const std::vector<TextToken> &tokens, int visible);

// Runs a script frame by frame as the original does: each frame re-runs the
// commands since the last finished wait, drawing what they draw, and stops
// at the first command that is not done (typing text, a wait, the wait for
// confirm).
class EventPlayer final {
public:
  EventPlayer(const StoryEvent &event, const TalkTables &talk, EventTuning tuning,
              std::uint32_t seed = 1);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] bool finished() const noexcept {
    return finished_;
  }
  [[nodiscard]] const TalkFrame &frame() const noexcept {
    return frame_;
  }
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  // The furthest of the event's commands reached.
  [[nodiscard]] std::size_t command() const noexcept {
    return reached_;
  }
  [[nodiscard]] std::vector<std::string> take_sounds();

private:
  struct CastState {
    int blink{};      // frames the eyes stay closed
    int blink_wait{}; // frames until the next blink
    int mouth_timer{};
    int mouth_shape{}; // 0 closed, 1 half, 2 open, 3 half
  };

  void step(Buttons pressed, Buttons held);
  bool run_text(std::size_t index, const EventText &text, bool confirm, bool fast);
  bool run_wait(int frames, bool fast);
  bool run_hold(std::size_t index, int frames, bool fast);
  bool run_confirm(std::size_t index, bool confirm, bool fast);
  void run_character(const EventCharacter &command);
  void typed_unit(const EventText &text);
  void add_part(const TalkCharacter &character, const TalkPart &part, const EventCharacter &command,
                int layer, float alpha);
  [[nodiscard]] int cast_slot(int character) const;
  [[nodiscard]] int random(int below);
  void sound(const std::string &name);

  const StoryEvent &event_;
  const TalkTables &talk_;
  EventTuning tuning_;
  std::minstd_rand random_;
  double pending_{};
  double seconds_{};
  std::size_t checkpoint_{}; // the command after the last finished wait
  std::size_t reached_{};    // the furthest command reached
  bool finished_{};
  int text_frames_{};
  int wait_frames_{};
  int confirm_frames_{};
  int typed_{};
  bool skip_{};
  int fade_in_{};
  int fade_out_{};
  bool fade_in_started_{};
  bool fade_out_started_{};
  bool speaking_{};
  std::vector<CastState> cast_;
  TalkFrame frame_;
  std::vector<std::string> sounds_;
};

// A talk scene: the background fades in, the script plays over it, then the
// scene fades out (the script no longer drawn) to `next`.
class TalkScene final {
public:
  TalkScene(const StoryEvent &event, const TalkTables &talk, EventTuning tuning, std::string next,
            std::uint32_t seed = 1);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const StoryEvent &event() const noexcept {
    return event_;
  }
  [[nodiscard]] const TalkBackground *background() const;
  // The script's frame, or null while the scene fades in or out.
  [[nodiscard]] const TalkFrame *frame() const;
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] float black_alpha() const noexcept {
    return fade_.black_alpha();
  }
  [[nodiscard]] std::size_t command() const noexcept {
    return player_.command();
  }
  [[nodiscard]] bool done() const noexcept {
    return fade_.done();
  }
  [[nodiscard]] const std::string &next() const {
    return fade_.next();
  }
  [[nodiscard]] std::vector<std::string> take_sounds() {
    return player_.take_sounds();
  }

private:
  const StoryEvent &event_;
  const TalkTables &talk_;
  std::string next_;
  ScreenFade fade_;
  EventPlayer player_;
  double seconds_{};
  bool started_{};
};

} // namespace phantomcrash
