#include "phantomcrash/settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

namespace phantomcrash {
namespace {

// Screen offsets beyond the original's ranges (x -32..30, y -16..14) are
// kept, but not without limit.
constexpr int offset_limit = 64;

int index_of(const std::array<std::string_view, control_action_count> &names,
             const std::string_view name) {
  const auto found = std::ranges::find(names, name);
  return found == names.end() ? -1 : static_cast<int>(found - names.begin());
}

int clamped(const json::JsonValue &value, const char *key, const int fallback, const int low,
            const int high) {
  const double number = value.number_or(key, fallback);
  return std::clamp(static_cast<int>(std::lround(number)), low, high);
}

ControlOptions controls_from_json(const json::JsonValue &value) {
  ControlOptions controls;
  controls.type = clamped(value, "type", controls.type, 0, 1);
  controls.view = clamped(value, "view", controls.view, 0, 1);
  controls.beginner = clamped(value, "beginner", controls.beginner, 0, 1);
  if (const auto *bindings = value.find("bindings")) {
    std::array<int, control_action_count> read = controls.bindings;
    for (std::size_t action = 0; action < control_action_count; ++action) {
      if (const auto *name = bindings->find(control_actions[action])) {
        read[action] = bindable_button_index(name->string());
      }
    }
    auto sorted = read;
    std::ranges::sort(sorted);
    bool permutation = true;
    for (std::size_t index = 0; index < sorted.size(); ++index) {
      permutation = permutation && sorted[index] == static_cast<int>(index);
    }
    if (permutation) {
      controls.bindings = read;
    }
  }
  return controls;
}

json::JsonValue controls_to_json(const ControlOptions &controls) {
  using json::JsonValue;
  JsonValue::Object bindings;
  for (std::size_t action = 0; action < control_action_count; ++action) {
    const auto button = static_cast<std::size_t>(controls.bindings[action]);
    bindings.emplace_back(std::string(control_actions[action]),
                          JsonValue{std::string(bindable_button_names[button])});
  }
  return JsonValue{JsonValue::Object{
      {"type", JsonValue{static_cast<std::int64_t>(controls.type)}},
      {"view", JsonValue{static_cast<std::int64_t>(controls.view)}},
      {"beginner", JsonValue{static_cast<std::int64_t>(controls.beginner)}},
      {"bindings", JsonValue{std::move(bindings)}},
  }};
}

GameOptions game_from_json(const json::JsonValue &value) {
  GameOptions game;
  game.vibration = value.boolean_or("vibration", game.vibration);
  game.bgm_volume = clamped(value, "bgm_volume", game.bgm_volume, 0, 255);
  game.se_volume = clamped(value, "se_volume", game.se_volume, 0, 255);
  if (const auto *offset = value.find("screen_offset"); offset != nullptr && offset->is_array()) {
    const auto &items = offset->array();
    for (std::size_t axis = 0; axis < 2 && axis < items.size(); ++axis) {
      game.screen_offset[axis] = std::clamp(static_cast<int>(std::lround(items[axis].number())),
                                            -offset_limit, offset_limit);
    }
  }
  if (const auto *controls = value.find("controls")) {
    game.controls = controls_from_json(*controls);
  }
  return game;
}

json::JsonValue game_to_json(const GameOptions &game) {
  using json::JsonValue;
  return JsonValue{JsonValue::Object{
      {"vibration", JsonValue{game.vibration}},
      {"bgm_volume", JsonValue{static_cast<std::int64_t>(game.bgm_volume)}},
      {"se_volume", JsonValue{static_cast<std::int64_t>(game.se_volume)}},
      {"screen_offset",
       JsonValue{JsonValue::Array{JsonValue{static_cast<std::int64_t>(game.screen_offset[0])},
                                  JsonValue{static_cast<std::int64_t>(game.screen_offset[1])}}}},
      {"controls", controls_to_json(game.controls)},
  }};
}

} // namespace

int control_action_index(const std::string_view name) {
  return index_of(control_actions, name);
}

int bindable_button_index(const std::string_view name) {
  return index_of(bindable_button_names, name);
}

bool *game_option(GameOptions &options, const std::string_view name) {
  if (name == "vibration") {
    return &options.vibration;
  }
  return nullptr;
}

int *game_value(GameOptions &options, const std::string_view name) {
  if (name == "bgm_volume") {
    return &options.bgm_volume;
  }
  if (name == "se_volume") {
    return &options.se_volume;
  }
  if (name == "control_type") {
    return &options.controls.type;
  }
  if (name == "view_change") {
    return &options.controls.view;
  }
  if (name == "beginner") {
    return &options.controls.beginner;
  }
  return nullptr;
}

const int *game_value(const GameOptions &options, const std::string_view name) {
  return game_value(const_cast<GameOptions &>(options), name);
}

Settings settings_from_json(const json::JsonValue &value) {
  Settings settings;
  if (const auto *frame_rate = value.find("frame_rate")) {
    auto &target = settings.frame_rate;
    target.vsync = frame_rate->boolean_or("vsync", target.vsync);
    target.cap_enabled = frame_rate->boolean_or("cap_enabled", target.cap_enabled);
    const double cap = frame_rate->number_or("cap", target.cap);
    target.cap =
        std::clamp(static_cast<int>(std::lround(cap)), frame_cap_minimum, frame_cap_maximum);
  }
  if (const auto *post = value.find("post_process")) {
    const PostProcessOptions defaults;
    settings.post_process.depth_blur = post->boolean_or("depth_blur", defaults.depth_blur);
    settings.post_process.distortion = post->boolean_or("distortion", defaults.distortion);
    settings.post_process.colour_tint = post->boolean_or("colour_tint", defaults.colour_tint);
    settings.post_process.smaa = post->boolean_or("smaa", false);
    settings.post_process.dynamic_lighting = post->boolean_or("dynamic_lighting", false);
    settings.post_process.volumetric_light =
        post->boolean_or("volumetric_light", defaults.volumetric_light);
    settings.post_process.original_softness = post->boolean_or("original_softness", false);
    settings.post_process.sharp_text = post->boolean_or("sharp_text", defaults.sharp_text);
    settings.post_process.softness_strength = std::clamp(
        static_cast<float>(post->number_or("softness_strength", defaults.softness_strength)), 0.0F,
        1.0F);
  }
  if (const auto *audio = value.find("audio")) {
    settings.audio.master = clamped(*audio, "master", 100, 0, 100);
    for (std::size_t i = 0; i < audio_bus_count; ++i) {
      settings.audio.levels[i] =
          clamped(*audio, std::string(audio_bus_names[i]).c_str(), 100, 0, 100);
    }
  }
  settings.fullscreen = value.boolean_or("fullscreen", settings.fullscreen);
  settings.frame_counter = value.boolean_or("frame_counter", settings.frame_counter);
  settings.pause_unfocused = value.boolean_or("pause_unfocused", settings.pause_unfocused);
  settings.skip_conversations = value.boolean_or("skip_conversations", settings.skip_conversations);
  settings.experimental_coop = value.boolean_or("experimental_coop", settings.experimental_coop);
  settings.slai_enabled = value.boolean_or("slai_enabled", false);
  if (const auto *coop = value.find("coop")) {
    settings.coop.enemy_level_offset = clamped(*coop, "enemy_level_offset", 0, -99, 99);
    settings.coop.kill_ny_percent = clamped(*coop, "kill_ny_percent", 100, 0, 1000);
    settings.coop.share_shootdowns =
        coop->boolean_or("share_shootdowns", settings.coop.share_shootdowns);
    settings.coop.share_cash = coop->boolean_or("share_cash", settings.coop.share_cash);
    settings.coop.share_boss_clears =
        coop->boolean_or("share_boss_clears", settings.coop.share_boss_clears);
  }
  if (const auto *profiles = value.find("coop_profiles"); profiles && profiles->is_array()) {
    for (std::size_t i = 0; i < settings.coop_profiles.size() && i < profiles->array().size();
         ++i) {
      if (profiles->array()[i].is_object()) {
        settings.coop_profiles[i] = quest_from_json(profiles->array()[i]);
      }
    }
  }
  if (const auto *profile = value.find("online_profile"); profile && profile->is_object()) {
    settings.online_profile = quest_from_json(*profile);
  }
  if (const auto *controls = value.find("host_controls")) {
    settings.host_controls = host_controls_from_json(*controls);
  }
  if (const auto *delay = value.find("online_input_delay_ticks"); delay && delay->is_number()) {
    settings.online_input_delay_ticks = std::max(0, static_cast<int>(std::lround(delay->number())));
  }
  settings.rumble_robots = std::clamp(
      static_cast<int>(std::lround(value.number_or("rumble_robots", settings.rumble_robots))), 0,
      max_rumble_robots);
  if (const auto *cheats = value.find("cheats")) {
    settings.cheats.infinite_money = cheats->boolean_or("infinite_money", false);
    settings.cheats.never_die = cheats->boolean_or("never_die", false);
    settings.cheats.infinite_ammo = cheats->boolean_or("infinite_ammo", false);
    settings.cheats.open_contests = cheats->boolean_or("open_contests", false);
  }
  if (const auto *game = value.find("game")) {
    settings.game = game_from_json(*game);
  }
  if (const auto *quest = value.find("quest")) {
    settings.quest = quest_from_json(*quest);
  }
  if (const auto *slots = value.find("save_slots"); slots != nullptr && slots->is_array()) {
    const auto &saved = slots->array();
    for (std::size_t slot = 0; slot < settings.save_slots.size() && slot < saved.size(); ++slot) {
      if (saved[slot].is_object()) {
        settings.save_slots[slot] = quest_from_json(saved[slot]);
      } else if (!saved[slot].is_null()) {
        settings.save_slots_unreadable = true;
      }
    }
  } else if (slots != nullptr) {
    settings.save_slots_unreadable = true;
  }
  if (const auto *kept = value.find("vs_scoobees")) {
    settings.vs_scoobees = vs_scoobees_from_json(*kept);
  }
  return settings;
}

json::JsonValue settings_to_json(const Settings &settings) {
  json::JsonValue::Array profiles;
  for (const auto &profile : settings.coop_profiles) {
    profiles.push_back(profile ? quest_to_json(*profile) : json::JsonValue{});
  }
  using json::JsonValue;
  JsonValue::Object frame_rate{
      {"vsync", JsonValue{settings.frame_rate.vsync}},
      {"cap_enabled", JsonValue{settings.frame_rate.cap_enabled}},
      {"cap", JsonValue{static_cast<std::int64_t>(settings.frame_rate.cap)}},
  };
  JsonValue::Object audio{{"master", JsonValue{static_cast<std::int64_t>(settings.audio.master)}}};
  for (std::size_t i = 0; i < audio_bus_count; ++i) {
    audio.emplace_back(std::string(audio_bus_names[i]),
                       JsonValue{static_cast<std::int64_t>(settings.audio.levels[i])});
  }
  JsonValue::Array slots;
  for (const auto &slot : settings.save_slots) {
    slots.push_back(slot ? quest_to_json(*slot) : JsonValue{});
  }
  return JsonValue{JsonValue::Object{
      {"frame_rate", JsonValue{std::move(frame_rate)}},
      {"audio", JsonValue{std::move(audio)}},
      {"post_process",
       JsonValue{JsonValue::Object{
           {"depth_blur", JsonValue{settings.post_process.depth_blur}},
           {"distortion", JsonValue{settings.post_process.distortion}},
           {"colour_tint", JsonValue{settings.post_process.colour_tint}},
           {"smaa", JsonValue{settings.post_process.smaa}},
           {"dynamic_lighting", JsonValue{settings.post_process.dynamic_lighting}},
           {"volumetric_light", JsonValue{settings.post_process.volumetric_light}},
           {"original_softness", JsonValue{settings.post_process.original_softness}},
           {"sharp_text", JsonValue{settings.post_process.sharp_text}},
           {"softness_strength",
            JsonValue{static_cast<double>(settings.post_process.softness_strength)}}}}},
      {"fullscreen", JsonValue{settings.fullscreen}},
      {"frame_counter", JsonValue{settings.frame_counter}},
      {"pause_unfocused", JsonValue{settings.pause_unfocused}},
      {"skip_conversations", JsonValue{settings.skip_conversations}},
      {"experimental_coop", JsonValue{settings.experimental_coop}},
      {"slai_enabled", JsonValue{settings.slai_enabled}},
      {"coop",
       JsonValue{JsonValue::Object{
           {"enemy_level_offset",
            JsonValue{static_cast<std::int64_t>(settings.coop.enemy_level_offset)}},
           {"kill_ny_percent", JsonValue{static_cast<std::int64_t>(settings.coop.kill_ny_percent)}},
           {"share_shootdowns", JsonValue{settings.coop.share_shootdowns}},
           {"share_cash", JsonValue{settings.coop.share_cash}},
           {"share_boss_clears", JsonValue{settings.coop.share_boss_clears}}}}},
      {"coop_profiles", JsonValue{std::move(profiles)}},
      {"online_profile",
       settings.online_profile ? quest_to_json(*settings.online_profile) : JsonValue{}},
      {"host_controls", host_controls_to_json(settings.host_controls)},
      {"rumble_robots", JsonValue{static_cast<std::int64_t>(settings.rumble_robots)}},
      {"online_input_delay_ticks",
       settings.online_input_delay_ticks
           ? JsonValue{static_cast<std::int64_t>(*settings.online_input_delay_ticks)}
           : JsonValue{}},
      {"cheats",
       JsonValue{JsonValue::Object{{"infinite_money", JsonValue{settings.cheats.infinite_money}},
                                   {"never_die", JsonValue{settings.cheats.never_die}},
                                   {"infinite_ammo", JsonValue{settings.cheats.infinite_ammo}},
                                   {"open_contests", JsonValue{settings.cheats.open_contests}}}}},
      {"game", game_to_json(settings.game)},
      {"quest", quest_to_json(settings.quest)},
      {"save_slots", JsonValue{std::move(slots)}},
      {"vs_scoobees", vs_scoobees_to_json(settings.vs_scoobees)},
  }};
}

Settings load_settings(const std::filesystem::path &path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    return {};
  }
  try {
    return settings_from_json(json::read_file(path));
  } catch (const std::exception &) {
    Settings unread;
    unread.save_slots_unreadable = true;
    return unread;
  }
}

bool save_settings(const std::filesystem::path &path, const Settings &settings) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      return false;
    }
    stream << json::write(settings_to_json(settings));
    if (!stream) {
      return false;
    }
  }
  std::filesystem::rename(temporary, path, error);
  return !error;
}

} // namespace phantomcrash
