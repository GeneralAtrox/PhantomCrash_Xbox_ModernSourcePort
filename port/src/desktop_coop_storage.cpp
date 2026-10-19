#include "desktop_coop_storage.hpp"
#include "phantomcrash/online_lobby.hpp"
#include "phantomcrash/online_content.hpp"
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace phantomcrash {
namespace {
std::filesystem::path executable() {
#ifdef _WIN32
  std::wstring path(32768, L'\0');
  const auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!count || count == path.size()) {
    throw std::runtime_error("Cannot identify the online executable");
  }
  path.resize(count);
  return path;
#else
  return std::filesystem::canonical("/proc/self/exe");
#endif
}
} // namespace

std::shared_ptr<CoopState> make_coop_state(const Options &options, const Settings &saved,
                                           const std::filesystem::path &preferences,
                                           const std::filesystem::path &root, const bool smoke) {
  auto coop = std::make_shared<CoopState>();
  coop->enabled = saved.experimental_coop || options.online.has_value();
  coop->options = saved.coop;
  if (saved.online_input_delay_ticks) {
    coop->input_delay = static_cast<std::uint32_t>(*saved.online_input_delay_ticks);
  }
  coop->profiles = saved.coop_profiles;
  coop->saved_profiles = saved.coop_profiles;
  coop->online_profile = saved.online_profile;
  coop->online_mode = options.online.has_value();
  coop->online_host = options.online && options.online->host;
  coop->fingerprint = [root, slai = saved.slai_enabled] {
    return online::content_fingerprint(executable(), root, slai);
  };
  if (options.online) {
    coop->profiles.fill(std::nullopt);
    coop->saved_profiles.fill(std::nullopt);
    online::RoomOptions room;
    room.host = options.online->host;
    room.bind = online::Endpoint::parse("0.0.0.0", room.host ? options.online->port : 0);
    if (!room.host) {
      room.server = online::Endpoint::parse(options.online->address, options.online->port);
    }
    SDL_Log("Online: checking executable, data, all asset roots, mods and model buffers");
    room.compatibility = coop->fingerprint();
    coop->open_online = [room] {
      auto lobby = std::make_shared<online::Lobby>(room);
      lobby->start_transport_worker();
      return lobby;
    };
    SDL_Log("Online: %s on UDP port %u", room.host ? "hosting" : "joining", options.online->port);
  }
  coop->save_profile = [preferences, smoke, owner = std::weak_ptr(coop)](const int slot,
                                                                         const QuestSave &profile) {
    const auto state = owner.lock();
    if (!state) {
      return false;
    }
    const bool online = state->online_mode;
    if (smoke) {
      return online; // scripted online checks exercise saves in memory only
    }
    if (slot < 0 || slot >= 4) {
      return false;
    }
    auto disk = load_settings(preferences);
    if (disk.save_slots_unreadable) {
      SDL_Log("Co-op save failed: player=%d settings unreadable", slot + 1);
      return false;
    }
    if (online) {
      disk.online_profile = profile;
    } else {
      disk.coop_profiles[slot] = profile;
    }
    const bool written = save_settings(preferences, disk);
    SDL_Log("%s save %s: player=%d day=%d", online ? "Online" : "Co-op",
            written ? "written" : "failed", slot + 1, profile.day + 1);
    return written;
  };
  return coop;
}

void copy_coop_profiles(const CoopState &coop, Settings &settings) {
  settings.online_profile = coop.online_profile;
  if (!coop.online_mode) {
    settings.coop_profiles = coop.profiles;
  }
}
} // namespace phantomcrash
