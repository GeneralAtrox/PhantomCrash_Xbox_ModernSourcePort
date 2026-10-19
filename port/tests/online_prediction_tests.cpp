#include "test_support.hpp"
#include "phantomcrash/online_prediction.hpp"

using namespace phantomcrash::online;

namespace {
// This fixture exercises the timeline, not game determinism. Battle integration
// tests must additionally compare the actual combat and match state.
struct CounterSimulation final : Simulation {
  struct State final : Checkpoint {
    std::array<float, max_players> positions{};
    std::array<int, max_players> toggles{};
  };
  State state;
  std::unique_ptr<Checkpoint> save() const override {
    return std::make_unique<State>(state);
  }
  void restore(const Checkpoint &checkpoint) override {
    state = dynamic_cast<const State &>(checkpoint);
  }
  void step(const InputFrame &frame) override {
    for (std::uint8_t player = 0; player < max_players; ++player) {
      if (frame.players & (1U << player)) {
        const auto &input = frame.inputs[player];
        state.positions[player] += input.control.move[1] + input.control.mouse_aim[0];
        state.toggles[player] += input.fire.camouflage ? 1 : 0;
      }
    }
  }
};

PlayerInput input(std::uint32_t tick, std::uint8_t player) {
  PlayerInput out;
  out.tick = tick;
  out.player = player;
  out.control.move[1] = static_cast<float>((tick / 5 + player) % 3) * 0.25F;
  out.control.mouse_aim[0] = tick % 7 == 0 ? 0.125F : 0;
  out.fire.camouflage = tick % 11 == 0;
  return out;
}
} // namespace

TEST_CASE("three-player prediction converges after delayed reordered duplicate inputs") {
  CounterSimulation expected, actual;
  Prediction reference(expected, 7, 0, 0, 12);
  Prediction network(actual, 7, 0, 8, 12);
  for (std::uint32_t tick = 0; tick < 100; ++tick) {
    for (std::uint8_t player = 0; player < 3; ++player) {
      reference.submit(input(tick, player));
    }
    CHECK(reference.advance());
    network.submit(input(tick, 0));
    if (tick >= 3) {
      network.submit(input(tick - 3, 1));
      network.submit(input(tick - 3, 1));
    }
    if (tick >= 5) {
      network.submit(input(tick - 5, 2));
    }
    CHECK(network.advance());
  }
  // Reverse arrival order, across both remaining remote players.
  for (int tick = 99; tick >= 95; --tick) {
    network.submit(input(static_cast<std::uint32_t>(tick), 2));
    network.submit(input(static_cast<std::uint32_t>(tick), 1));
  }
  network.reconcile();
  CHECK(network.next_tick() == 100 && network.confirmed_tick() == 100);
  CHECK(network.rollback_count() > 0);
  CHECK(actual.state.positions == expected.state.positions);
  CHECK(actual.state.toggles == expected.state.toggles);
}

TEST_CASE("prediction stalls at its limit then resumes without dropping ticks") {
  CounterSimulation simulation;
  Prediction timeline(simulation, 3, 0, 2, 4);
  CHECK(timeline.advance());
  CHECK(timeline.advance());
  CHECK(!timeline.advance());
  CHECK(timeline.next_tick() == 2);
  timeline.submit(input(0, 1));
  CHECK(!timeline.advance());
  timeline.submit(input(0, 0));
  CHECK(timeline.advance());
  CHECK(timeline.next_tick() == 3);
  CHECK(!timeline.advance());
}

TEST_CASE("confirmed inspection reconciles history and always restores the predicted present") {
  CounterSimulation simulation;
  Prediction timeline(simulation, 3, 0, 4, 4);
  for (std::uint32_t tick = 0; tick < 3; ++tick) {
    timeline.submit(input(tick, 0));
    CHECK(timeline.advance());
  }
  CHECK(!timeline.inspect_confirmed(1, [] {}));
  timeline.submit(input(0, 1));
  timeline.submit(input(1, 1));
  std::array<float, max_players> observed;
  CHECK(timeline.inspect_confirmed(2, [&] { observed = simulation.state.positions; }));
  CHECK(observed[1] > 0);
  const auto present = simulation.state;
  CHECK(timeline.next_tick() == 3 && timeline.confirmed_tick() == 2);
  CHECK(timeline.rollback_count() == 1);
  CHECK_THROWS(
      timeline.inspect_confirmed(1, [] { throw std::runtime_error("inspection failed"); }));
  CHECK(simulation.state.positions == present.positions);
  CHECK(simulation.state.toggles == present.toggles);
  CHECK(timeline.next_tick() == 3 && timeline.confirmed_tick() == 2);
  CHECK(!timeline.inspect_confirmed(3, [] {}));
  timeline.submit(input(2, 1));
  CHECK(timeline.inspect_confirmed(3, [&] { observed = simulation.state.positions; }));
  CHECK(observed == simulation.state.positions);
  for (std::uint32_t tick = 3; tick < 9; ++tick) {
    timeline.submit(input(tick, 0));
    timeline.submit(input(tick, 1));
    CHECK(timeline.advance());
  }
  CHECK(!timeline.inspect_confirmed(0, [] {})); // old history was pruned
}

TEST_CASE("prediction rejects conflicting confirmed input") {
  CounterSimulation simulation;
  Prediction timeline(simulation, 3);
  auto value = input(0, 1);
  timeline.submit(value);
  value.control.jump = true;
  bool rejected = false;
  try {
    timeline.submit(value);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  CHECK(rejected);
}
