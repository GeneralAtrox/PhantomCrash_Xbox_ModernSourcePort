#include "desktop_coop_matches.hpp"
#include <stdexcept>

namespace phantomcrash {

struct CoopMatch::Checkpoint {
  const CoopMatch *owner{};
  std::shared_ptr<RumblingWorld> world;
  std::shared_ptr<Battle> battle;
  std::shared_ptr<const RumblingWorld::Checkpoint> world_state;
  std::optional<BattleScene> entrances;
  std::array<std::shared_ptr<BattleScene>, 4> riders;
  std::array<std::optional<BattleScene>, 4> rider_states;
  std::vector<std::optional<RobotParts>> simulation_looks;
  QuestSave simulation_save;
  std::array<Participant, 4> players;
  std::array<std::optional<Settlement>, 4> settlements;
  CoopOptions options;
  int day{}, area{}, contest{}, look_revision{};
  double elapsed{};
};

std::shared_ptr<const CoopMatch::Checkpoint> CoopMatch::checkpoint() const {
  if (!world || !battle) {
    throw std::runtime_error("Cannot checkpoint an uninitialized co-op match");
  }
  auto state = std::make_shared<Checkpoint>();
  state->owner = this;
  state->world = world;
  state->battle = battle;
  state->world_state = world->checkpoint();
  if (entrances) {
    state->entrances = *entrances;
  }
  state->riders = rider_scenes;
  state->simulation_looks = simulation_looks;
  for (std::size_t slot = 0; slot < rider_scenes.size(); ++slot) {
    if (rider_scenes[slot]) {
      state->rider_states[slot] = *rider_scenes[slot];
    }
  }
  state->simulation_save = simulation_save;
  state->players = players;
  state->settlements = settlements;
  state->options = options;
  state->day = day;
  state->area = area;
  state->contest = contest;
  state->look_revision = look_revision;
  state->elapsed = elapsed;
  return state;
}

const std::array<CoopMatch::Participant, 4> &CoopMatch::players_in(const Checkpoint &state) {
  return state.players;
}

void CoopMatch::restore_checkpoint(const Checkpoint &state) {
  if (state.owner != this || world != state.world || battle != state.battle) {
    throw std::runtime_error("Cannot restore a checkpoint into another co-op match");
  }
  world->restore_checkpoint(*state.world_state);
  if (state.entrances) {
    if (entrances) {
      *entrances = *state.entrances;
    } else {
      entrances = std::make_unique<BattleScene>(*state.entrances);
    }
  } else {
    entrances.reset();
  }
  rider_scenes = state.riders;
  simulation_looks = state.simulation_looks;
  for (std::size_t slot = 0; slot < rider_scenes.size(); ++slot) {
    if (state.rider_states[slot]) {
      *rider_scenes[slot] = *state.rider_states[slot];
    }
  }
  simulation_save = state.simulation_save;
  players = state.players;
  settlements = state.settlements;
  options = state.options;
  day = state.day;
  area = state.area;
  contest = state.contest;
  look_revision = state.look_revision;
  elapsed = state.elapsed;
}

} // namespace phantomcrash
