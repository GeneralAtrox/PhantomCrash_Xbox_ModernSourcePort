#include "movie_player.hpp"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include <algorithm>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

// MPEG-1 (the original movies) is BT.601; HD replacements are usually BT.709.
SDL_Colorspace colorspace_of(const AVFrame &frame) {
  const bool full = frame.color_range == AVCOL_RANGE_JPEG;
  if (frame.colorspace == AVCOL_SPC_BT709 ||
      (frame.colorspace == AVCOL_SPC_UNSPECIFIED && frame.height > 576)) {
    return full ? SDL_COLORSPACE_BT709_FULL : SDL_COLORSPACE_BT709_LIMITED;
  }
  return full ? SDL_COLORSPACE_BT601_FULL : SDL_COLORSPACE_BT601_LIMITED;
}

} // namespace

MoviePlayer::MoviePlayer(SDL_Renderer *renderer) : renderer_(renderer) {}

MoviePlayer::~MoviePlayer() {
  stop();
}

void MoviePlayer::open(const std::filesystem::path &path) {
  stop();
  try {
    decoder_.open(path);
    if (decoder_.has_audio()) {
      audio_.open();
      audio_.set_gain(volume_);
    }
    // Decode ahead and show the first frame now, so starting is instant.
    pump();
    if (decoder_.has_frame()) {
      show(decoder_.front());
      decoder_.pop_front();
      pump();
    }
  } catch (...) {
    stop();
    throw;
  }
}

void MoviePlayer::stop() noexcept {
  audio_.stop();
  decoder_.close();
  if (texture_ != nullptr) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
  wall_seconds_ = 0.0;
  shown_seconds_ = -1.0;
  started_ = paused_ = audio_finished_ = has_picture_ = finished_ = false;
}

void MoviePlayer::set_paused(const bool paused) {
  if (paused == paused_) {
    return;
  }
  paused_ = paused;
  if (started_) {
    if (paused) {
      audio_.pause();
    } else {
      audio_.resume();
    }
  }
}

void MoviePlayer::set_volume(const float volume) {
  volume_ = std::clamp(volume, 0.0F, 1.0F);
  audio_.set_gain(volume_);
}

double MoviePlayer::clock() const noexcept {
  return decoder_.has_audio() ? audio_.clock_seconds() : wall_seconds_;
}

void MoviePlayer::pump() {
  decoder_.pump([this](const float *samples, int frames, double seconds) {
    audio_.queue(samples, frames, seconds);
  });
  if (decoder_.has_audio() && decoder_.audio_drained() && !audio_finished_) {
    audio_.finish();
    audio_finished_ = true;
  }
}

void MoviePlayer::show(const AVFrame &frame) {
  if (texture_ == nullptr) {
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_IYUV);
    SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,
                          SDL_TEXTUREACCESS_STREAMING);
    SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, frame.width);
    SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, frame.height);
    SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER,
                          colorspace_of(frame));
    texture_ = SDL_CreateTextureWithProperties(renderer_, properties);
    SDL_DestroyProperties(properties);
    if (texture_ == nullptr) {
      throw std::runtime_error(std::string("Unable to create a movie texture: ") + SDL_GetError());
    }
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
  }
  if (!SDL_UpdateYUVTexture(texture_, nullptr, frame.data[0], frame.linesize[0], frame.data[1],
                            frame.linesize[1], frame.data[2], frame.linesize[2])) {
    throw std::runtime_error(std::string("Unable to upload a movie frame: ") + SDL_GetError());
  }
  has_picture_ = true;
}

void MoviePlayer::update(const double elapsed_seconds) {
  if (!is_open() || finished_ || paused_) {
    return;
  }
  if (!started_) {
    started_ = true;
    audio_.resume();
  } else {
    wall_seconds_ += std::max(0.0, elapsed_seconds);
  }
  pump();
  audio_.update(decoder_.audio_drained(), wall_seconds_);
  // Show the newest frame that is due; older due frames are dropped.
  const double now = clock();
  bool due = false;
  while (decoder_.has_frame() && decoder_.front_seconds() <= now) {
    due = true;
    if (decoder_.next_seconds() > now) {
      break;
    }
    decoder_.pop_front();
    pump();
  }
  if (due) {
    shown_seconds_ = decoder_.front_seconds();
    show(decoder_.front());
    decoder_.pop_front();
    pump();
  }
  finished_ = movie::playback_complete(movie::CompletionState{
      .input_eof = decoder_.input_eof(),
      .video_decoder_drained = decoder_.video_drained(),
      .audio_decoder_drained = decoder_.audio_drained(),
      .staged_video_frame = decoder_.has_frame(),
      .audio_bytes_queued = audio_.queued_bytes(),
      .audio_device_buffer_drained = !decoder_.has_audio() || audio_.device_drained(),
      .playback_seconds = now,
      .authored_duration_seconds = decoder_.duration_seconds(),
  });
}

void MoviePlayer::draw(const Viewport &viewport) const {
  if (texture_ == nullptr || !has_picture_) {
    return;
  }
  float width = 0.0F;
  float height = 0.0F;
  SDL_GetTextureSize(texture_, &width, &height);
  // 640x480 movies map exactly onto the 4:3 canvas area.
  const float window_width = static_cast<float>(viewport.pixel_width);
  const float window_height = static_cast<float>(viewport.pixel_height);
  const float scale = std::min(window_width / width, window_height / height);
  const SDL_FRect area{(window_width - width * scale) * 0.5F,
                       (window_height - height * scale) * 0.5F, width * scale, height * scale};
  SDL_RenderTexture(renderer_, texture_, nullptr, &area);
}

} // namespace phantomcrash
