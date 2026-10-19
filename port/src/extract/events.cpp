#include "events.hpp"

#include "event_code.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>

namespace phantomcrash::extract {
namespace {

constexpr std::uint32_t event_table = 0x1D0E00; // looked up by scene id (0x0d113b)
constexpr int event_count = 0x6E;
constexpr std::uint32_t event_record = 0x24;
constexpr std::uint32_t talk_characters = 0x1C42E8; // {resource; pose[9]} (0x0c79c0)
constexpr int talk_character_count = 0x1C;
constexpr std::uint32_t talk_poses = 0x1C4748; // 0x1c-byte records (0x0c79e3)
constexpr std::uint32_t talk_backgrounds = 0x1D0DD0; // {back; front}[6] (0x0d0fa2)
constexpr int talk_background_count = 6;
constexpr double talk_front_y = 296.0; // 0x43940000, where the front strip is drawn
constexpr int talk_front_layer = 0x101;
constexpr std::uint32_t character_slot = 534; // talk characters share slots 534-545
constexpr std::uint32_t character_slots = 12;
constexpr std::size_t command_ints = 13;
constexpr std::uint32_t text_bit_no_skip = 0x80000000;
constexpr int music_keep = 0xCA; // "leave the music playing" (0x0d1615)

// Text codes of the text drawer (0x0bd720).
constexpr std::uint16_t text_end = 0xFFFF;
constexpr std::uint16_t text_newline = 0xFFFE;
constexpr std::uint16_t text_player_name = 0xFFFB; // save + 0x280c (0x06d78d)
constexpr std::uint16_t text_reset_colour = 0xFFF7;
const std::map<std::uint16_t, const char *> text_colours{
    {0xFFEE, "#256584"}, {0xFFEF, "#eeee66"}, {0xFFF0, "#66ee33"},
    {0xFFF1, "#8e4500"}, {0xFFF2, "#308080"}, {0xFFF3, "#808080"},
    {0xFFF4, "#ee88ee"}, {0xFFF5, "#ee9500"}, {0xFFF6, "#66eeee"}};

std::string colour(const std::int32_t value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "#%06x", static_cast<unsigned>(value) & 0xFFFFFF);
  return buffer;
}

Json pair_of(const std::vector<std::int32_t> &values, std::size_t first) {
  return array({integer(values[first]), integer(values[first + 1])});
}

Json rect(const Sprite &sprite) {
  return reals({sprite.left, sprite.top, sprite.right, sprite.bottom});
}

// A u16 glyph string as Unicode, with markup for the codes.
std::string glyph_text(const XbeImage &image, const GameTables &tables, std::uint32_t address) {
  const auto &characters = tables.font_characters;
  const auto &first = tables.first_glyph;
  std::string out;
  for (;; address += 2) {
    const std::uint16_t glyph = image.u16(address);
    char code[24];
    if (glyph == text_end) {
      return out;
    }
    if (glyph == text_newline) {
      out += '\n';
    } else if (glyph == text_player_name) {
      out += "{name}";
    } else if (glyph == text_reset_colour) {
      out += "{/}";
    } else if (const auto found = text_colours.find(glyph); found != text_colours.end()) {
      out += std::string("{") + found->second + "}";
    } else if (glyph >= 0xFFE0) {
      std::snprintf(code, sizeof(code), "{code:%04x}", glyph);
      out += code;
    } else if (glyph < characters.size() && first.at(characters[glyph]) == glyph) {
      out += characters[glyph] == "{" ? "{{" : characters[glyph];
    } else {
      std::snprintf(code, sizeof(code), "{glyph:%u}", glyph);
      out += code;
    }
  }
}

// Python's str.strip("　 ").
bool blank_ruby(std::string_view text) {
  constexpr std::string_view ideographic_space = "\xE3\x80\x80";
  while (!text.empty()) {
    if (text.front() == ' ') {
      text.remove_prefix(1);
    } else if (text.starts_with(ideographic_space)) {
      text.remove_prefix(ideographic_space.size());
    } else {
      return false;
    }
  }
  return true;
}

Json text_command(const XbeImage &image, const GameTables &tables,
                  const std::vector<std::int32_t> &values,
                  const std::vector<std::int32_t> *ruby) {
  const auto flags = static_cast<std::uint32_t>(values[3]);
  Json command = object({
      {"op", text("text")},
      {"text", text(glyph_text(image, tables, static_cast<std::uint32_t>(values[1])))},
      {"position", pair_of(values, 4)},
      {"colour", text(colour(values[6]))},
      {"frames_per_character", integer(values[2])},
      {"size", pair_of(values, 7)},
      {"layer", integer(talk_front_layer + values[9])},
  });
  if (flags & 0x20) {
    put(command, "animation", text("fade")); // the draw flag 0x20, the page-end marker
  }
  if (flags & text_bit_no_skip) {
    put(command, "skippable", boolean(false));
  }
  if (ruby != nullptr) {
    std::string ruby_text = glyph_text(image, tables, static_cast<std::uint32_t>((*ruby)[1]));
    if (!blank_ruby(ruby_text)) {
      put(command, "ruby", text(std::move(ruby_text)));
    }
  }
  return command;
}

Json sprite_command(const GameTables &tables, const std::vector<std::int32_t> &values) {
  // A negative id is a sprite chosen at run time (0x0c6ec5).
  Json command = object({{"op", text("sprite")}});
  if (values[1] >= 0) {
    const Json found = sprite_json(tables, values[1]);
    for (const auto &[key, value] : found.object()) {
      put(command, key, value);
    }
  } else {
    put(command, "sprite", integer(values[1]));
  }
  put(command, "position", pair_of(values, 2));
  put(command, "layer", integer(values[4]));
  put(command, "colour", text(colour(values[5])));
  const int alpha = (values[5] >> 24) & 0xFF;
  if (alpha != 0 && alpha != 0xFF) {
    put(command, "alpha", real(python_round(alpha / 255.0, 4))); // Pepper's lesson window
  }
  return command;
}

Json command_json(const XbeImage &image, const GameTables &tables,
                  const std::vector<std::int32_t> &values,
                  const std::vector<std::int32_t> *ruby) {
  switch (values[0]) {
  case 1:
    return text_command(image, tables, values, ruby);
  case 2:
    return object({{"op", text("wait")}, {"frames", integer(values[3])}});
  case 3:
    return sprite_command(tables, values);
  case 4: {
    Json command = object({{"op", text("character")},
                           {"character", integer(values[1])},
                           {"pose", integer(values[2])},
                           {"position", pair_of(values, 3)},
                           {"layer", integer(values[5])},
                           {"colour", text(colour(values[6]))}});
    if (values[7] & 1) {
      put(command, "fade", text("in"));
    } else if (values[7] & 2) {
      put(command, "fade", text("out"));
    }
    if (values[7] & 4) {
      put(command, "mouth", boolean(false));
    }
    return command;
  }
  case 5:
    return object({{"op", text("hold")}, {"frames", integer(values[1])}});
  case 10:
    return object({{"op", text("wait_for_confirm")}});
  default:
    throw FormatError("Unknown event opcode " + std::to_string(values[0]));
  }
}

Json pose_json(const XbeImage &image, const GameTables &tables, const int pose_id) {
  const std::uint32_t address = talk_poses + static_cast<std::uint32_t>(pose_id) * 0x1C;
  const auto ids = image.ints(address, 5);
  const std::size_t at = image.offset(address + 20, 8);
  const auto offsets = image.data().subspan(at, 8);
  const auto part = [&](const int sprite_id, const int offset) {
    if (sprite_id < 0) {
      return null();
    }
    const Sprite &sprite = tables.sprites.at(static_cast<std::size_t>(sprite_id));
    Json entry = object({{"page", integer(static_cast<std::int64_t>(sprite.slot) - character_slot)},
                         {"rect", rect(sprite)}});
    if (offset >= 0) {
      put(entry, "offset",
          array({integer(offsets[static_cast<std::size_t>(offset)]),
                 integer(offsets[static_cast<std::size_t>(offset) + 1])}));
    }
    return entry;
  };
  Json mouth = array();
  for (int index = 0; index < 3; ++index) {
    push(mouth, part(ids[1 + static_cast<std::size_t>(index)], 2 * index));
  }
  return object({{"body", part(ids[0], -1)}, {"mouth", mouth}, {"blink", part(ids[4], 6)}});
}

} // namespace

