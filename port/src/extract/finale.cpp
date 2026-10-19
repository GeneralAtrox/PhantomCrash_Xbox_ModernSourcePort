// The game's epilogue after the First Ranker's contest and the ending
// movie's subtitles (finale.py). Field names are researcher-assigned.
#include "events.hpp"
#include "robot_tables.hpp"
#include "text.hpp"

#include <map>

namespace phantomcrash::extract {
namespace {

constexpr std::uint32_t phase_table = 0x242F78;
constexpr std::uint32_t phase_size = 0x34;
constexpr int first_state = 8;
constexpr int last_state = 18;
constexpr std::uint32_t call_ins = 0x1B5B14;
constexpr int call_in_limit = 64;
constexpr std::uint32_t light_points_init = 0x11E660; // pushes x, y, z, last argument first
constexpr std::size_t light_points = 3;
constexpr int banners[]{3063, 3074};
constexpr std::uint32_t carrier_first_record = 24; // 0x06789f: route r is record 24 + r
constexpr std::uint32_t carrier_routes = 7;
constexpr std::uint32_t carrier_speeds = 0x21C74C; // of a segment a frame, per route
constexpr std::uint32_t subtitles = 0x1DB4A0;      // start, end (int16 frames), text pointer
constexpr std::uint32_t subtitle_size = 8;
constexpr int subtitle_limit = 64;
constexpr std::uint8_t push_imm32 = 0x68;

Json light_point_list(const XbeImage &image) {
  const ByteView code = image.data().subspan(image.offset(light_points_init, 96), 96);
  std::vector<double> pushed;
  for (std::size_t index = 0; pushed.size() < light_points * 3 && index + 4 < code.size();) {
    if (code[index] == push_imm32) {
      pushed.push_back(f32(code, index + 1));
      index += 5;
    } else {
      ++index;
    }
  }
  Json points = array();
  for (std::size_t point = 0; point < light_points; ++point) {
    const double z = pushed.at(point * 3);
    const double y = pushed.at(point * 3 + 1);
    const double x = pushed.at(point * 3 + 2);
    push(points, rounded_list({x, y, z}, 4));
  }
  return points;
}

// The camera carrier's routes: route r's Bezier segments at its speed.
Json carrier_route_list(const XbeImage &image) {
  Json routes = array();
  for (std::uint32_t route = 0; route < carrier_routes; ++route) {
    const std::uint32_t record = supply_routes + (carrier_first_record + route) * supply_route_size;
    const auto words = image.u32s(record, 2);
    push(routes, object({{"segments", bezier_segments(image, words[0], words[1])},
                         {"speed", rounded(image.f32(carrier_speeds + route * 4), 7)}}));
  }
  return routes;
}

Json call_in_frames(const XbeImage &image) {
  Json frames = array();
  for (int index = 0; index < call_in_limit; ++index) {
    const std::int32_t frame = image.i32(call_ins + static_cast<std::uint32_t>(index) * 4);
    if (frame < 0) {
      break;
    }
    push(frames, integer(frame));
  }
  return frames;
}

} // namespace

Json finale_tables(const XbeImage &image, const GameTables &tables) {
  Json scripts = array();
  std::map<std::uint32_t, std::size_t> by_address;
  Json phases = array();
  for (int state = first_state; state <= last_state; ++state) {
    const std::uint32_t row = phase_table + static_cast<std::uint32_t>(state - first_state) * phase_size;
    image.require(row, phase_size);
    Json lights = array();
    for (const std::uint32_t offset : {0x14u, 0x1Cu, 0x24u}) {
      push(lights, object({{"point", integer(image.i32(row + offset))},
                           {"on", boolean(image.u8(row + offset + 4) != 0)}}));
    }
    const std::uint32_t script_address = image.u32(row + 0x2C);
    Json script = null();
    if (script_address != 0) {
      auto [found, added] = by_address.try_emplace(script_address, scripts.array().size());
      if (added) {
        push(scripts, event_commands(image, tables, script_address));
      }
      script = integer(found->second);
    }
    push(phases, object({{"state", integer(state)},
                         {"wait_frames", real(image.f32(row))},
                         {"fade_in_frames", real(image.f32(row + 4))},
                         {"fade_out_frames", real(image.f32(row + 8))},
                         {"dissolve_frames", real(image.f32(row + 12))},
                         {"carrier_route", integer(image.i32(row + 0x10))},
                         {"lights", lights},
                         {"carrier_flag", boolean(image.u8(row + 0x29) != 0)},
                         {"script", script},
                         {"script_frame", real(image.f32(row + 0x30))}}));
  }
  Json banner_list = array();
  for (const int sprite : banners) {
    push(banner_list, sprite_json(tables, sprite));
  }
  return object({{"phases", phases},
                 {"scripts", scripts},
                 {"call_ins", call_in_frames(image)},
                 {"light_points", light_point_list(image)},
                 {"banners", banner_list},
                 {"carrier_routes", carrier_route_list(image)}});
}

Json ending_subtitles(const XbeImage &image) {
  Json entries = array();
  for (int index = 0; index < subtitle_limit; ++index) {
    const std::uint32_t at = subtitles + static_cast<std::uint32_t>(index) * subtitle_size;
    image.require(at, subtitle_size);
    const std::int16_t start = image.i16(at);
    const std::int16_t end = image.i16(at + 2);
    if (start < 0 || end < 0) {
      break;
    }
    push(entries, object({{"start", integer(start)},
                          {"end", integer(end)},
                          {"text", text(decode_cp932(image.raw_string(image.u32(at + 4)),
                                                     DecodeErrors::replace))}}));
  }
  return object({{"frame_rate", integer(60)}, {"subtitles", entries}});
}

} // namespace phantomcrash::extract
