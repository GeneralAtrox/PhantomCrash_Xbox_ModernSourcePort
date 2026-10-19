// How the CURRENT MUSIC meter hears the music (MusicSpectrum).

#include "test_support.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "phantomcrash/music_spectrum.hpp"

using namespace phantomcrash;

namespace {

constexpr int rate = 44100;
constexpr double tick = 1.0 / 60.0;
constexpr int window = 2048;

struct Tone {
  double hz;
  double amplitude;
};

// The window ending at `end` samples into a sum of sines.
std::vector<float> heard(const std::vector<Tone> &tones, const long long end) {
  std::vector<float> samples(window);
  for (int index = 0; index < window; ++index) {
    const double time = static_cast<double>(end - window + index) / rate;
    double value = 0.0;
    for (const auto &tone : tones) {
      value += tone.amplitude * std::sin(2.0 * std::numbers::pi * tone.hz * time);
    }
    samples[static_cast<std::size_t>(index)] = static_cast<float>(value);
  }
  return samples;
}

// Plays the tones to the spectrum for `seconds`, a tick at a time, from
// sample `start`; returns where it got to.
long long play(MusicSpectrum &spectrum, const std::vector<Tone> &tones, const double seconds,
               long long start) {
  const auto step = static_cast<long long>(rate * tick);
  for (int count = 0; count < static_cast<int>(seconds / tick); ++count) {
    start += step;
    spectrum.hear(heard(tones, start), rate, tick);
  }
  return start;
}

std::size_t loudest(const std::vector<float> &decibels) {
  return static_cast<std::size_t>(
      std::distance(decibels.begin(), std::ranges::max_element(decibels)));
}

} // namespace

TEST_CASE("the meter's bands run from the bass on the left to the treble on the right") {
  const auto bass = band_decibels(heard({{100.0, 1.0}}, window), rate, 7, 60.0F, 14000.0F);
  CHECK(bass.size() == 7U);
  CHECK(loudest(bass) == 0U);
  // A full-scale sine is about 0 dB, and far above the bands it is not in.
  CHECK(std::abs(bass[0]) < 3.0F);
  CHECK(bass[6] < -40.0F);
  const auto treble = band_decibels(heard({{10000.0, 1.0}}, window), rate, 7, 60.0F, 14000.0F);
  CHECK(loudest(treble) == 6U);
  const auto middle = band_decibels(heard({{1000.0, 1.0}}, window), rate, 7, 60.0F, 14000.0F);
  CHECK(loudest(middle) == 3U);
}

TEST_CASE("every bar is used however loud its band is") {
  // Bass 40 dB louder than the treble, as music often is.
  const std::vector<Tone> steady{{80.0, 0.5}, {1000.0, 0.05}, {8000.0, 0.005}};
  MusicSpectrum spectrum(SpectrumStyle{}, 7);
  long long at = play(spectrum, steady, 4.0, window);
  const auto &levels = spectrum.levels();
  // Each band at its own average stands at the centre, the quiet treble too.
  for (const std::size_t band : {0U, 3U, 6U}) {
    CHECK(std::abs(levels[band] - SpectrumStyle{}.centre) < 0.05F);
  }
  // Bands with nothing in them are silent rather than noise blown up.
  CHECK(levels[2] == 0.0F);
  // A beat in the treble fills its bar and leaves the bass where it was.
  std::vector<Tone> beat = steady;
  beat[2].amplitude = 0.05;
  at = play(spectrum, beat, tick, at);
  CHECK(levels[6] == 1.0F);
  CHECK(std::abs(levels[0] - SpectrumStyle{}.centre) < 0.05F);
  // Held, the beat becomes the band's new normal.
  play(spectrum, beat, 8.0, at);
  CHECK(levels[6] < 0.5F);
}

TEST_CASE("the meter hears nothing in silence") {
  MusicSpectrum spectrum(SpectrumStyle{}, 7);
  play(spectrum, {{80.0, 0.5}, {8000.0, 0.1}}, 1.0, window);
  CHECK(spectrum.levels()[0] > 0.0F);
  spectrum.silence();
  CHECK(std::ranges::all_of(spectrum.levels(), [](const float level) { return level == 0.0F; }));
  spectrum.hear(std::vector<float>(window, 0.0F), rate, tick);
  CHECK(std::ranges::all_of(spectrum.levels(), [](const float level) { return level == 0.0F; }));
}

TEST_CASE("the meter's hearing is checked as it is read") {
  const auto style = parse_spectrum_style(json::parse(R"({"window": 1024, "centre": 0.4})"));
  CHECK(style.window == 1024);
  CHECK(style.centre == 0.4F);
  CHECK(style.reach == SpectrumStyle{}.reach);
  CHECK_THROWS(parse_spectrum_style(json::parse(R"({"window": 1000})")));
  CHECK_THROWS(parse_spectrum_style(json::parse(R"({"low_hz": 500, "high_hz": 100})")));
}
