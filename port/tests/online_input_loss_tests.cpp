#include "test_support.hpp"
#include "phantomcrash/online_input_channel.hpp"
#include <chrono>
#include <memory>
#include <random>
#include <thread>

using namespace phantomcrash::online;

namespace {
// Folds every confirmed control into one number; any divergence changes it.
class Accumulator final : public Simulation {
public:
  struct State final : Checkpoint {
    std::uint64_t value{};
  };
  [[nodiscard]] std::unique_ptr<Checkpoint> save() const override {
    auto state = std::make_unique<State>();
    state->value = value;
    return state;
  }
  void restore(const Checkpoint &state) override {
    value = static_cast<const State &>(state).value;
  }
  void step(const InputFrame &frame) override {
    for (std::uint8_t player = 0; player < max_players; ++player) {
      if (frame.players & (1U << player)) {
        const auto &control = frame.inputs[player].control;
        value = value * 1099511628211ULL +
                static_cast<std::uint64_t>(control.move[0] * 1000.0F + control.move[1] * 10.0F) +
                (control.jump ? 7U : 0U);
      }
    }
  }
  std::uint64_t value{14695981039346656037ULL};
};

PlayerInput control(const std::uint32_t tick, const std::uint8_t player) {
  PlayerInput input;
  input.tick = tick;
  input.player = player;
  input.control.move = {static_cast<float>((tick / 7 + player) % 5) * 0.2F, 0.5F};
  input.control.jump = (tick + player) % 11 == 0;
  return input;
}
} // namespace

TEST_CASE("online controls reach every peer through the host despite heavy datagram loss") {
  RoomOptions options;
  options.host = true;
  options.bind = Endpoint::parse("127.0.0.1", 0);
  options.capacity = 3;
  std::array<std::unique_ptr<Room>, 3> rooms;
  rooms[0] = std::make_unique<Room>(options);
  options.host = false;
  options.server = rooms[0]->local_endpoint();
  rooms[1] = std::make_unique<Room>(options);
  rooms[2] = std::make_unique<Room>(options);
  double now = 0;
  const auto service = [&] {
    for (auto &room : rooms) {
      room->update(now);
    }
  };
  for (int pass = 0; pass < 60 && rooms[0]->participants() != 7; ++pass) {
    service();
    now += 0.005;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(rooms[0]->participants() == 7);
  service();
  std::array<Accumulator, 3> simulations;
  std::array<std::unique_ptr<Prediction>, 3> predictions;
  std::array<std::unique_ptr<InputChannel>, 3> channels;
  for (std::uint8_t slot = 0; slot < 3; ++slot) {
    predictions[slot] = std::make_unique<Prediction>(simulations[slot], 7);
    channels[slot] = std::make_unique<InputChannel>(*rooms[slot], *predictions[slot], 0x101, 7);
  }
  // A third of the arriving controls (and the acknowledgements they carry) are lost.
  std::mt19937 loss(1234);
  std::uint64_t lost = 0;
  const auto pump = [&] {
    for (int pass = 0; pass < 2; ++pass) {
      service();
      for (std::uint8_t slot = 0; slot < 3; ++slot) {
        for (const auto &message : rooms[slot]->take_messages()) {
          if (loss() % 3 == 0) {
            ++lost;
            continue;
          }
          CHECK(channels[slot]->receive(message, now));
        }
        predictions[slot]->reconcile();
        channels[slot]->update(now);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  constexpr std::uint32_t ticks = 240;
  std::array<std::optional<std::uint32_t>, 3> sent;
  for (int frame = 0; frame < 4000; ++frame) {
    bool finished = true;
    for (std::uint8_t slot = 0; slot < 3; ++slot) {
      auto &prediction = *predictions[slot];
      if (prediction.next_tick() < ticks) {
        if (sent[slot] != prediction.next_tick()) {
          channels[slot]->submit(control(prediction.next_tick(), slot), now);
          sent[slot] = prediction.next_tick();
        }
        static_cast<void>(prediction.advance());
      }
      finished = finished && prediction.confirmed_tick() >= ticks;
    }
    if (finished) {
      break;
    }
    pump();
    now += 1.0 / 60.0;
  }
  CHECK(lost > 0);
  for (std::uint8_t slot = 0; slot < 3; ++slot) {
    CHECK(predictions[slot]->next_tick() == ticks);
    CHECK(predictions[slot]->confirmed_tick() == ticks);
    CHECK(simulations[slot].value == simulations[0].value);
  }
  // Acknowledgements catch up too, so nothing stays queued for resending.
  for (int frame = 0; frame < 200; ++frame) {
    pump();
    now += 1.0 / 60.0;
  }
  for (std::uint8_t slot = 0; slot < 3; ++slot) {
    for (std::uint8_t peer = 0; peer < 3; ++peer) {
      CHECK(channels[slot]->unacknowledged(peer) == 0);
    }
  }
}
