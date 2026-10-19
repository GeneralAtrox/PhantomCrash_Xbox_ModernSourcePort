#include "test_support.hpp"
#include "../src/desktop_online_input.hpp"

using namespace phantomcrash;

TEST_CASE("network stalls retain local mouse motion and edges until one capture") {
  online::PendingInput pending;
  InputFrame first;
  first.held = button::a;
  first.pressed = button::a | button::x;
  first.mouse_aim = {0.125F, -0.25F};
  first.mouse_fire_left = true;
  first.ports[0].pressed = button::a;
  pending.push(first);
  InputFrame second;
  second.held = button::b;
  second.pressed = button::b;
  second.left_stick = {0.25F, -0.5F};
  second.mouse_aim = {-0.0625F, 0.5F};
  pending.push(second);
  const auto captured = pending.take();
  CHECK(captured.held == button::b);
  CHECK(captured.pressed == (button::a | button::b | button::x));
  CHECK(captured.left_stick == second.left_stick);
  CHECK(captured.mouse_aim == (std::array<float, 2>{0.0625F, 0.25F}));
  CHECK(captured.mouse_fire_left);
  CHECK(captured.ports[0].pressed == button::a);
  second.pressed = 0;
  second.mouse_aim = {};
  pending.push(second);
  const auto next = pending.take();
  CHECK(next.held == button::b);
  CHECK(next.pressed == 0);
  CHECK(next.mouse_aim == (std::array<float, 2>{}));
  CHECK(!next.mouse_fire_left);
  CHECK(next.ports[0].pressed == 0);
}
