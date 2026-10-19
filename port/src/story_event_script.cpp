// Parsing of the converted tables/events.json and tables/talk.json.

#include "phantomcrash/story_event.hpp"
#include "phantomcrash/story_progression.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

constexpr std::uint32_t player_name_colour = 0x66eeee; // 0xbd720, code -5

// The length of the UTF-8 sequence that starts with `byte`.
std::size_t utf8_length(const unsigned char byte) {
  if (byte >= 0xf0U) {
    return 4;
  }
  if (byte >= 0xe0U) {
    return 3;
  }
  return byte >= 0xc0U ? 2 : 1;
}

void markup_code(std::string_view code, std::vector<TextToken> &tokens) {
  if (code == "/") {
    tokens.push_back({TextToken::Kind::reset_colour});
  } else if (code == "name") {
    tokens.push_back({TextToken::Kind::player_name, 0, player_name_colour});
  } else if (code.size() == 7 && code.front() == '#') {
    tokens.push_back({TextToken::Kind::colour, 0, parse_colour(std::string(code))});
  } else if (code.starts_with("glyph:")) {
    int glyph = 0;
    const auto digits = code.substr(6);
    std::from_chars(digits.data(), digits.data() + digits.size(), glyph);
    tokens.push_back({TextToken::Kind::glyph, std::clamp(glyph, 0, 0xffff)});
  } else if (!code.starts_with("code:")) {
    throw std::runtime_error("unknown text code {" + std::string(code) + "}");
  }
}

Sprite part_sprite(const json::JsonValue &value) {
  Sprite sprite = parse_sprite(value);
  if (const auto *position = value.find("position")) {
    sprite.position = numbers<2>(*position, "position");
  }
  return sprite;
}

std::optional<TalkPart> parse_part(const json::JsonValue *value) {
  if (value == nullptr || value->is_null()) {
    return std::nullopt;
  }
  TalkPart part;
  part.page = static_cast<int>(value->number_or("page", 0));
  part.rect = numbers<4>(required(*value, "rect"), "rect");
  if (const auto *offset = value->find("offset")) {
    part.offset = numbers<2>(*offset, "offset");
  }
  return part;
}

TalkCharacter parse_character(const json::JsonValue &value) {
  TalkCharacter character;
  for (const auto &image : required(value, "images").array()) {
    character.images.push_back(image.string());
  }
  for (const auto &pose : required(value, "poses").array()) {
    if (pose.is_null()) {
      character.poses.emplace_back();
      continue;
    }
    TalkPose parsed;
    parsed.body = *parse_part(&required(pose, "body"));
    const auto &mouth = required(pose, "mouth").array();
    for (std::size_t index = 0; index < parsed.mouth.size() && index < mouth.size(); ++index) {
      parsed.mouth[index] = parse_part(&mouth[index]);
    }
    parsed.blink = parse_part(pose.find("blink"));
    character.poses.emplace_back(parsed);
  }
  return character;
}

EventText parse_text(const json::JsonValue &value, const GlyphFont &font) {
  EventText text;
  text.tokens = parse_event_text(font, required(value, "text").string());
  text.units = text_units(text.tokens);
  text.position = numbers<2>(required(value, "position"), "position");
  text.colour = parse_colour(value.string_or("colour", "#ffffff"));
  text.frames_per_character = static_cast<int>(value.number_or("frames_per_character", 0));
  text.layer = static_cast<int>(value.number_or("layer", text.layer));
  text.animation =
      value.string_or("animation", "none") == "fade" ? Animation::fade : Animation::none;
  text.skippable = value.boolean_or("skippable", true);
  return text;
}

} // namespace

EventCharacter parse_event_character(const json::JsonValue &value) {
  EventCharacter character;
  character.character = static_cast<int>(required(value, "character").number());
  character.pose = static_cast<int>(value.number_or("pose", 0));
  character.position = numbers<2>(required(value, "position"), "position");
  character.layer = static_cast<int>(value.number_or("layer", 0));
  character.colour = parse_colour(value.string_or("colour", "#ffffff"));
  const std::string fade = value.string_or("fade", "none");
  character.fade = fade == "in"    ? CharacterFade::in
                   : fade == "out" ? CharacterFade::out
                                   : CharacterFade::none;
  character.mouth = value.boolean_or("mouth", true);
  return character;
}

