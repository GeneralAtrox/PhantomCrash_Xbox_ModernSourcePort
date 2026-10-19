#pragma once

#include "phantomcrash/json.hpp"

#include <vector>

namespace phantomcrash {

// How the CURRENT MUSIC meter hears the music (the port's own; the original's
// drawer is not identified). Each bar is a band of frequencies, spaced evenly
// in pitch from low_hz to high_hz, bass on the left.
//
// Music is far louder in the bass than the treble, and louder in some parts
// than others, so a meter on raw loudness sits with its left bars full and its
// right bars empty. Each band is instead measured against its own recent
// past: a band at its running average stands at `centre`, and it takes
// `reach` running spreads (standard deviations, in dB) above the average to
// fill the bar, so every bar is used and jumps on the beats of its own band.
struct SpectrumStyle {
  int window{2048}; // samples per look, a power of two
  float low_hz{60.0F};
  float high_hz{14000.0F};
  double memory_seconds{3.0}; // how far back the average and spread reach
  float centre{0.3F};
  float reach{2.0F};
  float min_spread_db{3.0F}; // so a steady band does not flick over the full height
  float gate_db{-70.0F};     // a band quieter than this is silent
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] SpectrumStyle parse_spectrum_style(const json::JsonValue &value);

// The power of each of `bands` bands in `samples` (mono, Hann windowed, the
// size a power of two), in dB where a full-scale sine is about 0.
[[nodiscard]] std::vector<float> band_decibels(const std::vector<float> &samples, int rate,
                                               int bands, float low_hz, float high_hz);

class MusicSpectrum final {
public:
  MusicSpectrum(SpectrumStyle style, int bars);

  // The last window of mono samples heard, at `rate` samples a second, once
  // a tick.
  void hear(const std::vector<float> &samples, int rate, double tick_seconds);
  // Nothing heard: every level drops to 0. The running averages are kept.
  void silence();
  [[nodiscard]] const SpectrumStyle &style() const noexcept {
    return style_;
  }
  // Each bar's level, 0 to 1.
  [[nodiscard]] const std::vector<float> &levels() const noexcept {
    return levels_;
  }

private:
  struct Band {
    double mean{};
    double variance{};
    double seconds{}; // heard for, up to memory_seconds
  };

  SpectrumStyle style_;
  std::vector<Band> bands_;
  std::vector<float> levels_;
};

} // namespace phantomcrash
