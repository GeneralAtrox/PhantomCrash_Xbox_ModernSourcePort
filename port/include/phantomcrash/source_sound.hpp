#pragma once

#include "phantomcrash/json.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace phantomcrash {
struct SourceSoundSample {
  std::vector<float> pcm;
  int rate{};
};
struct SourceSoundNote {
  SourceSoundSample sample;
  double start{}, end{}, pitch{1};
  std::size_t loop_start{}, loop_end{};
  bool repeat{};
  unsigned adsr1{}, adsr2{};
  std::array<float, 2> gain{1, 1};
};
struct SourceSound {
  std::vector<SourceSoundNote> notes;
  double loop_start{}, loop_end{};
};
using SourceSoundLoader = std::function<SourceSoundSample(const std::string &)>;
[[nodiscard]] std::shared_ptr<const SourceSound> parse_source_sound(const json::JsonValue &value,
                                                                    const SourceSoundLoader &load);

// The source SPU envelope registers, stepped at 48 kHz independently of the
// host audio rate. Sample interpolation remains the port's float adapter.
class SourceEnvelope final {
public:
  SourceEnvelope(unsigned adsr1, unsigned adsr2) : adsr1_(adsr1), adsr2_(adsr2) {}
  void release();
  [[nodiscard]] float step();
  [[nodiscard]] bool finished() const {
    return phase_ == 3 && level_ == 0;
  }

private:
  unsigned adsr1_, adsr2_;
  int phase_{}, level_{}, counter_{};
};

class SourceSoundPlayback final {
public:
  SourceSoundPlayback(std::shared_ptr<const SourceSound> source, int output_rate,
                      bool repeat_sequence = true);
  void release();
  [[nodiscard]] std::array<float, 2> sample();
  [[nodiscard]] bool finished() const {
    return released_ && voices_.empty();
  }

private:
  struct Note {
    const SourceSoundNote *source{};
    SourceEnvelope envelope;
    double position{};
    std::uint64_t age{}, duration{};
    int envelope_clock{};
    float level{};
  };
  std::shared_ptr<const SourceSound> source_;
  std::vector<Note> voices_;
  std::vector<std::pair<std::uint64_t, const SourceSoundNote *>> schedule_;
  std::size_t next_note_{};
  int rate_;
  std::uint64_t frame_{}, loop_start_{}, loop_end_{};
  bool released_{};
  bool repeat_{};
};
} // namespace phantomcrash
