#pragma once

#include "phantomcrash/settings.hpp"
#include "phantomcrash/online_prediction.hpp"
#include "phantomcrash/online_room.hpp"
#include <future>
#include <functional>
#include <memory>

namespace phantomcrash {
namespace online {
class Lobby;
}
class CoopMatches;
class OnlineGame;
struct CoopState {
  bool enabled{};
  CoopOptions options;
  int robots{default_rumble_robots};
  std::uint32_t input_delay{online::default_input_delay_ticks}; // online matches' (Settings)
  std::array<std::optional<QuestSave>, 4> profiles{};
  std::array<std::optional<QuestSave>, 4> saved_profiles{};
  std::shared_ptr<CoopMatches> matches;
  std::shared_ptr<online::Lobby> online;
  std::shared_ptr<OnlineGame> online_game;
  std::function<std::shared_ptr<online::Lobby>()> open_online;
  bool online_mode{}, online_host{};
  std::function<online::Compatibility()> fingerprint;
  std::shared_future<online::Compatibility> pending_fingerprint;
  std::optional<QuestSave> online_profile;
  // The last host typed into "Join by address", offered again this session.
  std::string last_address;
  // Explicit Save completes only after the host has written this profile.
  std::function<bool(int, const QuestSave &)> save_profile;
};
} // namespace phantomcrash
