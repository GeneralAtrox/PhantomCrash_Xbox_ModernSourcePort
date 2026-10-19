#pragma once

// The story events (talk scenes) as the converter decodes them: the scripts
// of tables/events.json and the backgrounds and characters of
// tables/talk.json. See docs/NEW_GAME.md for the original's event player.

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace phantomcrash {

struct StoryConditionContext;

// One piece of event text. Glyphs, line breaks and the player's name each
// take one step of the typing; colour changes take none (0xbd720).
struct TextToken {
  enum class Kind { glyph, newline, colour, reset_colour, player_name };
  Kind kind{Kind::glyph};
  int glyph{};
  std::uint32_t colour{};
};

// Markup: "\n", "{#rrggbb}", "{/}" (back to the text's colour), "{name}",
// "{glyph:N}" and "{{" for a brace; other "{code:...}" codes are dropped.
[[nodiscard]] std::vector<TextToken> parse_event_text(const GlyphFont &font,
                                                      std::string_view markup);
// Typing steps in a token list.
[[nodiscard]] int text_units(const std::vector<TextToken> &tokens);

// A cut-out of a talking character's texture page, placed from the pose's
// position by `offset` (the faces: mouth and blink).
struct TalkPart {
  int page{};
  std::array<float, 4> rect{};
  std::array<float, 2> offset{};
};

struct TalkPose {
  TalkPart body;
  std::array<std::optional<TalkPart>, 3> mouth; // closed, half open, open
  std::optional<TalkPart> blink;
};

struct TalkCharacter {
  std::vector<std::string> images; // texture pages
  std::vector<std::optional<TalkPose>> poses;

  // A missing pose shows the first one (0xc7a77); null when there is none.
  [[nodiscard]] const TalkPose *pose(int index) const;
};

struct TalkBackground {
  Sprite back;
  Sprite front; // the lower strip, drawn over the characters
};

struct TalkTables {
  std::vector<TalkBackground> backgrounds;
  std::vector<TalkCharacter> characters;
};

// wait ends the page (the commands before it are not drawn again); hold
// waits with the page still drawn, cut short by fast-forward.
enum class EventOp { text, wait, sprite, character, wait_for_confirm, hold };
enum class CharacterFade { none, in, out };

struct EventText {
  std::vector<TextToken> tokens;
  int units{};
  std::array<float, 2> position{};
  std::uint32_t colour{0xffffff};
  int frames_per_character{}; // 0: all at once (the record's size sizes only ruby, unused)
  int layer{0x101};
  Animation animation{Animation::none};
  bool skippable{true};
};

struct EventCharacter {
  int character{};
  int pose{};
  std::array<float, 2> position{};
  int layer{};
  std::uint32_t colour{0xffffff};
  CharacterFade fade{CharacterFade::none};
  bool mouth{true};
};

struct EventCommand {
  EventOp op{EventOp::wait};
  EventText text;
  int frames{}; // wait, hold
  Sprite sprite;
  float alpha{1.0F}; // sprite
  EventCharacter character;
};

struct StoryEvent {
  int scene{};
  int from_scene{};
  int to_scene{};
  bool enabled{true};
  std::vector<int> requires_seen; // scenes of events that must have played
  // Requires progression context; callers without one keep conditional events blocked.
  bool unported_conditions{};
  std::array<int, 6> conditions{1, -1, -1, -1, -1, -1};
  int background{-1};
  std::vector<int> cast; // characters in cast order (blink and mouth state)
  std::string music;     // a WAV under sound/, "keep", or empty for none
  float music_volume{1.0F};
  std::vector<EventCommand> commands;
};

// Throw std::runtime_error naming the entry when a value is missing or wrong.
[[nodiscard]] std::vector<StoryEvent> parse_story_events(const json::JsonValue &document,
                                                         const GlyphFont &font);
[[nodiscard]] TalkTables parse_talk_tables(const json::JsonValue &document);
// story/talk.json's background_edges: the widescreen edges of every talk
// background's back and front (the extraction has none).
void apply_talk_edges(const json::JsonValue &tuning, TalkTables &tables);
// A script's command array, as events.json and tables/lessons.json give it.
[[nodiscard]] std::vector<EventCommand> parse_event_commands(const json::JsonValue &commands,
                                                             const GlyphFont &font);
// One character command ("character", "pose", "position", "layer", "colour",
// "fade", "mouth"), as a script or a screen gives it.
[[nodiscard]] EventCharacter parse_event_character(const json::JsonValue &value);

// The event that plays on the first move from one scene to another, instead
// of the scene it leads to (0xd1861); null when none does.
[[nodiscard]] const StoryEvent *event_between(const std::vector<StoryEvent> &events, int from_scene,
                                              int to_scene, const std::vector<int> &seen,
                                              const StoryConditionContext *context = nullptr);

} // namespace phantomcrash
