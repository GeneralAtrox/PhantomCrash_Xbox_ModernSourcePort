#pragma once
#include "phantomcrash/input.hpp"
#include <utility>

namespace phantomcrash::online {
// Desktop events received while a network tick is waiting. Levels use the
// latest sample; consumable edges and mouse deltas survive until capture.
class PendingInput final {
public:
  void push(const phantomcrash::InputFrame &input) {
    auto previous = std::exchange(input_, input);
    input_.pressed |= previous.pressed;
    input_.clicked |= previous.clicked;
    input_.right_clicked |= previous.right_clicked;
    input_.mouse_double_clicked |= previous.mouse_double_clicked;
    input_.pointer_moved |= previous.pointer_moved;
    input_.mouse_fire_left |= previous.mouse_fire_left;
    input_.mouse_fire_right |= previous.mouse_fire_right;
    input_.mouse_wheel += previous.mouse_wheel;
    for (std::size_t axis = 0; axis < input_.mouse_aim.size(); ++axis) {
      input_.mouse_aim[axis] += previous.mouse_aim[axis];
    }
    for (std::size_t port = 0; port < input_.ports.size(); ++port) {
      input_.ports[port].pressed |= previous.ports[port].pressed;
    }
    if (!input_.dialog_choice) {
      input_.dialog_choice = previous.dialog_choice;
    }
  }
  phantomcrash::InputFrame take() {
    return std::exchange(input_, {});
  }

private:
  phantomcrash::InputFrame input_;
};
} // namespace phantomcrash::online
