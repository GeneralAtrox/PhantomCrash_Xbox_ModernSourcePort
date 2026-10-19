#pragma once

// Movie timing helpers (from the Record of Lodoss War port): the audio
// device is the master clock, and video frames are shown when that clock
// reaches their timestamps. Pure functions, so they can be unit-tested.

#include <algorithm>

namespace phantomcrash::movie {

// True once the decoder is drained, nothing is queued and the device has had
// one buffer's worth of time to play what it already held.
struct AudioDrainTracker {
  double queue_empty_since_seconds{-1.0};

  [[nodiscard]] constexpr bool update(const bool decoder_drained, const int bytes_queued,
                                      const double now_seconds,
                                      const double device_buffer_seconds) noexcept {
    if (!decoder_drained || bytes_queued != 0) {
      queue_empty_since_seconds = -1.0;
      return false;
    }
    if (queue_empty_since_seconds < 0.0) {
      queue_empty_since_seconds = now_seconds;
    }
    return now_seconds - queue_empty_since_seconds >= device_buffer_seconds;
  }
};

// Media time now audible: the end of what was queued, minus what is still
// queued and the device buffer.
[[nodiscard]] constexpr double audio_media_clock(const double queued_media_end_seconds,
                                                 const int queued_bytes,
                                                 const double bytes_per_second,
                                                 const double device_buffer_seconds) noexcept {
  if (queued_media_end_seconds <= 0.0 || bytes_per_second <= 0.0) {
    return 0.0;
  }
  const double stream_queue_seconds =
      static_cast<double>(std::max(0, queued_bytes)) / bytes_per_second;
  return std::max(0.0, queued_media_end_seconds - stream_queue_seconds -
                           std::max(0.0, device_buffer_seconds));
}

// audio_media_clock, but keeps advancing with wall time while the stream is
// nearly empty (end of the soundtrack, or a movie whose audio ends early), so
// the remaining video frames are still shown on time.
struct AudioMediaClockTracker {
  double starved_since_seconds{-1.0};
  double starved_base_media_seconds{};
  double starved_queued_end_seconds{};

  [[nodiscard]] constexpr double update(const double queued_media_end_seconds,
                                        const int queued_bytes, const double bytes_per_second,
                                        const double wall_seconds,
                                        const double device_buffer_seconds,
                                        const int device_buffer_bytes) noexcept {
    const double ordinary = audio_media_clock(queued_media_end_seconds, queued_bytes,
                                              bytes_per_second, device_buffer_seconds);
    if (queued_media_end_seconds <= 0.0 || bytes_per_second <= 0.0 || device_buffer_bytes <= 0 ||
        queued_bytes > device_buffer_bytes) {
      starved_since_seconds = -1.0;
      starved_base_media_seconds = 0.0;
      starved_queued_end_seconds = 0.0;
      return ordinary;
    }
    if (starved_since_seconds < 0.0 || queued_media_end_seconds != starved_queued_end_seconds) {
      starved_since_seconds = wall_seconds;
      starved_base_media_seconds = ordinary;
      starved_queued_end_seconds = queued_media_end_seconds;
    }
    return std::max(ordinary, starved_base_media_seconds +
                                  std::max(0.0, wall_seconds - starved_since_seconds));
  }
};

// A frame without a timestamp follows the previous one by one frame.
[[nodiscard]] constexpr double
decoded_video_frame_seconds(const bool timestamp_valid, const double timestamp_seconds,
                            const double previous_seconds, const double frame_duration_seconds,
                            const double playback_fallback_seconds) noexcept {
  if (timestamp_valid) {
    return timestamp_seconds;
  }
  if (previous_seconds >= 0.0 && frame_duration_seconds > 0.0) {
    return previous_seconds + frame_duration_seconds;
  }
  return playback_fallback_seconds;
}

struct CompletionState {
  bool input_eof{};
  bool video_decoder_drained{};
  bool audio_decoder_drained{};
  bool staged_video_frame{};
  int audio_bytes_queued{};
  bool audio_device_buffer_drained{};
  double playback_seconds{};
  double authored_duration_seconds{};
};

// A movie ends when every frame has been shown, all audio has played and the
// clock has reached the container's duration (when it states one).
[[nodiscard]] constexpr bool playback_complete(const CompletionState &state) noexcept {
  if (!state.input_eof || !state.video_decoder_drained || !state.audio_decoder_drained ||
      state.staged_video_frame || state.audio_bytes_queued != 0 ||
      !state.audio_device_buffer_drained) {
    return false;
  }
  return state.authored_duration_seconds <= 0.0 ||
         state.playback_seconds >= state.authored_duration_seconds;
}

} // namespace phantomcrash::movie
