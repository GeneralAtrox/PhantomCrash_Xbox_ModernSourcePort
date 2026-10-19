#pragma once

#include "phantomcrash/movie_clock.hpp"

#include <SDL3/SDL.h>

namespace phantomcrash {

// A movie's soundtrack on its own SDL audio stream (after the Lodoss movie
// audio output). The stream opens paused so a movie can be prepared ahead of
// time; its playback position is the movie's master clock.
class MovieAudioOutput final {
public:
  MovieAudioOutput() = default;
  ~MovieAudioOutput();
  MovieAudioOutput(const MovieAudioOutput &) = delete;
  MovieAudioOutput &operator=(const MovieAudioOutput &) = delete;

  // 48 kHz interleaved stereo float, paused until resume().
  void open();
  void stop() noexcept;
  void resume();
  void pause();
  void set_gain(float gain);
  // Queues sample frames starting at media time `seconds`.
  void queue(const float *samples, int frames, double seconds);
  void finish();
  // Refreshes the clock; `wall_seconds` is time since playback started.
  void update(bool decoder_drained, double wall_seconds);

  [[nodiscard]] bool is_open() const noexcept {
    return stream_ != nullptr;
  }
  [[nodiscard]] double clock_seconds() const noexcept {
    return clock_seconds_;
  }
  [[nodiscard]] int queued_bytes() const noexcept {
    return queued_bytes_;
  }
  [[nodiscard]] bool device_drained() const noexcept {
    return device_drained_;
  }

private:
  SDL_AudioStream *stream_{};
  movie::AudioDrainTracker drain_{};
  movie::AudioMediaClockTracker clock_{};
  double device_buffer_seconds_{};
  int device_buffer_bytes_{};
  double queued_end_seconds_{};
  double clock_seconds_{};
  int queued_bytes_{};
  bool device_drained_{};
};

} // namespace phantomcrash
