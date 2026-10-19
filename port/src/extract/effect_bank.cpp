// The particle effects (effect_bank.py): groups, scripts and emitter
// definitions, and the atlas cells they draw. Field names are
// researcher-assigned from the code that reads them.
#include "robot_assets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>

namespace phantomcrash::extract {
namespace {

constexpr std::uint32_t groups = 0x205C10;
constexpr int group_count = 79;
constexpr std::uint32_t scripts = 0x215928;
constexpr int script_count = 62;
constexpr std::uint32_t definition_size = 0x150;
constexpr std::uint32_t end_scripts = 0xFFFFFFFF; // -1 ends a group's script list
constexpr std::uint32_t end_flashes = 0xFFFFFFFE; // -2 ends its flash list
constexpr std::uint32_t end_flag = 0x200000;      // script flag: the last pair
constexpr std::uint32_t atlas_cells = 0x11D060;   // constructor filling 0x9660e0
constexpr std::size_t atlas_cell_count = 22;
constexpr double atlas_cell_width = 0.125;
constexpr double atlas_cell_height = 0.25;
constexpr std::uint32_t atlas_resource = 1589; // texture slot 400, the particles
constexpr int effect_rate = 30;                // frames a second in a battle (0x255ab0)
constexpr double hue_degrees = 360.0;
constexpr double percent = 100.0;
constexpr std::size_t curve_limit = 64;
constexpr int decimals = 5;
constexpr std::size_t cell_offset = 0x114; // short: the first atlas cell, then frames

constexpr std::pair<std::uint32_t, const char *> script_flags[]{
    {0x2, "spawn"},          {0x4, "replaceable"},     {0x8, "inherit_velocity"},
    {0x10, "exact_spread"},  {0x20, "radial_push"},    {0x40, "random_direction"},
    {0x80, "spin"},          {0x100, "streak"},        {0x200, "streak_long"},
    {0x400, "streak_short"}, {0x800, "random_offset"}, {0x1000, "fragments"},
    {0x2000, "detached"},    {0x4000, "random_speed"}, {0x8000, "swirl"},
    {0x10000, "die_at_target"}, {0x20000, "debris"},   {0x40000, "debris_kind"},
    {0x80000, "kept"},       {0x100000, "streak_line"}};

struct Field {
  std::size_t offset;
  const char *name;
  char kind; // 'f' float, 'i' int, 'b' byte
};
constexpr Field fields[]{
    {0x00, "force_scale", 'f'},  {0x04, "force_time", 'i'},    {0x08, "force_jitter", 'f'},
    {0x0C, "drag", 'f'},         {0x10, "drag_time", 'i'},     {0x14, "drag_jitter", 'f'},
    {0x18, "additive", 'b'},     {0x1C, "no_gravity", 'b'},    {0x2C, "gravity", 'f'},
    {0x30, "wind_off", 'b'},     {0x34, "wind", 'f'},          {0x44, "pull_off", 'b'},
    {0x48, "pull", 'f'},         {0x4C, "pull_to_target", 'b'}, {0x50, "pull_range", 'f'},
    {0x6C, "colour_time", 'i'},  {0x70, "alpha_time", 'i'},    {0x74, "life", 'f'},
    {0x78, "life_jitter", 'f'},  {0x7C, "mass", 'f'},          {0x80, "mass_time", 'i'},
    {0x84, "mass_jitter", 'f'},  {0x88, "size", 'f'},          {0x8C, "scale_time", 'i'},
    {0x90, "size_jitter", 'f'},  {0x94, "min_speed", 'f'},     {0x98, "min_speed_time", 'i'},
    {0x9C, "max_speed", 'f'},    {0xA0, "max_speed_time", 'i'}, {0xB0, "alpha", 'f'},
    {0xC0, "alpha_extra", 'f'},  {0xC4, "shape_type", 'i'},    {0xC8, "shape_variant", 'i'},
    {0xCC, "direction_mode", 'i'}, {0xF4, "rate", 'f'},        {0xF8, "rate_jitter", 'f'},
    {0xFC, "spread", 'f'},       {0x100, "spread_jitter", 'f'}, {0x104, "speed", 'f'},
    {0x108, "speed_jitter", 'f'}, {0x10C, "speed_time", 'i'},  {0x110, "push", 'f'}};

constexpr std::pair<std::size_t, const char *> vectors[]{
    {0x20, "gravity_angles"}, {0x38, "wind_angles"}, {0x54, "pull_angles"},
    {0x60, "target"},         {0xA4, "colour"},      {0xB4, "colour_jitter_hsv"},
    {0xD0, "offset"},         {0xDC, "rotation"},    {0xE8, "scale"}};

struct Curve {
  std::size_t offset;
  const char *name;
  bool bytes;
};
constexpr Curve curves[]{{0x11C, "blue", true},       {0x120, "green", true},
                         {0x124, "red", true},        {0x128, "alpha", true},
                         {0x12C, "scale", false},     {0x130, "pull", false},
                         {0x134, "wind", false},      {0x138, "rate", false},
                         {0x13C, "mass", false},      {0x140, "speed", false},
                         {0x144, "min_speed", false}, {0x148, "max_speed", false}};

Json flag_names(std::uint32_t value) {
  Json names = array();
  for (int bit = 0; bit < 32; ++bit) {
    const std::uint32_t mask = 1u << bit;
    if ((value & mask) == 0 || mask == end_flag) {
      continue;
    }
    const auto known = std::find_if(std::begin(script_flags), std::end(script_flags),
                                    [&](const auto &entry) { return entry.first == mask; });
    push(names, text(known != std::end(script_flags) ? known->second : "bit_" + std::to_string(bit)));
  }
  return names;
}

Json curve(const XbeImage &image, std::uint32_t address, bool bytes) {
  Json keys = array();
  for (std::size_t index = 0; index < curve_limit; ++index) {
    const std::uint32_t at = address + static_cast<std::uint32_t>(index) * 8;
    image.require(at, 8);
    const double time = image.f32(at);
    if (time < 0.0) {
      break;
    }
    push(keys, array({rounded(time, decimals),
                      bytes ? integer(image.u8(at + 4)) : rounded(image.f32(at + 4), decimals)}));
  }
  return keys;
}

double clamp01(double value) {
  return std::min(std::max(value, 0.0), 1.0);
}

// The red of an HSV colour as 0x0647fe works it out: the definitions'
// colour jitter is a brightness this large either way.
double hue_red(double hue, double saturation, double value) {
  const double h = clamp01(hue / hue_degrees);
  const double s = clamp01(saturation / percent);
  const double v = clamp01(value / percent);
  const double wrapped = h > 0.5 ? h - 1.0 : h;
  const double red = clamp01(2.0 - std::abs(wrapped) * 6.0);
  return ((1.0 - red) * (1.0 - s) + red) * v;
}

std::string hex6(std::uint32_t value) {
  char buffer[16];
  std::snprintf(buffer, sizeof buffer, "0x%06x", value);
  return buffer;
}

Json definition(const XbeImage &image, std::uint32_t address) {
  const ByteView raw = image.data().subspan(image.offset(address, definition_size), definition_size);
  Json out = object({{"address", text(hex6(address))}});
  for (const Field &f : fields) {
    if (f.kind == 'f') {
      put(out, f.name, rounded(f32(raw, f.offset), decimals));
    } else if (f.kind == 'i') {
      put(out, f.name, integer(i32(raw, f.offset)));
    } else {
      put(out, f.name, boolean(raw[f.offset] != 0));
    }
  }
  for (const auto &[offset, name] : vectors) {
    put(out, name, rounded_list({f32(raw, offset), f32(raw, offset + 4), f32(raw, offset + 8)}, decimals));
  }
  const double jitter = hue_red(f32(raw, 0xB4) * hue_degrees, f32(raw, 0xB8) * percent,
                                f32(raw, 0xBC) * percent);
  put(out, "colour_jitter", rounded(jitter, decimals));
  const std::int32_t shape = i32(raw, 0xC4);
  const char *shape_name = shape == 0 || shape == 1 ? "point"
                           : shape == 3             ? "sphere"
                           : shape == 5             ? "cluster"
                                                    : nullptr;
  put(out, "shape", text(shape_name != nullptr ? shape_name : "shape_" + std::to_string(shape)));
  put(out, "cell", integer(i16(raw, cell_offset)));
  put(out, "frames", integer(i16(raw, cell_offset + 2)));
  Json curve_map = object();
  for (const Curve &c : curves) {
    if (const std::uint32_t pointer = u32(raw, c.offset); pointer != 0) {
      put(curve_map, c.name, curve(image, pointer, c.bytes));
    }
  }
  put(out, "curves", std::move(curve_map));
  return out;
}

Json id_list(const XbeImage &image, std::uint32_t address, std::uint32_t end) {
  Json ids = array();
  for (std::uint32_t value; (value = image.u32(address)) != end; address += 4) {
    push(ids, integer(value));
  }
  return ids;
}

// Walks the constructor: push y, push x, mov ecx, cell, call.
Json atlas_cell_list(const XbeImage &image) {
  const ByteView data = image.data();
  std::size_t at = image.offset(atlas_cells, 1) + 3; // after push ebp; mov ebp, esp
  Json cells = array();
  for (std::size_t count = 0; count < atlas_cell_count; ++count) {
    std::vector<double> pushed;
    while (pushed.size() < 2) {
      if (data[at] == 0x6A) {
        pushed.push_back(static_cast<double>(data[at + 1]));
        at += 2;
      } else if (data[at] == 0x68) {
        pushed.push_back(f32(data, at + 1));
        at += 5;
      } else {
        throw FormatError("unexpected atlas constructor code");
      }
    }
    if (data[at] != 0xB9 || data[at + 5] != 0xE8) {
      throw FormatError("unexpected atlas constructor code");
    }
    at += 10;
    push(cells, rounded_list({pushed[1], pushed[0]}, decimals));
  }
  return cells;
}

} // namespace

Json effect_bank(const XbeImage &image, const GameTables &tables, EffectResources &resources) {
  std::map<std::uint32_t, std::size_t> definitions;
  std::vector<std::uint32_t> by_index;
  Json script_list = array();
  for (int script = 0; script < script_count; ++script) {
    std::uint32_t address = image.u32(scripts + static_cast<std::uint32_t>(script) * 4);
    Json emitters = array();
    for (;;) {
      image.require(address, 8);
      const std::uint32_t flags = image.u32(address);
      const std::uint32_t found = image.u32(address + 4);
      if (found != 0) {
        auto [entry, added] = definitions.try_emplace(found, by_index.size());
        if (added) {
          by_index.push_back(found);
        }
        push(emitters, object({{"flags", flag_names(flags)}, {"definition", integer(entry->second)}}));
      }
      if ((flags & end_flag) != 0) {
        break;
      }
      address += 8;
    }
    push(script_list, object({{"id", integer(script)}, {"emitters", emitters}}));
  }
  Json group_list = array();
  for (int group = 0; group < group_count; ++group) {
    const auto words = image.u32s(groups + static_cast<std::uint32_t>(group) * 8, 2);
    push(group_list, object({{"id", integer(group)},
                             {"scripts", id_list(image, words[0], end_scripts)},
                             {"flashes", id_list(image, words[1], end_flashes)}}));
  }
  Json pictures = effect_pictures(image, tables, resources);
  Json definition_list = array();
  for (const std::uint32_t address : by_index) {
    push(definition_list, definition(image, address));
  }
  Json bank = object(
      {{"frame_rate", integer(effect_rate)},
       {"particle_motion", object({{"fragment_copies", integer(8)},
                                   {"fragment_step", real(0.1)},
                                   {"fragment_jitter", real(0.1)},
                                   {"debris_restitution", real(0.0)},
                                   {"debris_friction", real(0.0)},
                                   {"debris_rest_speed", real(0.02)},
                                   {"debris_clearance", real(0.05)},
                                   {"debris_stage_contacts", boolean(false)},
                                   {"start_at_offset", boolean(true)}})},
       {"atlas", object({{"texture", text("textures/binlink/" + std::to_string(atlas_resource) + ".png")},
                         {"cell_size", reals({atlas_cell_width, atlas_cell_height})},
                         {"cells", atlas_cell_list(image)}})},
       {"groups", group_list},
       {"scripts", script_list},
       {"definitions", definition_list}});
  for (auto &[key, value] : pictures.object()) {
    put(bank, key, value);
  }
  return bank;
}

} // namespace phantomcrash::extract
