#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"

#include <array>

namespace phantomcrash {

struct LevelControlStyle {
  double delay{0.35};
  double interval{0.1};
  double accelerate_after{0.8};
  double fast_interval{1.0 / 30.0};
  double sound_interval{0.07};
  int coarse_step{10};
  Buttons coarse_back{button::white}, coarse_forward{button::black};
  Buttons minimum{button::left_trigger}, maximum{button::right_trigger};
};

LevelControlStyle parse_level_control(const json::JsonValue &value);

struct LevelMeterHit {
  float zero_x{}, step{1}, top{}, bottom{};
};

struct LevelChange {
  int value{};
  bool sound{};
};

// Independent D-pad and stick/key clocks add their steps. Sound cadence is
// independent of the number of levels crossed, including coarse moves and drag.
class LevelControl {
public:
  void reset();
  LevelChange update(double seconds, const InputFrame &input, int level, int minimum, int maximum,
                     Buttons left, Buttons right, const LevelControlStyle &style,
                     const LevelMeterHit &meter);

private:
  struct Repeat {
    int direction{};
    double held{}, next{};
    int step(int wanted, double seconds, const LevelControlStyle &style);
  };
  std::array<Repeat, 3> repeat_{};
  bool dragging_{};
  double sound_wait_{};
};

} // namespace phantomcrash
