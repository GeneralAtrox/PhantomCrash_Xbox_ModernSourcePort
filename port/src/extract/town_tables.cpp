#include "events.hpp"
#include "text.hpp"
#include "town_garage_tables.hpp"

namespace phantomcrash::extract {
namespace {

constexpr std::uint32_t classes = 0x24D108;
constexpr std::uint32_t class_size = 0x2C;
constexpr int class_count = 28;
constexpr std::uint32_t contests = 0x24B388;
constexpr std::uint32_t contest_size = 0x6C;
constexpr int contest_count = 56;
constexpr std::uint32_t calendar = 0x249FA8;
constexpr std::uint32_t day_size = 0x3C;
constexpr int days = 60;
constexpr std::size_t venue_word = 3; // +0xc: the first venue's contest
constexpr std::size_t venue_count = 3;
constexpr std::size_t module_categories = 7; // Wild Machines' module lineups, +0x1c
constexpr std::size_t contest_teams = 8;
constexpr std::uint32_t entrants = 0x1B1008;
constexpr std::uint32_t entrant_size = 8;
constexpr int entrant_count = 0x4A3;
constexpr std::uint32_t teams = 0x24D5D8;
constexpr std::uint32_t team_size = 0x24;
constexpr int team_count = 0x110;
constexpr std::size_t team_members = 8;
constexpr std::uint32_t crews = 0x1C9920;
constexpr std::uint32_t crew_size = 0x1C;
constexpr int crew_count = 0x424;
constexpr std::uint32_t pilots = 0x1C5AF8;
constexpr std::uint32_t pilot_size = 0x10;
constexpr int pilot_count = 0x3DE;
constexpr std::uint32_t pilot_sprite = 8;

std::uint32_t at(const std::uint32_t base, const int index, const std::uint32_t size) {
  return base + static_cast<std::uint32_t>(index) * size;
}

std::string table_text(const XbeImage &image, const std::int32_t address) {
  if (address == 0) {
    return "";
  }
  const std::string raw = image.raw_string(static_cast<std::uint32_t>(address));
  return strip(nfkc(decode_cp932(raw, DecodeErrors::replace)));
}

std::vector<std::int32_t> file_ints(const XbeImage &image, const std::uint32_t address,
                                    const std::size_t count) {
  image.require(address, 4 * count);
  std::vector<std::int32_t> out;
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back(image.i32(address + static_cast<std::uint32_t>(4 * i)));
  }
  return out;
}

Json rounded_floats(const XbeImage &image, const std::uint32_t address, const std::size_t count) {
  image.require(address, 4 * count);
  Json out = array();
  for (std::size_t i = 0; i < count; ++i) {
    push(out, real(python_round(image.f32(address + static_cast<std::uint32_t>(4 * i)), 4)));
  }
  return out;
}

Json slice(const std::vector<std::int32_t> &words, const std::size_t from, const std::size_t to) {
  return integers(std::vector<std::int32_t>(words.begin() + static_cast<std::ptrdiff_t>(from),
                                            words.begin() + static_cast<std::ptrdiff_t>(to)));
}

Json class_json(const XbeImage &image, const int index) {
  const std::uint32_t address = at(classes, index, class_size);
  const auto words = file_ints(image, address, 11);
  Json legs = array();
  for (std::size_t k = 6; k < 10; ++k) {
    if (words[k] >= 0 && words[k] < 8) {
      push(legs, integer(words[k]));
    }
  }
  return object({{"name", text(table_text(image, words[0]))},
                 {"regulation", text(table_text(image, words[1]))},
                 {"fee", integer(words[2])},
                 {"weight", rounded_floats(image, address + 0xC, 2)},
                 {"maker", integer(words[5])},
                 {"legs", legs},
                 {"other", integer(words[10])}});
}

// The lookups clamp a bad index to the first record.
int record(const int index, const int count) {
  return index >= 0 && index < count ? index : 0;
}

Json portraits(const XbeImage &image, const GameTables &tables, const int entrant) {
  Json out = array();
  if (entrant == -1) {
    return out;
  }
  const int crew = image.i16(at(entrants, record(entrant, entrant_count), entrant_size));
  const std::uint32_t crew_address = at(crews, record(crew, crew_count), crew_size);
  for (const std::uint32_t member : {crew_address, crew_address + 2}) {
    const int pilot = image.i16(member);
    const int sprite = image.i16(at(pilots, record(pilot, pilot_count), pilot_size) + pilot_sprite);
    if (sprite >= 0) {
      push(out, sprite_json(tables, sprite));
    }
  }
  return out;
}

Json contest_json(const XbeImage &image, const GameTables &tables, const int index) {
  const std::uint32_t address = at(contests, index, contest_size);
  const auto words = file_ints(image, address, 17);
  Json team_list = array();
  for (std::size_t k = 8; k < 8 + contest_teams; ++k) {
    if (words[k] >= 0) {
      push(team_list, integer(words[k]));
    }
  }
  return object({{"name", text(table_text(image, words[0]))},
                 {"class", integer(words[1])},
                 {"night", boolean(words[2] == 1)},
                 {"levels", slice(words, 3, 5)},
                 {"reserve_after", integer(words[5])},
                 {"roster_size", integer(words[6])},
                 {"extra_team", integer(words[7])},
                 {"teams", team_list},
                 {"champion", integer(words[16])},
                 {"champion_portraits", portraits(image, tables, words[16])},
                 {"floats_44", rounded_floats(image, address + 0x44, 10)}});
}

Json day_json(const XbeImage &image, const int day) {
  const auto words = file_ints(image, at(calendar, day, day_size), day_size / 4);
  return object({{"venues", slice(words, venue_word, venue_word + venue_count)},
                 {"dealers", slice(words, 0, venue_word)},
                 {"chips", integer(words[6])},
                 {"modules", slice(words, 7, 7 + module_categories)},
                 {"used_svs", integer(words[14])}});
}

} // namespace

Json town_tables(const XbeImage &image, const GameTables &tables) {
  Json class_list = array();
  for (int index = 0; index < class_count; ++index) {
    push(class_list, class_json(image, index));
  }
  Json contest_list = array();
  for (int index = 0; index < contest_count; ++index) {
    push(contest_list, contest_json(image, tables, index));
  }
  Json day_list = array();
  for (int day = 0; day < days; ++day) {
    push(day_list, day_json(image, day));
  }
  Json team_list = array();
  for (int index = 0; index < team_count; ++index) {
    const auto words = file_ints(image, at(teams, index, team_size), 1 + team_members);
    push(team_list, object({{"leader", integer(words[0])}, {"members", slice(words, 1, words.size())}}));
  }
  Json entrant_list = array();
  for (int index = 0; index < entrant_count; ++index) {
    const std::uint32_t address = at(entrants, index, entrant_size);
    image.require(address, entrant_size);
    push(entrant_list, object({{"pilot", integer(image.i16(address))},
                               {"robot", integer(image.i16(address + 2))},
                               {"ai_profile", integer(image.i16(address + 4))},
                               {"level", integer(image.i16(address + 6))}}));
  }
  return object({{"classes", class_list},
                 {"contests", contest_list},
                 {"calendar", day_list},
                 {"teams", team_list},
                 {"entrants", entrant_list}});
}

} // namespace phantomcrash::extract
