// tables/*.json: resources, sprites, music, font, movies, story events, talk,
// town and garage tables from the executable (convert.write_tables).
#include "convert_steps.hpp"
#include "events.hpp"
#include "town_garage_tables.hpp"

#include <map>

namespace phantomcrash::extract {
namespace {

namespace fs = std::filesystem;

constexpr const char *font_image = "textures/scroll/ascii_font_sys_tex/0.png";
constexpr int font_cell = 18;
constexpr int font_columns = 28;

// Texture slot -> every pack texture that can fill it (usually one): the
// disc's packs first, then the binlink packs (binlink/<resource>).
Json slot_sources(const GameTables &tables) {
  Json sources = object();
  const auto add = [&](const std::uint32_t slot, const std::string &pack, const std::uint32_t index) {
    Json &list = member(sources, std::to_string(slot));
    if (list.is_null()) {
      list = array();
    }
    push(list, object({{"pack", text(pack)}, {"texture", integer(index)}}));
  };
  for (const auto &resource : tables.resources) {
    if (!resource.path || !resource.first_slot) {
      continue;
    }
    for (std::uint32_t index = 0; index < *resource.slot_count; ++index) {
      add(*resource.first_slot + index, pack_name(*resource.path), index);
    }
  }
  for (const Resource *resource : sprite_binlink_packs(tables)) {
    for (std::uint32_t index = 0; index < *resource->slot_count; ++index) {
      add(*resource->first_slot + index, "binlink/" + std::to_string(resource->id), index);
    }
  }
  return sources;
}

// A sprite as the port's data files give one: its image and texel rect.
std::optional<Json> sprite_source(const GameTables &tables, const Json &sources,
                                  const std::int32_t sprite_id) {
  if (sprite_id < 0 || static_cast<std::size_t>(sprite_id) >= tables.sprites.size()) {
    return std::nullopt;
  }
  const Sprite &sprite = tables.sprites[static_cast<std::size_t>(sprite_id)];
  const Json *list = sources.find(std::to_string(sprite.slot));
  if (list == nullptr) {
    return std::nullopt;
  }
  const Json &source = list->array().front();
  const std::string image = "textures/" + field(source, "pack").string() + "/" +
                            std::to_string(static_cast<std::int64_t>(field(source, "texture").number())) +
                            ".png";
  return object({{"image", text(image)},
                 {"rect", reals({sprite.left, sprite.top, sprite.right, sprite.bottom})}});
}

// A credit with its music file, jacket/artist sprites and biography.
Json credit_record(const XbeImage &image, const GameTables &tables, const Json &sources,
                   const MusicCredit &credit) {
  Json record = object({{"file", text("sound/" + tables.music[static_cast<std::size_t>(credit.track)])},
                        {"track", integer(credit.track)},
                        {"title", text(credit.title)},
                        {"artist", text(credit.artist)}});
  const std::pair<const char *, std::int32_t> sprites[]{{"jacket", credit.jacket},
                                                        {"jacket_small", credit.jacket_small},
                                                        {"artist_name", credit.artist_name},
                                                        {"artist_link", credit.artist_link}};
  for (const auto &[key, sprite_id] : sprites) {
    if (auto source = sprite_source(tables, sources, sprite_id)) {
      put(record, key, std::move(*source));
    }
  }
  if (credit.artist_profile != 0) {
    // Like conversation text, these scripts pair ruby with the main text.
    // The biographies show one static text block and then hold until closed.
    const Json script = event_commands(image, tables, credit.artist_profile);
    std::vector<const Json *> lines;
    for (const Json &command : script.array()) {
      if (field(command, "op").string() == "text") {
        lines.push_back(&command);
      }
    }
    if (lines.size() != 1 || field(*lines[0], "frames_per_character").number() != 0) {
      throw FormatError("Unsupported artist profile for " + credit.artist);
    }
    Json profile = object();
    for (const char *key : {"text", "position", "colour", "layer"}) {
      put(profile, key, field(*lines[0], key));
    }
    put(record, "artist_profile", std::move(profile));
  }
  return record;
}

void write_table(const fs::path &directory, const char *name, const Json &value,
                 const bool readable_text = false) {
  // readable_text keeps non-ASCII text as UTF-8 rather than escapes.
  write_json(directory / name, value, 1, !readable_text);
}

} // namespace

Json write_tables(const XbeImage &image, const GameTables &tables, const fs::path &output) {
  const fs::path directory = output / "tables";
  Json resources = array();
  for (const auto &resource : tables.resources) {
    push(resources, resource.to_json());
  }
  write_table(directory, "resources.json", resources);

  const Json sources = slot_sources(tables);
  Json sprites = array();
  for (const auto &sprite : tables.sprites) {
    push(sprites, sprite.to_json());
  }
  write_table(directory, "sprites.json", object({{"slots", sources}, {"sprites", sprites}}));

  Json credits = array();
  for (const auto &credit : tables.music_credits) {
    if (static_cast<std::size_t>(credit.track) < tables.music.size()) {
      push(credits, credit_record(image, tables, sources, credit));
    }
  }
  write_table(directory, "music.json",
              object({{"tracks", texts(tables.music)},
                      {"scene_music", integers(tables.scene_music)},
                      {"credits", credits},
                      {"volume_millibels", integers(tables.volume_millibels)}}));

  std::string characters;
  for (const auto &character : tables.font_characters) {
    characters += character;
  }
  Json metrics = array();
  for (const auto &[left, width] : tables.font_metrics) {
    push(metrics, array({integer(left), integer(width)}));
  }
  write_table(directory, "font.json",
              object({{"image", text(font_image)},
                      {"cell", integer(font_cell)},
                      {"columns", integer(font_columns)},
                      {"characters", text(characters)},
                      {"metrics", metrics}}));
  write_table(directory, "movies.json",
              object({{"movies", texts(tables.movies)},
                      {"binlink_packs", texts(tables.binlink_packs)}}));

  const Json story = story_events(image, tables);
  write_table(directory, "events.json", object({{"events", story}}), true);
  write_table(directory, "talk.json", talk_tables(image, tables));
  write_table(directory, "town.json", town_tables(image, tables), true);
  write_table(directory, "garage.json", garage_tables(image), true);
  return object({{"events", integer(story.array().size())},
                 {"resources", integer(tables.resources.size())},
                 {"sprites", integer(tables.sprites.size())},
                 {"music", integer(tables.music.size())},
                 {"movies", integer(tables.movies.size())},
                 {"font_glyphs", integer(tables.font_characters.size())}});
}

} // namespace phantomcrash::extract
