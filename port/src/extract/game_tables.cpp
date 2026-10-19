#include "game_tables.hpp"

#include "sha256.hpp"
#include "text.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace phantomcrash::extract {
namespace {

constexpr std::uint32_t music_names = 0x254480;
constexpr std::uint32_t scene_music = 0x256230;
constexpr int scene_count = 159;
constexpr std::uint32_t movies = 0x259524;
constexpr int movie_count = 11;
constexpr std::uint32_t sprites = 0x276680;
constexpr int sprite_count = 0xEB3;
constexpr std::uint32_t resources = 0x285E40;
constexpr int resource_count = 1927;
constexpr std::uint32_t binlink_packs = 0x1C3E98;
constexpr std::uint32_t font_characters = 0x27F114;
constexpr std::uint32_t font_metrics = 0x1C3FB0;
constexpr int font_metrics_count = 0x184;
constexpr std::uint32_t music_credits = 0x1EFD38;
constexpr int music_credit_count = 0x51;
constexpr std::uint32_t volume_millibels = 0x410400;

std::string lower(std::string text) {
  std::ranges::transform(text, text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

std::vector<std::string> string_table(const XbeImage &image, std::uint32_t address,
                                      std::uint32_t stride = 4, int limit = 4096) {
  std::vector<std::string> names;
  for (int i = 0; i < limit; ++i) {
    auto name = image.string(image.u32(address + static_cast<std::uint32_t>(i) * stride));
    if (!name) {
      break;
    }
    names.push_back(std::move(*name));
  }
  return names;
}

std::vector<Resource> read_resources(const XbeImage &image) {
  std::vector<Resource> out;
  for (int index = 0; index < resource_count; ++index) {
    const std::uint32_t base = resources + static_cast<std::uint32_t>(index) * 24;
    const auto data = image.data();
    const std::size_t at = image.offset(base, 24);
    const std::uint32_t path_address = u32(data, at);
    Resource r;
    r.id = index;
    r.size = u32(data, at + 4);
    r.type = u32(data, at + 8);
    const std::uint32_t slots = u32(data, at + 12);
    const std::uint32_t pack = u32(data, at + 16);
    const std::uint32_t pack_offset = u32(data, at + 20);
    const auto path = image.string(path_address);
    if (r.type == resource_texture_pack && slots != 0) {
      const std::size_t s = image.offset(slots, 8);
      r.first_slot = u32(data, s);
      r.slot_count = u32(data, s + 4);
    }
    if (r.type == resource_model && slots != 0) {
      const std::size_t s = image.offset(slots, 12);
      r.first_mesh_slot = u32(data, s);
      r.mesh_count = u32(data, s + 4);
      const std::uint32_t pack_id = u32(data, s + 8);
      if (pack_id != 0xFFFFFFFF) {
        r.texture_resource = pack_id;
      }
    }
    if (path) {
      r.path = media_relative(*path);
    } else if (pack != 0xFFFFFFFF) {
      r.binlink_pack = pack;
      r.binlink_offset = pack_offset;
    }
    out.push_back(std::move(r));
  }
  return out;
}

std::vector<Sprite> read_sprites(const XbeImage &image) {
  std::vector<Sprite> out;
  for (int index = 0; index < sprite_count; ++index) {
    const std::uint32_t entry = sprites + static_cast<std::uint32_t>(index) * 8;
    const std::uint32_t definition = image.u32(entry);
    Sprite s;
    s.id = index;
    s.slot = image.u32(entry + 4);
    image.require(definition, 24);
    double *fields[]{&s.left, &s.top, &s.right, &s.bottom, &s.texture_width, &s.texture_height};
    for (std::uint32_t k = 0; k < 6; ++k) {
      *fields[k] = image.f32(definition + k * 4);
    }
    out.push_back(s);
  }
  return out;
}

// One pair (glyph 829, 0x83bd) is not valid Shift-JIS; U+FFFD keeps the later
// glyphs at their index.
std::vector<std::string> read_font_characters(const XbeImage &image) {
  const std::string raw = image.raw_string(image.u32(font_characters));
  std::vector<std::string> out;
  for (std::size_t i = 0; i + 1 < raw.size(); i += 2) {
    out.push_back(first_character(decode_cp932(raw.substr(i, 2), DecodeErrors::replace)));
  }
  return out;
}

std::vector<MusicCredit> read_music_credits(const XbeImage &image) {
  std::vector<MusicCredit> out;
  for (int index = 0; index < music_credit_count; ++index) {
    const std::uint32_t base = music_credits + static_cast<std::uint32_t>(index) * 0x1AC;
    const int track = image.i32(base);
    if (track < 0) {
      continue;
    }
    MusicCredit c;
    c.track = track;
    c.title = plain(image.raw_string(base + 4));
    c.artist = plain(image.raw_string(base + 0x69));
    image.require(base + 0x198, 20);
    c.jacket = image.i32(base + 0x198);
    c.jacket_small = image.i32(base + 0x19C);
    c.artist_profile = static_cast<std::uint32_t>(image.i32(base + 0x1A0));
    c.artist_name = image.i32(base + 0x1A4);
    c.artist_link = image.i32(base + 0x1A8);
    out.push_back(std::move(c));
  }
  return out;
}

} // namespace

Json Resource::to_json() const {
  return object({{"id", integer(id)},
                 {"path", path ? text(*path) : null()},
                 {"size", integer(size)},
                 {"type", integer(type)},
                 {"first_slot", optional_integer(first_slot)},
                 {"slot_count", optional_integer(slot_count)},
                 {"binlink_pack", optional_integer(binlink_pack)},
                 {"binlink_offset", optional_integer(binlink_offset)},
                 {"texture_resource", optional_integer(texture_resource)},
                 {"first_mesh_slot", optional_integer(first_mesh_slot)},
                 {"mesh_count", optional_integer(mesh_count)}});
}

Json Sprite::to_json() const {
  return object({{"id", integer(id)},
                 {"slot", integer(slot)},
                 {"left", real(left)},
                 {"top", real(top)},
                 {"right", real(right)},
                 {"bottom", real(bottom)},
                 {"texture_width", real(texture_width)},
                 {"texture_height", real(texture_height)}});
}

std::optional<std::pair<const Resource *, std::uint32_t>>
GameTables::texture_for_slot(const std::uint32_t slot) const {
  for (const auto &r : resources) {
    if (!r.path || !r.first_slot) {
      continue;
    }
    if (*r.first_slot <= slot && slot < *r.first_slot + *r.slot_count) {
      return std::pair{&r, slot - *r.first_slot};
    }
  }
  return std::nullopt;
}

std::string media_relative(const std::string &path) {
  std::vector<std::string> parts;
  std::string part;
  for (const char c : path) {
    if (c == '\\' || c == '/') {
      parts.push_back(part);
      part.clear();
    } else {
      part += c;
    }
  }
  parts.push_back(part);
  std::size_t start = 0;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (lower(parts[i]) == "media") {
      start = i + 1;
      break;
    }
  }
  std::string out;
  for (std::size_t i = start; i < parts.size(); ++i) {
    out += (i > start ? "/" : "") + parts[i];
  }
  return out;
}

std::string pack_name(const std::string &relative) {
  const auto dot = relative.rfind('.');
  return dot == std::string::npos ? relative : relative.substr(0, dot);
}

std::vector<const Resource *> sprite_binlink_packs(const GameTables &tables) {
  std::set<std::uint32_t> used;
  for (const auto &s : tables.sprites) {
    used.insert(s.slot);
  }
  std::vector<const Resource *> out;
  for (const auto &r : tables.resources) {
    if (!r.binlink_pack || !r.first_slot) {
      continue;
    }
    for (std::uint32_t i = 0; i < *r.slot_count; ++i) {
      if (used.contains(*r.first_slot + i)) {
        out.push_back(&r);
        break;
      }
    }
  }
  return out;
}

GameTables load_game_tables(ByteView xbe) {
  if (sha256_hex(xbe) != europe_xbe_sha256) {
    throw FormatError("Not the European default.xbe these table addresses describe");
  }
  const XbeImage image(xbe);
  GameTables t;
  image.require(scene_music, scene_count * 4);
  for (int i = 0; i < scene_count; ++i) {
    t.scene_music.push_back(image.i32(scene_music + static_cast<std::uint32_t>(i) * 4));
  }
  for (int i = 0; i < movie_count; ++i) {
    t.movies.push_back(
        media_relative(image.string(image.u32(movies + static_cast<std::uint32_t>(i) * 4))
                           .value_or("")));
  }
  for (const auto &path : string_table(image, binlink_packs, 8)) {
    if (lower(path).starts_with("d:\\")) {
      t.binlink_packs.push_back(media_relative(path));
    }
  }
  t.music = string_table(image, music_names);
  t.resources = read_resources(image);
  t.sprites = read_sprites(image);
  t.font_characters = read_font_characters(image);
  for (std::size_t i = 0; i < t.font_characters.size(); ++i) {
    t.first_glyph.try_emplace(t.font_characters[i], i);
  }
  for (int i = 0; i < font_metrics_count; ++i) {
    const std::uint32_t at = font_metrics + static_cast<std::uint32_t>(i) * 2;
    t.font_metrics.emplace_back(image.i8(at), image.u8(at + 1));
  }
  t.music_credits = read_music_credits(image);
  for (std::uint32_t i = 0; i < 256; ++i) {
    t.volume_millibels.push_back(image.i16(volume_millibels + i * 2));
  }
  return t;
}

} // namespace phantomcrash::extract
