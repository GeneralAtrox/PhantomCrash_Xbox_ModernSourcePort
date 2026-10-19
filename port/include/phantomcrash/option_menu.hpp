#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/message_box.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// What an option item does when confirmed.
enum class OptionAction {
  go_to,  // fade out to another screen
  toggle, // ask a YES/NO question and set an on/off option from the answer
  reset,  // ask, then restore every option's default and say so
  quit,   // ask YES/NO, then fade out to go_to (YES) or stay (NO)
  exit,   // fade out to where the option screen returns
};

// A button whose plate is drawn in code at the item's place and size, with
// lettering from a data/ui/sprite_text.json display font, instead of the
// atlas art. A port addition (the original has no such button).
struct OptionPlate {
  std::string text;
  std::string font = "futura";
};

struct OptionItem {
  std::string id;
  Sprite normal;
  Sprite selected; // over `normal` while the cursor is on the item
  HelpLine help;
  OptionAction action{OptionAction::go_to};
  std::string go_to;
  std::string option;                // toggle: the GameOptions member
  std::vector<std::string> question; // toggle, reset, quit
  std::vector<std::string> done;     // reset: the notice after YES
  int default_choice{1};             // reset: 0 YES, 1 NO
  std::optional<OptionPlate> plate;  // drawn in place of the sprites when set
};

// data/ui/option.json (original scene 26).
struct OptionScript : MenuBasics, MenuTickers {
  std::vector<OptionItem> items;
  int default_cursor{};
  int cancel_cursor{}; // where a cancel press moves the cursor
  // The RIDING SV plate's height, for a menu that shows it (the system
  // menu's, 0x6dd74).
  std::optional<float> riding_plate_y;
};

// Throws std::runtime_error naming the document (`name`) and entry at fault.
[[nodiscard]] OptionScript parse_option(const json::JsonValue &document,
                                        const std::string &name = "option.json");

// The original artist biography, including glyph/colour markup and line breaks.
struct ArtistProfile {
  std::string text;
  std::array<float, 2> position{};
  std::uint32_t colour{0xffffff};
  int layer{};
};

// A track's credit from the converted tables/music.json.
struct MusicCredit {
  std::string file; // e.g. "sound/5GARAGE.WAV"
  std::string title;
  std::string artist;
  // The CD jacket, large and small (the converted credit's image and rect;
  // no position or layer), when the credit has one.
  std::optional<Sprite> jacket;
  std::optional<Sprite> jacket_small;
  // The artist's name and web address, as images (Sonic Amp's preview).
  std::optional<Sprite> artist_name;
  std::optional<Sprite> artist_link;
  std::optional<ArtistProfile> artist_profile;
};

[[nodiscard]] std::vector<MusicCredit> parse_music_credits(const json::JsonValue &document);
// The credit line for a music file (names compare without regard to case),
// or empty when the file has no credit, as the original shows nothing then.
[[nodiscard]] std::string credit_line(const std::vector<MusicCredit> &credits,
                                      const std::string &file, const std::string &format);

// The OPTION screen: a vertical list whose help line scrolls along the top,
// with message boxes for the on/off option and DEFAULT. Follows the
// original's tasks 0x2e9eb (list), 0x2e87f (vibration) and 0x2e923/0x2eb23
// (default): the cursor moves first, then confirm is checked, then cancel,
// which moves the cursor to EXIT (or a box's choice to NO) instead of leaving.
class OptionMenu final {
public:
  OptionMenu(OptionScript script, MessageBoxStyle box, GameOptions options,
             const std::string &returning_from = {});

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] bool done() const noexcept {
    return fade_.done();
  }
  [[nodiscard]] const std::string &next() const {
    return fade_.next();
  }
  [[nodiscard]] std::vector<std::string> take_sounds();
  [[nodiscard]] const std::string &music() const noexcept {
    return script_.music;
  }

  [[nodiscard]] const OptionScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] int cursor() const noexcept {
    return cursor_;
  }
  // The selected item pulses only while the list takes input.
  [[nodiscard]] bool cursor_pulses() const noexcept {
    return phase_ == Phase::list && !fade_.fading_in() && !fade_.leaving();
  }
  // Seconds the help line of the cursor's item has been scrolling.
  [[nodiscard]] double help_seconds() const {
    return help_.seconds(cursor_);
  }
  [[nodiscard]] const MessageBox &box() const noexcept {
    return box_;
  }
  [[nodiscard]] const GameOptions &options() const noexcept {
    return options_;
  }
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] bool faded_in() const noexcept {
    return !fade_.fading_in();
  }
  [[nodiscard]] float black_alpha() const noexcept {
    return fade_.black_alpha();
  }

private:
  enum class Phase { list, question, notice };

  void update_list(double tick_seconds, const InputFrame &input);
  void update_box(double tick_seconds, const InputFrame &input);
  void open_box(MessageBox::Kind kind, const std::vector<std::string> &lines, int choice);
  void close_box();
  void sound(const std::string &name);

  OptionScript script_;
  MessageBox box_;
  GameOptions options_;
  ScreenFade fade_;
  Phase phase_{Phase::list};
  int cursor_{};
  int reset_choice_{1};
  HelpClocks help_;
  CursorRepeat repeat_;
  double seconds_{};
  std::vector<std::string> sounds_;
};

} // namespace phantomcrash
