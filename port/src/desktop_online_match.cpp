#include "desktop_online_match.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace phantomcrash::online {
namespace {
void update_result(CoopMatch &match, const int slot) {
  auto &participant = match.players[slot];
  if (participant.finished) {
    return;
  }
  const auto &robot = match.battle->robot(participant.actor);
  const auto &scene = match.rider_scenes[slot];
  if (robot.alive && (!scene || !scene->scripted())) {
    participant.play_seconds += tick_seconds;
  }
  if (!robot.alive) {
    if (!robot.dying) {
      participant.finished = MatchOutcome::retire;
    }
  } else if (participant.won &&
             std::ranges::none_of(match.battle->robots(), [](const auto &r) { return r.dying; })) {
    participant.victory_hold -= tick_seconds;
    if (participant.victory_hold <= 0) {
      participant.finished = MatchOutcome::win;
    }
  } else if (scene && scene->finished()) {
    participant.finished = MatchOutcome::leave;
  }
}
} // namespace

struct MatchSimulation::State final : Checkpoint {
  std::shared_ptr<const CoopMatch::Checkpoint> match;
  std::uint32_t next{};
};

MatchSimulation::MatchSimulation(CoopMatch &match, const std::uint8_t players,
                                 const std::uint32_t first_tick, MatchAssets *assets)
    : match_(match), assets_(assets), players_(players), next_(first_tick) {
  if (!players || players >= (1U << max_players) || !match.world || !match.battle ||
      !match.battle->cooperative()) {
    throw std::runtime_error("Invalid online co-op simulation");
  }
  match.world->prepare_deterministic();
  if (assets_) {
    assets_->prepare(match_);
  }
}

std::unique_ptr<Simulation::Checkpoint> MatchSimulation::save() const {
  auto state = std::make_unique<State>();
  state->match = match_.checkpoint();
  state->next = next_;
  return state;
}

void MatchSimulation::restore(const Checkpoint &checkpoint) {
  const auto &state = dynamic_cast<const State &>(checkpoint);
  match_.restore_checkpoint(*state.match);
  next_ = state.next;
}

std::shared_ptr<const CoopMatch::Checkpoint> MatchSimulation::match_state(const Checkpoint &state) {
  return dynamic_cast<const State &>(state).match;
}

void MatchSimulation::step(const InputFrame &frame) {
  if (frame.players != players_ || frame.tick != next_ ||
      next_ == std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error("Online co-op frame is out of order");
  }
  // Validate the whole frame before mutating simulation state.
  for (std::uint8_t slot = 0; slot < max_players; ++slot) {
    const bool present = (players_ & (1U << slot)) != 0;
    const int actor = match_.players[slot].actor;
    if (present != (actor >= 0) ||
        (present && actor >= static_cast<int>(match_.battle->robots().size()))) {
      throw std::runtime_error("Online co-op membership changed without a new timeline");
    }
    if (present) {
      const auto &input = frame.inputs[slot];
      if (input.tick != next_ || input.player != slot) {
        throw std::runtime_error("Online co-op input belongs to another tick or player");
      }
      static_cast<void>(encode_input(input));
    }
  }
  if (assets_) {
    assets_->prepare(match_);
  }
  auto &battle = *match_.battle;
  // Each post-tick snapshot contains only that tick's presentation outboxes.
  // Local views must not determine when replicated simulation clears them.
  if (auto *doors = battle.doors()) {
    static_cast<void>(doors->take_sounds());
  }
  if (auto *objects = battle.objects()) {
    static_cast<void>(objects->take_sounds());
  }
  for (const auto &scene : match_.rider_scenes) {
    if (scene) {
      static_cast<void>(scene->take_sounds());
      static_cast<void>(scene->take_music());
    }
  }
  if (match_.entrances) {
    static_cast<void>(match_.entrances->take_sounds());
    static_cast<void>(match_.entrances->take_music());
  }
  for (std::uint8_t slot = 0; slot < max_players; ++slot) {
    if (!(players_ & (1U << slot))) {
      continue;
    }
    const auto &input = frame.inputs[slot];
    const int actor = match_.players[slot].actor;
    auto &scene = match_.rider_scenes[slot];
    battle.select_player(actor);
    battle.set_player_target_view(input.view);
    const bool scripted = scene && scene->scripted();
    if (scene) {
      scene->prepare(battle, tick_seconds);
    }
    battle.set_player_control(actor, scripted ? RobotControl{} : input.control,
                              scripted ? FireControl{} : input.fire);
    if (scene) {
      // Local co-op observes the previous battle events before the shared
      // match tick. Preserve that phase when a remote player has no view.
      scene->observe(battle);
    }
    update_result(match_, slot);
    if (match_.players[slot].finished) {
      battle.set_player_control(actor, {}, {});
    }
  }
  match_.tick(tick_seconds);
  if (assets_) {
    // The world can replace an actor at the end of this tick. Bind its pose
    // before any local view gets a chance to initialize it differently.
    assets_->prepare(match_);
  }
  ++next_;
}

} // namespace phantomcrash::online
