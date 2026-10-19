#include "movie_audio_output.hpp"

#include "movie_decoder.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

constexpr int bytes_per_frame = MovieDecoder::output_channels * static_cast<int>(sizeof(float));
constexpr double bytes_per_second =
    static_cast<double>(MovieDecoder::output_sample_rate) * bytes_per_frame;

[[noreturn]] void fail(const char *what) {
  throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
}

} // namespace

MovieAudioOutput::~MovieAudioOutput() {
  stop();
}

void MovieAudioOutput::open() {
  stop();
  const SDL_AudioSpec spec{SDL_AUDIO_F32, MovieDecoder::output_channels,
                           MovieDecoder::output_sample_rate};
  // Opened paused: the device starts when the movie does.
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (stream_ == nullptr) {
    fail("Unable to open movie audio");
  }
  SDL_AudioSpec device{};
  int device_frames = 0;
  const SDL_AudioDeviceID id = SDL_GetAudioStreamDevice(stream_);
  if (id != 0 && SDL_GetAudioDeviceFormat(id, &device, &device_frames) && device.freq > 0 &&
      device_frames > 0) {
    device_buffer_seconds_ = static_cast<double>(device_frames) / device.freq;
    device_buffer_bytes_ = device_frames * bytes_per_frame;
  }
}

void MovieAudioOutput::stop() noexcept {
  if (stream_ != nullptr) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }
  drain_ = {};
  clock_ = {};
  device_buffer_seconds_ = 0.0;
  device_buffer_bytes_ = 0;
  queued_end_seconds_ = clock_seconds_ = 0.0;
  queued_bytes_ = 0;
  device_drained_ = false;
}

void MovieAudioOutput::resume() {
  if (stream_ != nullptr && !SDL_ResumeAudioStreamDevice(stream_)) {
    fail("Unable to start movie audio");
  }
}

void MovieAudioOutput::pause() {
  // Each device stream has its own logical device, so this pauses only it.
  if (stream_ != nullptr) {
    SDL_PauseAudioStreamDevice(stream_);
  }
}

void MovieAudioOutput::set_gain(const float gain) {
  if (stream_ != nullptr) {
    SDL_SetAudioStreamGain(stream_, gain);
  }
}

void MovieAudioOutput::queue(const float *samples, const int frames, const double seconds) {
  if (stream_ == nullptr || frames <= 0) {
    return;
  }
  queued_end_seconds_ =
      std::max(queued_end_seconds_,
               seconds + static_cast<double>(frames) / MovieDecoder::output_sample_rate);
  if (!SDL_PutAudioStreamData(stream_, samples, frames * bytes_per_frame)) {
    fail("Unable to queue movie audio");
  }
}

void MovieAudioOutput::finish() {
  if (stream_ != nullptr) {
    SDL_FlushAudioStream(stream_);
  }
}

void MovieAudioOutput::update(const bool decoder_drained, const double wall_seconds) {
  if (stream_ == nullptr) {
    return;
  }
  queued_bytes_ = std::max(0, SDL_GetAudioStreamQueued(stream_));
  device_drained_ =
      drain_.update(decoder_drained, queued_bytes_, wall_seconds, device_buffer_seconds_);
  clock_seconds_ = std::max(
      clock_seconds_, clock_.update(queued_end_seconds_, queued_bytes_, bytes_per_second,
                                    wall_seconds, device_buffer_seconds_, device_buffer_bytes_));
}

} // namespace phantomcrash
