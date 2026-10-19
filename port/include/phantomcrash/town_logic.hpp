#pragma once

#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/town_screen.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

// What the logic of every town screen keeps besides its own state: the
// black fades in and out, the time since it opened and the sounds it asks
// for. A screen's update calls advance() first and takes input only when it
// returns true.
class TownLogic {
public:
  explicit TownLogic(const MenuBasics &basics) : fade_(basics.fade_in, basics.fade_out) {}

  [[nodiscard]] bool done() const noexcept {
    return fade_.done();
  }
  [[nodiscard]] std::string next() const {
    return fade_.done() ? fade_.next() : std::string{};
  }
  [[nodiscard]] bool faded_in() const noexcept {
    return !fade_.fading_in();
  }
  [[nodiscard]] bool leaving() const noexcept {
    return fade_.leaving();
  }
  [[nodiscard]] float black_alpha() const noexcept {
    return fade_.black_alpha();
  }
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] std::vector<std::string> take_sounds() {
    return sounds_.take();
  }

protected:
  // Advances the clock and the fades; true when the screen takes input.
  bool advance(const double tick_seconds) {
    seconds_ += tick_seconds;
    return fade_.update(tick_seconds) && !fade_.leaving();
  }
  // Fades out to `target`; the screen is done once the fade ends.
  void leave(const std::string &target) {
    fade_.leave(target);
  }
  void sound(const std::string &name) {
    sounds_.play(name);
  }

private:
  ScreenFade fade_;
  SoundQueue sounds_;
  double seconds_{};
};

} // namespace phantomcrash
