#include "text.hpp"
#include "town_garage_tables.hpp"

#include <array>

namespace phantomcrash::extract {
namespace {

constexpr std::uint32_t robots = 0x19F920;
constexpr std::uint32_t robot_size = 0x3C;
constexpr int robot_count = 0x4A6;
// Side 0 of each weapon pair is the one the game calls RIGHT.
constexpr std::array robot_parts{"body",           "legs",          "right_arm", "left_arm",
                                 "right_shoulder", "left_shoulder", "chip"};
constexpr std::size_t robot_modules = 10;
constexpr std::uint32_t robot_name = 0x38;
constexpr std::uint32_t lineups = 0x24FC18;
constexpr int lineup_count = 0x41;
constexpr std::size_t lineup_size = 3;
constexpr std::uint32_t modules = 0x1C1F90;
constexpr std::uint32_t module_size = 0x2C;
constexpr int module_count = 107;
constexpr std::uint32_t paints = 0x1C31F8;
constexpr int paint_count = 57;
constexpr std::uint32_t emblems = 0x1C34A8;
constexpr int emblem_count = 206;
constexpr std::uint32_t entry_size = 12;
constexpr std::uint32_t shop_lineups = 0x250230;
constexpr std::uint32_t shop_lineup_size = 0x48;
constexpr std::size_t shop_lineup_items = 9;
constexpr int chip_lineup_count = 24;
constexpr std::uint32_t module_lineups = shop_lineups + chip_lineup_count * shop_lineup_size;
constexpr int module_lineup_count = 135;
constexpr std::uint32_t used_sv_lineups = 0x252EE8;
constexpr std::uint32_t used_sv_lineup_size = 0x30;
constexpr std::size_t used_sv_lineup_items = 6;
constexpr int used_sv_lineup_count = 24;

std::uint32_t at(const std::uint32_t base, const int index, const std::uint32_t size) {
  return base + static_cast<std::uint32_t>(index) * size;
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

std::vector<std::int16_t> file_shorts(const XbeImage &image, const std::uint32_t address,
                                      const std::size_t count) {
  image.require(address, 2 * count);
  std::vector<std::int16_t> out;
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back(image.i16(address + static_cast<std::uint32_t>(2 * i)));
  }
  return out;
}

Json name_text(const XbeImage &image, const std::uint32_t pointer) {
  return pointer != 0 ? text(plain(image.raw_string(pointer))) : null();
}

Json robot_json(const XbeImage &image, const int index) {
  const std::uint32_t address = at(robots, index, robot_size);
  const auto shorts = file_shorts(image, address, robot_name / 2);
  Json parts = object();
  for (std::size_t slot = 0; slot < robot_parts.size(); ++slot) {
    const std::int16_t part = shorts[1 + slot * 2];
    const auto level = static_cast<std::int8_t>(shorts[2 + slot * 2] & 0xFF);
    put(parts, robot_parts[slot],
        part >= 0 ? object({{"id", integer(part)}, {"level", integer(level)}}) : null());
  }
  Json module_list = array();
  for (std::size_t k = 17; k < 17 + robot_modules; ++k) {
    if (shorts[k] >= 0) {
      push(module_list, integer(shorts[k]));
    }
  }
  return object({{"name", name_text(image, image.u32(address + robot_name))},
                 {"flags", integer(shorts[0])},
                 {"parts", parts},
                 {"paint", integer(shorts[15])},
                 {"emblem", integer(shorts[16])},
                 {"modules", module_list},
                 {"short_36", integer(shorts[27])}});
}

Json lineup_json(const XbeImage &image, const int index) {
  const auto words =
      file_ints(image, at(lineups, index, lineup_size * 8), lineup_size * 2);
  Json out = array();
  for (std::size_t k = 0; k < lineup_size; ++k) {
    push(out, object({{"robot", integer(words[k * 2])}, {"picture", integer(words[k * 2 + 1])}}));
  }
  return out;
}

Json shop_lineup(const XbeImage &image, const std::uint32_t table, const int index) {
  const auto words = file_ints(image, at(table, index, shop_lineup_size), shop_lineup_items * 2);
  Json out = array();
  for (std::size_t k = 0; k < shop_lineup_items; ++k) {
    if (words[k * 2] >= 0) {
      push(out, object({{"id", integer(words[k * 2])}, {"level", integer(words[k * 2 + 1])}}));
    }
  }
  return out;
}

Json used_sv_lineup(const XbeImage &image, const int index) {
  const auto words = file_ints(image, at(used_sv_lineups, index, used_sv_lineup_size),
                               used_sv_lineup_items * 2);
  Json out = array();
  for (std::size_t k = 0; k < used_sv_lineup_items; ++k) {
    if (words[k * 2] >= 0) {
      push(out, integer(words[k * 2]));
    }
  }
  return out;
}

Json module_json(const XbeImage &image, const int index) {
  const std::uint32_t address = at(modules, index, module_size);
  const auto words = file_ints(image, address, module_size / 4);
  const auto rounded = [&](const std::uint32_t offset) {
    return real(python_round(image.f32(address + offset), 5));
  };
  const auto shorts = file_shorts(image, address + 0x22, 2);
  return object({{"name", name_text(image, static_cast<std::uint32_t>(words[0]))},
                 {"weight", rounded(0xC)},
                 {"robot_4538", rounded(0x14)},
                 {"robot_4540", rounded(0x18)},
                 {"laser_reduction", integer(image.u8(address + 0x20))},
                 {"active_frames", integer(shorts[0])},
                 {"charge_frames", integer(shorts[1])},
                 {"price", integer(words[10])},
                 {"armour", integer(words[2])},
                 {"sockets", integer(words[4] & 0xFF)},
                 {"word_04", integer(words[1])},
                 {"word_1c", integer(words[7])}});
}

Json entries(const XbeImage &image, const std::uint32_t table, const int count) {
  Json out = array();
  for (int index = 0; index < count; ++index) {
    const auto words = file_ints(image, at(table, index, entry_size), 3);
    push(out, object({{"name", name_text(image, static_cast<std::uint32_t>(words[0]))},
                      {"index", integer(words[1])},
                      {"price", integer(words[2])}}));
  }
  return out;
}

template <typename Read> Json listed(const int count, Read read) {
  Json out = array();
  for (int index = 0; index < count; ++index) {
    push(out, read(index));
  }
  return out;
}

} // namespace

Json garage_tables(const XbeImage &image) {
  return object({
      {"robots", listed(robot_count, [&](int i) { return robot_json(image, i); })},
      {"dealer_lineups", listed(lineup_count, [&](int i) { return lineup_json(image, i); })},
      {"modules", listed(module_count, [&](int i) { return module_json(image, i); })},
      {"paints", entries(image, paints, paint_count)},
      {"emblems", entries(image, emblems, emblem_count)},
      {"chip_lineups",
       listed(chip_lineup_count, [&](int i) { return shop_lineup(image, shop_lineups, i); })},
      {"module_lineups",
       listed(module_lineup_count, [&](int i) { return shop_lineup(image, module_lineups, i); })},
      {"used_sv_lineups",
       listed(used_sv_lineup_count, [&](int i) { return used_sv_lineup(image, i); })},
  });
}

} // namespace phantomcrash::extract
