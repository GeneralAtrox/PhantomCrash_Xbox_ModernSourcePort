// Lesson messages and enemy paths (battle_data.py), stage environments
// (environment.py) and the battle chatter (chatter.py).
#include "events.hpp"
#include "robot_tables.hpp"

#include <map>
#include <optional>

namespace phantomcrash::extract {
namespace {

// {script *first, *second}[state]: the Preferred and Standard control types.
constexpr std::uint32_t lesson_tables[]{0x243168, 0x243220};
constexpr std::uint32_t lesson_first_state = 0x18;
constexpr std::uint32_t lesson_last_state = 0x2E;
constexpr std::size_t enemy_path_table = 0x0C; // u32 segment offsets, up to the first

constexpr std::uint32_t presets = 0x2014C8;        // preset list pointer per stage
constexpr std::uint32_t sun_directions = 0x2015F8; // f32[3] per stage
constexpr int stage_count = 8;
constexpr std::uint32_t preset_size = 0x54;
constexpr int preset_limit = 32;
constexpr std::size_t preset_bytes = 0x54; // struct "<2I3f2I2f4i6iIf"
constexpr const char *fog_modes[]{"none", "linear", "exp", "exp2"}; // D3DFOG_*
constexpr int environment_decimals = 4;

constexpr std::uint32_t chatter_lines = 0x1B3520;
constexpr int chatter_table_count = 67;
constexpr std::uint32_t chatter_events = 0x23;
constexpr std::uint32_t chatter_gates = 0x203848;
// The Area Rankers' defeat lines, by area (0x089861 reads them by the
// contest's area, save +0x6104 / 2, and 0x091f29 starts them).
constexpr std::uint32_t ranker_defeat_lines = 0x2431B4;
constexpr int ranker_defeat_count = 3;
constexpr std::pair<const char *, std::uint32_t> chatter_chances[]{
    {"player", 0x203890}, {"team_two", 0x203920}, {"computer", 0x2039B0}};

Json rgb(const std::uint32_t value) {
  return array({integer((value >> 16) & 0xFF), integer((value >> 8) & 0xFF), integer(value & 0xFF)});
}

std::optional<Json> preset(const XbeImage &image, const std::uint32_t address) {
  if (!image.mapped(address, preset_size)) {
    return std::nullopt;
  }
  const ByteView raw = image.data().subspan(image.offset(address, preset_size), preset_bytes);
  const auto name = image.string(u32(raw, 0));
  const std::uint32_t mode = u32(raw, 0x14);
  if (!name || mode >= std::size(fog_modes)) {
    return std::nullopt;
  }
  const auto round4 = [&](std::size_t at) { return rounded(f32(raw, at), environment_decimals); };
  const auto ints = [&](std::size_t at, std::size_t count) {
    Json out = array();
    for (std::size_t k = 0; k < count; ++k) {
      push(out, integer(i32(raw, at + 4 * k)));
    }
    return out;
  };
  return object({{"name", text(*name)},
                 {"fog", object({{"mode", text(fog_modes[mode])},
                                 {"colour", rgb(u32(raw, 4))},
                                 {"density", round4(8)},
                                 {"start", round4(12)},
                                 {"end", round4(16)}})},
                 {"light", object({{"colour", rgb(u32(raw, 0x18))},
                                   {"diffuse", round4(0x1C)},
                                   {"ambient", round4(0x20)}})},
                 {"stencil_argb", ints(0x24, 4)},
                 {"levels", object({{"min", ints(0x34, 3)}, {"max", ints(0x40, 3)}})},
                 {"flag", integer(u32(raw, 0x4C))},
                 {"value_0x50", round4(0x50)}});
}

} // namespace

Json lesson_messages(const XbeImage &image, const GameTables &tables) {
  Json states = object();
  const char *names[]{"first", "second"};
  for (std::uint32_t state = lesson_first_state; state <= lesson_last_state; ++state) {
    const auto usual = image.u32s(lesson_tables[0] + state * 8, 2);
    const auto other = image.u32s(lesson_tables[1] + state * 8, 2);
    Json entry = object();
    for (std::size_t k = 0; k < 2; ++k) {
      if (usual[k] != 0) {
        put(entry, names[k], event_commands(image, tables, usual[k]));
      }
    }
    Json standard = object();
    for (std::size_t k = 0; k < 2; ++k) {
      if (other[k] != 0 && other[k] != usual[k]) {
        put(standard, names[k], event_commands(image, tables, other[k]));
      }
    }
    if (!standard.object().empty()) {
      put(entry, "standard", std::move(standard));
    }
    put(states, std::to_string(state), std::move(entry));
  }
  return object({{"states", states}});
}

Json enemy_path(ByteView data) {
  if (data.size() < 4 || std::string_view(reinterpret_cast<const char *>(data.data()), 4) != "ENPT") {
    throw FormatError("not an enemy path (no ENPT magic)");
  }
  const std::size_t first = u32(data, enemy_path_table);
  const std::size_t count = (first - enemy_path_table) / 4;
  std::vector<std::uint32_t> offsets;
  std::map<std::uint32_t, std::size_t> index_of;
  for (std::size_t k = 0; k < count; ++k) {
    offsets.push_back(u32(data, enemy_path_table + 4 * k));
    index_of[offsets.back()] = k; // a repeated offset keeps its last index, as a dict does
  }
  Json segments = array();
  for (const std::uint32_t offset : offsets) {
    const std::uint8_t kind = u8(data, offset + 2);
    const std::uint8_t links = u8(data, offset + 3);
    const std::size_t points_offset = u32(data, offset + 8);
    Json next = array();
    for (std::size_t k = 0; k < links; ++k) {
      push(next, integer(index_of.at(u32(data, offset + 12 + 4 * k))));
    }
    const std::uint32_t point_count = u32(data, points_offset);
    Json points = array();
    for (std::uint32_t i = 0; i < point_count; ++i) {
      const std::size_t at = points_offset + 4 + 12 * std::size_t{i};
      push(points, rounded_list({f32(data, at), f32(data, at + 4), f32(data, at + 8)}, 4));
    }
    push(segments, object({{"kind", integer(kind)}, {"points", points}, {"next", next}}));
  }
  return object({{"segments", segments}});
}

Json stage_environments(const XbeImage &image) {
  Json stages = array();
  for (int stage = 0; stage < stage_count; ++stage) {
    const auto index = static_cast<std::uint32_t>(stage);
    const auto direction = image.f32s(sun_directions + 12 * index, 3);
    const std::uint32_t first = image.u32(presets + 4 * index);
    Json list = array();
    for (int k = 0; k < preset_limit; ++k) {
      auto found = preset(image, first + static_cast<std::uint32_t>(k) * preset_size);
      if (!found) {
        break;
      }
      push(list, std::move(*found));
    }
    push(stages, object({{"stage", integer(stage)},
                         {"sun_direction", rounded_list(direction, environment_decimals)},
                         {"presets", list}}));
  }
  return stages;
}

Json chatter_tables(const XbeImage &image, const GameTables &tables) {
  Json chances = object();
  for (const auto &[kind, address] : chatter_chances) {
    put(chances, kind, rounded_list(image.f32s(address, chatter_events), 4));
  }
  Json scripts = array();
  std::map<std::uint32_t, std::size_t> by_address;
  std::map<std::string, std::size_t> by_content;
  const auto script_at = [&](const std::uint32_t address) {
    if (!by_address.contains(address)) {
      Json commands = event_commands(image, tables, address);
      // Equal scripts at different addresses are written once.
      const std::string key = dumps(commands, {-1, false, false});
      auto [found, added] = by_content.try_emplace(key, scripts.array().size());
      if (added) {
        push(scripts, std::move(commands));
      }
      by_address[address] = found->second;
    }
    return integer(by_address[address]);
  };
  Json line_tables = array();
  for (int table = 0; table < chatter_table_count; ++table) {
    Json entries = array();
    for (std::uint32_t event = 0; event < chatter_events; ++event) {
      const std::uint32_t address =
          image.u32(chatter_lines + (static_cast<std::uint32_t>(table) * chatter_events + event) * 4);
      push(entries, address == 0 ? null() : script_at(address));
    }
    push(line_tables, std::move(entries));
  }
  Json defeats = array();
  for (const std::uint32_t address : image.u32s(ranker_defeat_lines, ranker_defeat_count)) {
    push(defeats, address == 0 ? null() : script_at(address));
  }
  return object({{"gates", integers(image.i16s(chatter_gates, chatter_events))},
                 {"chances", chances},
                 {"scripts", scripts},
                 {"tables", line_tables},
                 {"ranker_defeats", defeats}});
}

} // namespace phantomcrash::extract
