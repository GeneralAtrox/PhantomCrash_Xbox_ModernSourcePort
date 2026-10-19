#pragma once

#include "phantomcrash/battle_scene.hpp"
#include "phantomcrash/rumbling_world.hpp"
#include "phantomcrash/match_result.hpp"
#include <map>
#include <tuple>

namespace phantomcrash {
namespace online {
class MatchSession;
class Presentation;
} // namespace online

struct CoopMatch {
  // Transport/prediction ownership is outside rollback state.
  std::shared_ptr<online::MatchSession> online;
  std::array<std::weak_ptr<online::Presentation>, 4> presentations;
  bool network_prepared{};
  std::shared_ptr<void> resources; // owns the tables/assets referenced by an online world
  struct Checkpoint;
  [[nodiscard]] std::shared_ptr<const Checkpoint> checkpoint() const;
  void restore_checkpoint(const Checkpoint &state);
  struct Participant {
    int actor{-1};
    RobotParts look;
    std::int64_t bounty{};
    std::vector<int> beaten;
    bool won{};
    double play_seconds{};
    float victory_hold{};
    std::optional<MatchOutcome> finished;
  };
  struct Settlement {
    MatchSummary summary;
    std::int64_t bounty{};
    std::vector<int> beaten;
    bool won{}, ready{};
  };
  std::array<std::optional<Settlement>, 4> settlements;
  int day{}, area{}, contest{};
  std::shared_ptr<RumblingWorld> world;
  std::shared_ptr<Battle> battle;
  QuestSave simulation_save;
  std::unique_ptr<BattleScene> entrances;
  // Online player entrances/exits must survive rollback even without a local view.
  // Offline presenters retain their existing ownership until explicitly bound.
  std::array<std::shared_ptr<BattleScene>, 4> rider_scenes;
  // The looks bound to simulation-owned poses. Rollback restores this alongside
  // the poses so a replacement's model cannot be mistaken for the previous one.
  std::vector<std::optional<RobotParts>> simulation_looks;
  std::array<Participant, 4> players{};
  [[nodiscard]] static const std::array<Participant, 4> &players_in(const Checkpoint &state);
  int look_revision{};
  double elapsed{};
  CoopOptions options;
  void collect_rewards();
  int join(int slot, RobotParts look);
  std::int64_t leave(int slot, QuestSave &save);
  void tick(float seconds);
  [[nodiscard]] bool empty() const;
  [[nodiscard]] RobotParts look(int actor) const;
};

class CoopMatches {
public:
  using Key = std::tuple<int, int, int>;
  std::map<Key, std::shared_ptr<CoopMatch>> matches;
  void tick(float seconds);
};

} // namespace phantomcrash
