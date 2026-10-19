// The finale's data: the converter's tables/finale.json and the tuning in
// data/story/finale.json (docs/FINAL_BATTLE.md).
#include "phantomcrash/finale.hpp"

#include "phantomcrash/json_fields.hpp"

#include <stdexcept>

namespace phantomcrash {
namespace {

using json::JsonValue;
using json::required;

float number(const JsonValue &value, const char *key, const float fallback) {
  return static_cast<float>(value.number_or(key, fallback));
}

FinalePhase parse_phase(const JsonValue &value) {
  FinalePhase phase;
  phase.state = static_cast<int>(required(value, "state").number());
  phase.wait_frames = number(value, "wait_frames", 0.0F);
  phase.fade_in_frames = number(value, "fade_in_frames", 0.0F);
  phase.fade_out_frames = number(value, "fade_out_frames", 0.0F);
  phase.dissolve_frames = number(value, "dissolve_frames", 0.0F);
  phase.carrier_route = static_cast<int>(value.number_or("carrier_route", -1));
  if (const JsonValue *lights = value.find("lights")) {
    const auto &array = lights->array();
    for (std::size_t index = 0; index < phase.lights.size() && index < array.size(); ++index) {
      phase.lights[index].point = static_cast<int>(array[index].number_or("point", -1));
      phase.lights[index].on = array[index].boolean_or("on", false);
    }
  }
  phase.carrier_flag = value.boolean_or("carrier_flag", false);
  if (const JsonValue *script = value.find("script"); script != nullptr && script->is_number()) {
    phase.script = static_cast<int>(script->number());
  }
  phase.script_frame = number(value, "script_frame", -1.0F);
  return phase;
}

} // namespace

FinaleTables parse_finale_tables(const JsonValue &document) {
  try {
    FinaleTables tables;
    for (const JsonValue &phase : required(document, "phases").array()) {
      tables.phases.push_back(parse_phase(phase));
    }
    for (const JsonValue &script : required(document, "scripts").array()) {
      tables.scripts.push_back(script);
    }
    for (const FinalePhase &phase : tables.phases) {
      if (phase.script >= static_cast<int>(tables.scripts.size())) {
        throw std::runtime_error("a phase names a script that is not there");
      }
    }
    if (const JsonValue *call_ins = document.find("call_ins")) {
      for (const JsonValue &frame : call_ins->array()) {
        tables.call_ins.push_back(static_cast<int>(frame.number()));
      }
    }
    if (const JsonValue *points = document.find("light_points")) {
      for (const JsonValue &point : points->array()) {
        const auto xyz = json::numbers<3>(point, "light_points");
        tables.light_points.push_back({xyz[0], xyz[1], xyz[2]});
      }
    }
    if (const JsonValue *banners = document.find("banners")) {
      tables.banners = parse_sprites(*banners);
    }
    if (const JsonValue *routes = document.find("carrier_routes")) {
      for (const JsonValue &value : routes->array()) {
        FinaleCarrierRoute route;
        route.speed = number(value, "speed", 0.0F);
        for (const JsonValue &segment : required(value, "segments").array()) {
          const auto &points = segment.array();
          if (points.size() != 4) {
            throw std::runtime_error("carrier_routes: a segment has four points");
          }
          auto &out = route.segments.emplace_back();
          for (std::size_t k = 0; k < 4; ++k) {
            const auto xyz = json::numbers<3>(points[k], "carrier_routes point");
            out[k] = {xyz[0], xyz[1], xyz[2]};
          }
        }
        tables.carrier_routes.push_back(std::move(route));
      }
    }
    return tables;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("tables/finale.json: ") + error.what());
  }
}

FinaleTuning parse_finale_tuning(const JsonValue &document) {
  try {
    FinaleTuning tuning;
    tuning.stage = document.string_or("stage", tuning.stage);
    tuning.next = document.string_or("next", tuning.next);
    tuning.frame_rate = document.number_or("frame_rate", tuning.frame_rate);
    if (tuning.frame_rate <= 0.0) {
      throw std::runtime_error("frame_rate must be above 0");
    }
    tuning.start_fade_frames = number(document, "start_fade_frames", tuning.start_fade_frames);
    tuning.robot_heading = number(document, "robot_heading", tuning.robot_heading);
    if (const JsonValue *start = document.find("start_sound")) {
      tuning.start_sound_state = static_cast<int>(start->number_or("state", 8));
      tuning.start_sound_frame = number(*start, "frame", tuning.start_sound_frame);
      tuning.start_sound = start->string_or("sound", tuning.start_sound);
    }
    if (const JsonValue *music = document.find("music")) {
      tuning.music_state = static_cast<int>(music->number_or("state", tuning.music_state));
      tuning.music = music->string_or("track", tuning.music);
    }
    tuning.script_sound = document.string_or("script_sound", tuning.script_sound);
    tuning.call_in_sound = document.string_or("call_in_sound", tuning.call_in_sound);
    if (const JsonValue *banner = document.find("banner")) {
      tuning.banner_frames = number(*banner, "frames", tuning.banner_frames);
      if (const JsonValue *position = banner->find("position")) {
        tuning.banner_position = json::numbers<2>(*position, "banner.position");
      }
      tuning.banner_sound = banner->string_or("sound", tuning.banner_sound);
    }
    tuning.stand_frames = number(document, "stand_frames", tuning.stand_frames);
    if (const JsonValue *carrier = document.find("carrier")) {
      const auto groups = [](const JsonValue &list) {
        std::vector<int> out;
        for (const JsonValue &group : list.array()) {
          out.push_back(static_cast<int>(group.number()));
        }
        return out;
      };
      if (const JsonValue *effects = carrier->find("effects")) {
        tuning.carrier_effects = groups(*effects);
      }
      if (const JsonValue *lights = carrier->find("light_effects")) {
        const auto &list = lights->array();
        for (std::size_t index = 0; index < list.size() && index < 3; ++index) {
          tuning.light_effects[index] = groups(list[index]);
        }
      }
      const auto vector = [&](const char *name, const Vec3 fallback) {
        const JsonValue *value = carrier->find(name);
        if (value == nullptr) {
          return fallback;
        }
        const auto &xyz = value->array();
        if (xyz.size() != 3) {
          throw std::runtime_error(std::string("carrier.") + name + " needs three numbers");
        }
        return Vec3{static_cast<float>(xyz[0].number()), static_cast<float>(xyz[1].number()),
                    static_cast<float>(xyz[2].number())};
      };
      tuning.emitter_offset = vector("emitter_offset", tuning.emitter_offset);
      tuning.emitter_direction = vector("emitter_direction", tuning.emitter_direction);
    }
    if (const JsonValue *end = document.find("end")) {
      tuning.end_music = end->string_or("music", tuning.end_music);
      tuning.end_frames = number(*end, "frames", tuning.end_frames);
      if (const JsonValue *keys = end->find("tv_off")) {
        for (const JsonValue &key : keys->array()) {
          const auto scale = json::numbers<2>(required(key, "scale"), "end.tv_off scale");
          tuning.tv_off.push_back(
              {number(key, "frame", 0.0F), scale[0], scale[1], number(key, "alpha", 1.0F)});
        }
      }
    }
    return tuning;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("story/finale.json: ") + error.what());
  }
}

} // namespace phantomcrash
