#include "phantomcrash/music_spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

// An in-place radix-2 FFT; the size is a power of two.
void fft(std::vector<std::complex<float>> &values) {
  const std::size_t size = values.size();
  for (std::size_t index = 1, reversed = 0; index < size; ++index) {
    std::size_t bit = size >> 1U;
    for (; (reversed & bit) != 0; bit >>= 1U) {
      reversed ^= bit;
    }
    reversed ^= bit;
    if (index < reversed) {
      std::swap(values[index], values[reversed]);
    }
  }
  for (std::size_t length = 2; length <= size; length <<= 1U) {
    const double angle = -2.0 * std::numbers::pi / static_cast<double>(length);
    const std::complex<float> turn(static_cast<float>(std::cos(angle)),
                                   static_cast<float>(std::sin(angle)));
    for (std::size_t start = 0; start < size; start += length) {
      std::complex<float> twiddle(1.0F, 0.0F);
      for (std::size_t offset = 0; offset < length / 2; ++offset) {
        const auto even = values[start + offset];
        const auto odd = values[start + offset + length / 2] * twiddle;
        values[start + offset] = even + odd;
        values[start + offset + length / 2] = even - odd;
        twiddle *= turn;
      }
    }
  }
}

bool power_of_two(const int value) {
  return value > 0 && (value & (value - 1)) == 0;
}

} // namespace

SpectrumStyle parse_spectrum_style(const json::JsonValue &value) {
  SpectrumStyle style;
  const auto number = [&](const char *key, const auto fallback) {
    return static_cast<decltype(fallback)>(value.number_or(key, fallback));
  };
  style.window = number("window", style.window);
  if (!power_of_two(style.window) || style.window < 64) {
    throw std::runtime_error("window must be a power of two, at least 64");
  }
  style.low_hz = number("low_hz", style.low_hz);
  style.high_hz = number("high_hz", style.high_hz);
  if (style.low_hz <= 0.0F || style.high_hz <= style.low_hz) {
    throw std::runtime_error("low_hz and high_hz must rise from above 0");
  }
  style.memory_seconds = std::max(number("memory_seconds", style.memory_seconds), 0.1);
  style.centre = std::clamp(number("centre", style.centre), 0.0F, 1.0F);
  style.reach = std::max(number("reach", style.reach), 0.1F);
  style.min_spread_db = std::max(number("min_spread_db", style.min_spread_db), 0.1F);
  style.gate_db = number("gate_db", style.gate_db);
  return style;
}

std::vector<float> band_decibels(const std::vector<float> &samples, const int rate, const int bands,
                                 const float low_hz, const float high_hz) {
  std::vector<float> decibels(static_cast<std::size_t>(std::max(bands, 0)), -200.0F);
  const auto size = static_cast<int>(samples.size());
  if (bands <= 0 || !power_of_two(size) || size < 2 || rate <= 0) {
    return decibels;
  }
  std::vector<std::complex<float>> values(samples.size());
  double window_sum = 0.0;
  for (int index = 0; index < size; ++index) {
    const double window =
        0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * index / static_cast<double>(size - 1));
    window_sum += window;
    values[static_cast<std::size_t>(index)] = static_cast<float>(window) * samples[index];
  }
  if (window_sum <= 0.0) {
    return decibels;
  }
  fft(values);
  // Scaled so a full-scale sine's peak bin has a power of 1.
  const double scale = 4.0 / (window_sum * window_sum);
  const double bin_hz = static_cast<double>(rate) / size;
  const int last_bin = size / 2;
  for (int band = 0; band < bands; ++band) {
    const auto edge = [&](const int at) {
      return low_hz *
             std::pow(static_cast<double>(high_hz) / low_hz, static_cast<double>(at) / bands);
    };
    const int first = std::min(static_cast<int>(std::ceil(edge(band) / bin_hz)), last_bin);
    const int end =
        std::clamp(static_cast<int>(std::ceil(edge(band + 1) / bin_hz)), first + 1, last_bin + 1);
    double power = 0.0;
    for (int bin = first; bin < end; ++bin) {
      power += std::norm(values[static_cast<std::size_t>(bin)]);
    }
    decibels[static_cast<std::size_t>(band)] =
        static_cast<float>(10.0 * std::log10(power * scale + 1e-20));
  }
  return decibels;
}

MusicSpectrum::MusicSpectrum(SpectrumStyle style, const int bars)
    : style_(std::move(style)), bands_(static_cast<std::size_t>(std::max(bars, 0))),
      levels_(bands_.size(), 0.0F) {}

void MusicSpectrum::hear(const std::vector<float> &samples, const int rate,
                         const double tick_seconds) {
  const auto decibels =
      band_decibels(samples, rate, static_cast<int>(bands_.size()), style_.low_hz, style_.high_hz);
  for (std::size_t index = 0; index < bands_.size(); ++index) {
    Band &band = bands_[index];
    const double heard = decibels[index];
    if (heard <= style_.gate_db) {
      levels_[index] = 0.0F;
      continue;
    }
    if (band.seconds == 0.0) {
      // The first sound: a wide spread, so the opening is not all jumps.
      band.mean = heard;
      band.variance = 36.0;
    }
    // Measured against the past, then added to it.
    const double spread = std::max(std::sqrt(band.variance), double{style_.min_spread_db});
    const double above = (heard - band.mean) / spread;
    levels_[index] = static_cast<float>(
        std::clamp(style_.centre + (1.0 - style_.centre) * above / style_.reach, 0.0, 1.0));
    if (tick_seconds <= 0.0) {
      continue;
    }
    // A running average over memory_seconds, or all of it while shorter.
    band.seconds = std::min(band.seconds + tick_seconds, style_.memory_seconds);
    const double weight = tick_seconds / band.seconds;
    const double difference = heard - band.mean;
    band.mean += weight * difference;
    band.variance = (1.0 - weight) * (band.variance + weight * difference * difference);
  }
}

void MusicSpectrum::silence() {
  std::ranges::fill(levels_, 0.0F);
}

} // namespace phantomcrash
