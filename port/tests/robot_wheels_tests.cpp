#include "test_support.hpp"
#include "phantomcrash/robot_mover.hpp"

#include <cmath>

using namespace phantomcrash;

namespace {
MovementTuning wheel_tuning() {
  MovementTuning tuning;
  tuning.wheels = WheeledTuning{28, 44, 5, 0.7F, 0.65F, 0.42F};
  tuning.dash_hold_seconds = 100;
  return tuning;
}
void drive(RobotMover &mover, const RobotControl &control, const int ticks) {
  for (int i = 0; i < ticks; ++i) {
    mover.tick(control, 1.0F / 60.0F);
  }
}
} // namespace

TEST_CASE("wheels accelerate, brake to rest, and reverse") {
  RobotMover mover(wheel_tuning());
  mover.place({}, 0);
  RobotControl input;
  input.move[1] = 1;
  mover.tick(input, 1.0F / 60.0F);
  CHECK(mover.local_velocity().z > 0);
  CHECK(mover.local_velocity().z < mover.tuning().walk_speed);
  drive(mover, input, 60);
  CHECK(std::abs(mover.local_velocity().z - mover.tuning().walk_speed) < 0.001F);
  drive(mover, {}, 60);
  CHECK(mover.mode() == MoveMode::stand);
  CHECK(length(mover.velocity()) == 0);
  input.move[1] = -1;
  drive(mover, input, 60);
  CHECK(mover.backward());
  CHECK(std::abs(mover.local_velocity().z + mover.tuning().back_speed) < 0.001F);
}

TEST_CASE("wheels steer without strafing and preserve momentum through turns") {
  RobotMover mover(wheel_tuning());
  mover.place({}, 0);
  RobotControl steer;
  steer.strafe = 1;
  drive(mover, steer, 10);
  CHECK(length(mover.position()) == 0);
  CHECK(mover.heading() > 0);
  RobotControl input;
  input.move[1] = 1;
  drive(mover, input, 60);
  const float before = length(mover.velocity());
  input.strafe = 1;
  mover.tick(input, 1.0F / 60.0F);
  CHECK(std::abs(mover.local_velocity().x) > 0.01F);
  CHECK(length(mover.velocity()) <= before + 0.001F);
  input.strafe = 0;
  const float slip = std::abs(mover.local_velocity().x);
  drive(mover, input, 30);
  CHECK(std::abs(mover.local_velocity().x) < slip * 0.1F);
}

TEST_CASE("wheel boost and jump retain the shared movement transitions") {
  RobotMover mover(wheel_tuning());
  mover.place({}, 0);
  RobotControl input;
  input.move[1] = 1;
  input.dash = true;
  drive(mover, input, 60);
  CHECK(mover.mode() == MoveMode::dash);
  CHECK(mover.local_velocity().z > mover.tuning().walk_speed);
  input.jump = true;
  mover.tick(input, 1.0F / 60.0F);
  CHECK(!mover.grounded());
  CHECK(mover.mode() == MoveMode::jump);
  CHECK(mover.position().y > 0);
  drive(mover, {}, 360);
  CHECK(mover.grounded());
  CHECK(std::isfinite(mover.position().x));
}

TEST_CASE("walking legs keep their lateral movement") {
  RobotMover mover(MovementTuning{});
  mover.place({}, 0);
  RobotControl input;
  input.strafe = 1;
  drive(mover, input, 30);
  CHECK(dot(mover.position(), heading_right(0)) > 0);
  CHECK(mover.heading() == 0);
}