namespace {

EventCommand parse_command(const json::JsonValue &value, const GlyphFont &font) {
  EventCommand command;
  const std::string op = required(value, "op").string();
  if (op == "text") {
    command.op = EventOp::text;
    command.text = parse_text(value, font);
  } else if (op == "wait") {
    command.op = EventOp::wait;
    command.frames = static_cast<int>(required(value, "frames").number());
  } else if (op == "sprite") {
    command.op = EventOp::sprite;
    // A sprite picked at run time (a negative id) has no image yet.
    if (const auto *image = value.find("image"); image != nullptr && !image->is_null()) {
      command.sprite = part_sprite(value);
    }
    command.alpha = static_cast<float>(value.number_or("alpha", 1.0));
  } else if (op == "character") {
    command.op = EventOp::character;
    command.character = parse_event_character(value);
  } else if (op == "hold") {
    command.op = EventOp::hold;
    command.frames = static_cast<int>(required(value, "frames").number());
  } else if (op == "wait_for_confirm") {
    command.op = EventOp::wait_for_confirm;
  } else {
    throw std::runtime_error("unknown op \"" + op + "\"");
  }
  return command;
}

// 0xd1861: predicate, contest, area, contest class and day/night conditions.
bool requires_progression(const std::array<int, 6> &bytes) {
  const auto in = [](const int value, const int last) { return value >= 0 && value <= last; };
  return in(bytes[1], 0x4a) || in(bytes[2], 0x37) || in(bytes[3], 3) || in(bytes[4], 0x1b) ||
         in(bytes[5], 1);
}

StoryEvent parse_event(const json::JsonValue &value, const GlyphFont &font) {
  StoryEvent event;
  event.scene = static_cast<int>(required(value, "scene").number());
  event.from_scene = static_cast<int>(value.number_or("from_scene", -1));
  event.to_scene = static_cast<int>(value.number_or("to_scene", -1));
  event.enabled = value.boolean_or("enabled", true);
  if (const auto *seen = value.find("requires_seen")) {
    for (const auto &scene : seen->array()) {
      event.requires_seen.push_back(static_cast<int>(scene.number()));
    }
  }
  if (const auto *conditions = value.find("condition_bytes")) {
    if (conditions->array().size() != event.conditions.size()) {
      throw std::runtime_error("condition_bytes needs 6 bytes");
    }
    for (std::size_t index = 0; index < event.conditions.size(); ++index) {
      const auto &entry = conditions->array()[index];
      if (!entry.is_number() || entry.number() < -128 || entry.number() > 255 ||
          entry.number() != static_cast<int>(entry.number())) {
        throw std::runtime_error("condition_bytes must contain integer bytes");
      }
      const int byte = static_cast<int>(entry.number());
      event.conditions[index] = byte > 127 ? byte - 256 : byte;
    }
    event.unported_conditions = requires_progression(event.conditions);
  }
  event.background = static_cast<int>(value.number_or("background", -1));
  for (const auto &member : required(value, "cast").array()) {
    event.cast.push_back(static_cast<int>(member.number()));
  }
  if (const auto *music = value.find("music"); music != nullptr && !music->is_null()) {
    event.music = music->string();
  }
  event.music_volume = static_cast<float>(value.number_or("music_volume", 1.0));
  event.commands = parse_event_commands(required(value, "commands"), font);
  return event;
}

} // namespace

std::vector<EventCommand> parse_event_commands(const json::JsonValue &commands,
                                               const GlyphFont &font) {
  std::vector<EventCommand> out;
  const auto &list = commands.array();
  for (std::size_t index = 0; index < list.size(); ++index) {
    try {
      out.push_back(parse_command(list[index], font));
    } catch (const std::exception &error) {
      throw std::runtime_error("command " + std::to_string(index) + ": " + error.what());
    }
  }
  return out;
}

