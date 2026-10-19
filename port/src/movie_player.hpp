#pragma once

#include "movie_audio_output.hpp"
#include "movie_decoder.hpp"
#include "phantomcrash/canvas.hpp"

#include <SDL3/SDL.h>

#include <filesystem>

namespace phantomcrash {

// Plays one movie: decoded frames go to the GPU as YUV textures and are shown
// when the soundtrack's clock reaches them (after the Lodoss movie player).
// open() prepares the first frame with the sound paused, so the next movie
// can be ready before the current one ends; the first update() starts it.
class MoviePlayer final {
public:
  explicit MoviePlayer(SDL_Renderer *renderer);
  ~MoviePlayer();
  MoviePlayer(const MoviePlayer &) = delete;
  MoviePlayer &operator=(const MoviePlayer &) = delete;

  // Throws std::runtime_error when the movie cannot be opened.
  void open(const std::filesystem::path &path);
  void stop() noexcept;
  // Advances playback by real time; starts it on the first call.
  void update(double elapsed_seconds);
  // Pauses the sound (and the clock) while the game is paused.
  void set_paused(bool paused);
  void set_volume(float volume);
  // Draws the current frame as large as the window allows at its own aspect:
  // the original 4:3 movies fill the 640x480 area, wider ones use the width.
  void draw(const Viewport &viewport) const;

  [[nodiscard]] bool is_open() const noexcept {
    return decoder_.is_open();
  }
  [[nodiscard]] bool started() const noexcept {
    return started_;
  }
  [[nodiscard]] bool finished() const noexcept {
    return finished_;
  }
  [[nodiscard]] bool has_picture() const noexcept {
    return has_picture_;
  }
  [[nodiscard]] double playback_seconds() const noexcept {
    return wall_seconds_;
  }
  [[nodiscard]] double duration_seconds() const noexcept {
    return decoder_.duration_seconds();
  }
  // One video frame's time, or 0 when the stream does not say.
  [[nodiscard]] double frame_seconds() const noexcept {
    return decoder_.frame_duration_seconds();
  }

private:
  void pump();
  void show(const struct AVFrame &frame);
  [[nodiscard]] double clock() const noexcept;

  SDL_Renderer *renderer_{};
  SDL_Texture *texture_{};
  MovieDecoder decoder_;
  MovieAudioOutput audio_;
  double wall_seconds_{};
  double shown_seconds_{-1.0};
  float volume_{1.0F};
  bool started_{};
  bool paused_{};
  bool audio_finished_{};
  bool has_picture_{};
  bool finished_{};
};

} // namespace phantomcrash
