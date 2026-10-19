#include "desktop_scene_dump.hpp"
#include "desktop_scene_dump_text.hpp"

#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

// The dump's name: the local date and time, which sort in order.
std::string stamp() {
  SDL_Time now{};
  SDL_DateTime at{};
  if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &at, true)) {
    return "scene";
  }
  char buffer[32];
  SDL_snprintf(buffer, sizeof buffer, "%04d%02d%02d-%02d%02d%02d", at.year, at.month, at.day,
               at.hour, at.minute, at.second);
  return buffer;
}

void write_text(const std::filesystem::path &path, const std::string &text) {
  std::ofstream file(path, std::ios::binary);
  file << text;
  if (!file) {
    throw std::runtime_error("unable to write " + path.string());
  }
}

std::string quoted(const std::filesystem::path &path) {
  return "\"" + path.string() + "\"";
}

// The executable running now, for the steps back to the scene.
std::filesystem::path executable() {
  const char *base = SDL_GetBasePath();
  const std::filesystem::path folder =
      base != nullptr ? std::filesystem::path(reinterpret_cast<const char8_t *>(base))
                      : std::filesystem::path{};
  return folder / "phantomcrash_port.exe";
}

} // namespace

bool dump_scene(SDL_Renderer *renderer, const ScreenRunner &screens, Settings live,
                const std::filesystem::path &preferences) {
  try {
    const std::filesystem::path folder = preferences.parent_path() / "scene_dumps";
    std::filesystem::create_directories(folder);
    const std::string name = stamp();
    const auto picture = folder / (name + ".bmp");
    const auto settings = folder / (name + ".settings.json");
    const auto description = folder / (name + ".json");

    SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
    const bool saved = surface != nullptr && SDL_SaveBMP(surface, picture.string().c_str());
    SDL_DestroySurface(surface);
    if (!saved) {
      SDL_Log("Scene dump: no screenshot (%s)", SDL_GetError());
    }

    // The running game as it stands, so --settings starts from this save;
    // the co-op profiles as last checkpointed on disk.
    live.quest = screens.quest_save();
    live.game = screens.game_options();
    live.vs_scoobees = screens.vs_scoobees();
    live.save_slots = screens.save_slots();
    live.coop_profiles = load_settings(preferences).coop_profiles;
    live.fullscreen = false;
    write_text(settings, json::write(settings_to_json(live)));

    json::JsonValue dump =
        screens.scene_dump(quoted(executable()) + " --settings " + quoted(settings));
    auto &fields = std::get<json::JsonValue::Object>(dump.value);
    fields.push_back(
        {"capture",
         {json::JsonValue::Object{{"screenshot", {saved ? picture.string() : std::string{}}},
                                  {"settings", {settings.string()}},
                                  {"dump", {description.string()}}}}});
    const std::string text = json::write(dump);
    write_text(description, text);
    const bool copied = SDL_SetClipboardText(scene_dump_clipboard(text).c_str());
    SDL_Log("Scene dump: %s (%s)", description.string().c_str(),
            copied ? "copied to the clipboard" : SDL_GetError());
    return copied;
  } catch (const std::exception &error) {
    SDL_Log("Scene dump failed: %s", error.what());
  }
  return false;
}

void save_frame(SDL_Renderer *renderer, const std::filesystem::path &path) {
  SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
  const bool saved = surface != nullptr && SDL_SaveBMP(surface, path.string().c_str());
  SDL_DestroySurface(surface);
  if (!saved) {
    throw std::runtime_error("Unable to save " + path.string() + ": " + SDL_GetError());
  }
}

} // namespace phantomcrash
