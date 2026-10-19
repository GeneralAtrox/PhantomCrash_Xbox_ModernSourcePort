#include "test_support.hpp"
#include "phantomcrash/online_input.hpp"
#include <bit>
#include <limits>

using namespace phantomcrash;
using namespace phantomcrash::online;

namespace {
PlayerInput sample() {
  PlayerInput out;
  out.tick = 0x12345678;
  out.player = 2;
  out.control.move = {-0.0F, 0.375F};
  out.control.strafe = -0.25F;
  out.control.twist = 0.5F;
  out.control.pitch = -1.0F;
  out.control.mouse_aim = {std::bit_cast<float>(0x3f000001U), -0.003F};
  out.control.jump = out.control.dash = out.control.reset_twist = true;
  out.control.down_boost = out.control.standard = out.control.quick_turn = true;
  out.control.dodge = -1;
  out.fire.fire = {true, false, true, true};
  out.fire.camouflage = true;
  out.view = TargetView{};
  for (std::size_t i = 0; i < 16; ++i) {
    out.view->view_projection.m[i] = static_cast<float>(i) * 0.37F - 1.0F;
  }
  out.view->eye = {19, -2.3F, 55};
  out.view->boxes = LockBoxes{{-0.1F, 0.2F}, {0.3F, 0.4F}, {0.5F, 0.6F}};
  return out;
}

bool rejected(std::span<const std::uint8_t> bytes) {
  try {
    static_cast<void>(decode_input(bytes));
    return false;
  } catch (const std::runtime_error &) {
    return true;
  }
}
} // namespace

TEST_CASE("online inputs preserve float bits, local aiming and every control") {
  auto input = sample();
  for (int variant = 0; variant < 3; ++variant) {
    const auto bytes = encode_input(input);
    CHECK(bytes.size() == (variant == 0 ? 136 : variant == 1 ? 112 : 36));
    CHECK(bytes[0] == 0x78 && bytes[1] == 0x56 && bytes[2] == 0x34 && bytes[3] == 0x12);
    CHECK(bytes[4] == 2 && bytes[7] == 0);
    const auto decoded = decode_input(bytes);
    CHECK(same_input(input, decoded));
    CHECK(std::bit_cast<std::uint32_t>(decoded.control.move[0]) == 0x80000000U);
    CHECK(decoded.control.mouse_aim == input.control.mouse_aim);
    CHECK(decoded.fire.fire == input.fire.fire);
    if (variant == 0) {
      input.view->boxes.reset();
    } else {
      input.view.reset();
    }
  }
}

TEST_CASE("online input rejects truncated and malformed datagrams") {
  const auto bytes = encode_input(sample());
  for (std::size_t count = 0; count < bytes.size(); ++count) {
    CHECK(rejected(std::span(bytes).first(count)));
  }
  auto bad = bytes;
  bad.push_back(0);
  CHECK(rejected(bad));
  bad = bytes;
  bad[4] = max_players;
  CHECK(rejected(bad));
  bad = bytes;
  bad[7] = 3;
  CHECK(rejected(bad));
  bad = bytes;
  bad[6] |= 0x80;
  CHECK(rejected(bad));
  bad = bytes;
  bad[6] &= ~0x08;
  CHECK(rejected(bad));
  bad = bytes;
  bad[8] = 0;
  bad[9] = 0;
  bad[10] = 0x80;
  bad[11] = 0x7f;
  CHECK(rejected(bad));
}

TEST_CASE("remote input prediction repeats holds but never mouse or button edges") {
  const auto previous = sample();
  const auto predicted = predict_input(previous, previous.tick + 1);
  CHECK(predicted.tick == previous.tick + 1);
  CHECK(predicted.player == previous.player);
  CHECK(predicted.control.move == previous.control.move);
  CHECK(predicted.control.jump && predicted.control.standard && predicted.control.quick_turn);
  CHECK(predicted.fire.fire == previous.fire.fire);
  CHECK(!predicted.fire.camouflage && !predicted.control.dash && !predicted.control.reset_twist);
  CHECK(!predicted.control.down_boost && predicted.control.dodge == 0);
  CHECK(predicted.control.mouse_aim == (std::array<float, 2>{}));
  CHECK(predicted.view->view_projection.m == previous.view->view_projection.m);
  CHECK(previous.control.dash && previous.fire.camouflage);
}
