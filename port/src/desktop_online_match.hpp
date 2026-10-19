#pragma once

#include "desktop_coop_matches.hpp"
#include "phantomcrash/online_prediction.hpp"
#include "phantomcrash/online_state.hpp"

namespace phantomcrash::online {
[[nodiscard]] StateDigest state_digest(const CoopMatch &match, AssetDigests *assets = nullptr);
// Named digests and readable values of the match's parts, one log line each.
[[nodiscard]] std::vector<std::string> state_sections(const CoopMatch &match);

class MatchAssets {
public:
  virtual ~MatchAssets() = default;
  // Static session assets provide poses/collision, independently of local views.
  virtual void prepare(CoopMatch &match) = 0;
};

// The simulation side of an online co-op match. Local views capture controls
// and TargetView; every peer runs the same player-slot order and world update.
// Rendering, sound playback and persistent profile writes are outside step().
class MatchSimulation final : public Simulation {
public:
  MatchSimulation(CoopMatch &match, std::uint8_t players, std::uint32_t first_tick = 0,
                  MatchAssets *assets = nullptr);
  [[nodiscard]] std::unique_ptr<Checkpoint> save() const override;
  void restore(const Checkpoint &state) override;
  void step(const InputFrame &frame) override;
  // The match inside one of this simulation's saved states.
  [[nodiscard]] static std::shared_ptr<const CoopMatch::Checkpoint>
  match_state(const Checkpoint &state);

private:
  struct State;
  CoopMatch &match_;
  MatchAssets *assets_{}; // session-owned, outlives this simulation and its checkpoints
  std::uint8_t players_{};
  std::uint32_t next_{};
};

} // namespace phantomcrash::online