std::vector<TextToken> parse_event_text(const GlyphFont &font, const std::string_view markup) {
  std::vector<TextToken> tokens;
  std::size_t index = 0;
  while (index < markup.size()) {
    const char byte = markup[index];
    if (byte == '\n') {
      tokens.push_back({TextToken::Kind::newline});
      ++index;
    } else if (byte == '{' && index + 1 < markup.size() && markup[index + 1] == '{') {
      tokens.push_back({TextToken::Kind::glyph, glyph_run(font, "{").front()});
      index += 2;
    } else if (byte == '{') {
      const auto end = markup.find('}', index);
      if (end == std::string_view::npos) {
        throw std::runtime_error("unclosed { in text");
      }
      markup_code(markup.substr(index + 1, end - index - 1), tokens);
      index = end + 1;
    } else {
      const std::size_t length = utf8_length(static_cast<unsigned char>(byte));
      for (const int glyph : glyph_run(font, markup.substr(index, length))) {
        tokens.push_back({TextToken::Kind::glyph, glyph});
      }
      index += length;
    }
  }
  return tokens;
}

int text_units(const std::vector<TextToken> &tokens) {
  return static_cast<int>(std::ranges::count_if(tokens, [](const TextToken &token) {
    return token.kind != TextToken::Kind::colour && token.kind != TextToken::Kind::reset_colour;
  }));
}

const TalkPose *TalkCharacter::pose(const int index) const {
  if (index >= 0 && std::cmp_less(index, poses.size()) && poses[index]) {
    return &*poses[static_cast<std::size_t>(index)];
  }
  return !poses.empty() && poses.front() ? &*poses.front() : nullptr;
}

std::vector<StoryEvent> parse_story_events(const json::JsonValue &document, const GlyphFont &font) {
  std::vector<StoryEvent> events;
  for (const auto &value : required(document, "events").array()) {
    try {
      events.push_back(parse_event(value, font));
    } catch (const std::exception &error) {
      throw std::runtime_error("events.json: event " +
                               std::to_string(static_cast<int>(value.number_or("scene", -1))) +
                               ": " + error.what());
    }
  }
  return events;
}

TalkTables parse_talk_tables(const json::JsonValue &document) {
  try {
    TalkTables tables;
    for (const auto &value : required(document, "backgrounds").array()) {
      tables.backgrounds.push_back(
          {part_sprite(required(value, "back")), part_sprite(required(value, "front"))});
    }
    for (const auto &value : required(document, "characters").array()) {
      tables.characters.push_back(parse_character(value));
    }
    return tables;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("talk.json: ") + error.what());
  }
}

void apply_talk_edges(const json::JsonValue &tuning, TalkTables &tables) {
  const auto *edges = tuning.find("background_edges");
  if (edges == nullptr) {
    return;
  }
  const auto *back = edges->find("back");
  const auto *front = edges->find("front");
  for (TalkBackground &background : tables.backgrounds) {
    if (back != nullptr) {
      parse_sprite_edges(*back, background.back);
    }
    if (front != nullptr) {
      parse_sprite_edges(*front, background.front);
    }
  }
}

const StoryEvent *event_between(const std::vector<StoryEvent> &events, const int from_scene,
                                const int to_scene, const std::vector<int> &seen,
                                const StoryConditionContext *context) {
  const auto played = [&](const int scene) { return std::ranges::find(seen, scene) != seen.end(); };
  for (const auto &event : events) {
    if (!event.enabled || event.from_scene != from_scene || event.to_scene != to_scene ||
        played(event.scene) || !std::ranges::all_of(event.requires_seen, played) ||
        (event.unported_conditions && (!context || !story_conditions_pass(event, *context)))) {
      continue;
    }
    return &event;
  }
  return nullptr;
}

} // namespace phantomcrash