Json sprite_json(const GameTables &tables, const int sprite_id) {
  const Sprite &sprite = tables.sprites.at(static_cast<std::size_t>(sprite_id));
  const auto found = tables.texture_for_slot(sprite.slot);
  Json image = null();
  if (found && !(character_slot <= sprite.slot && sprite.slot < character_slot + character_slots)) {
    image = text("textures/" + pack_name(*found->first->path) + "/" +
                 std::to_string(found->second) + ".png");
  }
  return object({{"sprite", integer(sprite_id)}, {"image", image}, {"rect", rect(sprite)}});
}

Json event_commands(const XbeImage &image, const GameTables &tables, std::uint32_t address) {
  std::vector<std::vector<std::int32_t>> raw;
  for (;; address += command_ints * 4) {
    auto values = image.ints(address, command_ints);
    if (values[0] == 13) {
      break;
    }
    raw.push_back(std::move(values));
  }
  Json out = array();
  for (std::size_t index = 0; index < raw.size();) {
    // 0x0c5b9d draws every other text record: the ruby, then the text.
    if (raw[index][0] == 1 && index + 1 < raw.size() && raw[index + 1][0] == 1) {
      push(out, command_json(image, tables, raw[index + 1], &raw[index]));
      index += 2;
    } else {
      push(out, command_json(image, tables, raw[index], nullptr));
      index += 1;
    }
  }
  return out;
}

Json talk_tables(const XbeImage &image, const GameTables &tables) {
  Json backgrounds = array();
  for (int index = 0; index < talk_background_count; ++index) {
    const auto ids = image.ints(talk_backgrounds + 8 * static_cast<std::uint32_t>(index), 2);
    Json back = sprite_json(tables, ids[0]);
    put(back, "position", array({integer(0), integer(0)}));
    put(back, "layer", integer(0));
    Json front = sprite_json(tables, ids[1]);
    put(front, "position", array({integer(0), real(talk_front_y)}));
    put(front, "layer", integer(talk_front_layer));
    push(backgrounds, object({{"back", back}, {"front", front}}));
  }
  Json characters = array();
  for (int index = 0; index < talk_character_count; ++index) {
    const auto ids = image.ints(talk_characters + 0x28 * static_cast<std::uint32_t>(index), 10);
    const Resource &resource = tables.resources.at(static_cast<std::size_t>(ids[0]));
    const std::string path = pack_name(*resource.path);
    Json images = array();
    for (std::uint32_t page = 0; page < resource.slot_count.value_or(0); ++page) {
      push(images, text("textures/" + path + "/" + std::to_string(page) + ".png"));
    }
    // A missing pose shows the first one (0x0c7a77).
    Json poses = array();
    for (std::size_t pose = 1; pose < ids.size(); ++pose) {
      push(poses, ids[pose] >= 0 ? pose_json(image, tables, ids[pose]) : null());
    }
    push(characters, object({{"id", integer(index)}, {"images", images}, {"poses", poses}}));
  }
  return object({{"backgrounds", backgrounds}, {"characters", characters}});
}

Json story_events(const XbeImage &image, const GameTables &tables) {
  const XbeSection &segment = image.section("EVENTSEG");
  std::vector<std::int32_t> scenes;
  for (int index = 0; index < event_count; ++index) {
    scenes.push_back(image.ints(event_table + static_cast<std::uint32_t>(index) * event_record, 1)[0]);
  }
  Json records = array();
  for (int index = 0; index < event_count; ++index) {
    const std::uint32_t base = event_table + static_cast<std::uint32_t>(index) * event_record;
    const auto ids = image.ints(base, 3);
    const auto raw = image.data().subspan(image.offset(base, event_record), event_record);
    const std::int16_t music = i16(raw, 0x1E);
    const double volume = f32(raw, 0x20);
    Json cast = array();
    for (std::size_t k = 0; k < 6 && i8(raw, 0x17 + k) >= 0; ++k) {
      push(cast, integer(i8(raw, 0x17 + k)));
    }
    const std::uint32_t script = event_script_address(image, ids[0], segment);
    // Events whose table index is in [first, last] must have played (0 = none).
    const int first = raw[0x0C];
    const int last = raw[0x0D];
    Json required = array();
    if (first != 0 && last != 0) {
      for (int k = std::min(first, last); k <= std::max(first, last); ++k) {
        push(required, integer(scenes.at(static_cast<std::size_t>(k))));
      }
    }
    Json condition = array();
    for (std::size_t k = 0x0E; k < 0x14; ++k) {
      push(condition, integer(raw[k]));
    }
    Json music_json = null();
    if (music == music_keep) {
      music_json = text("keep");
    } else if (music >= 0) {
      music_json = text(tables.music.at(static_cast<std::size_t>(music)));
    }
    char script_hex[16];
    std::snprintf(script_hex, sizeof(script_hex), "0x%x", script);
    push(records, object({
                      {"scene", integer(ids[0])},
                      {"from_scene", integer(ids[1])},
                      {"to_scene", integer(ids[2])},
                      {"enabled", boolean(raw[0x14] != 0)},
                      {"requires_seen", required},
                      // Checks 0x0d1861 also makes (a condition function, the
                      // day, the garage...); not decoded yet.
                      {"condition_bytes", condition},
                      {"background", integer(i8(raw, 0x16))},
                      {"cast", cast},
                      {"music", music_json},
                      {"music_volume", real(python_round(volume, 3))},
                      {"script", text(script_hex)},
                      {"commands", event_commands(image, tables, script)},
                  }));
  }
  return records;
}

} // namespace phantomcrash::extract
