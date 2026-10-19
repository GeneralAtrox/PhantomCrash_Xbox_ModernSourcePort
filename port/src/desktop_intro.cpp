#include "desktop_intro.hpp"

#include "desktop_draw_list.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {

IntroScreen::IntroScreen(TextureCache &textures, const DataRoots &roots, IntroScript script,
                         const bool first_boot, const int pass)
    : textures_(textures), renderer_(textures.renderer()), roots_(roots),
      sequence_(std::move(script), first_boot, pass),
      current_(std::make_unique<MoviePlayer>(renderer_)),
      preloaded_(std::make_unique<MoviePlayer>(renderer_)) {
  enter_step();
}

void IntroScreen::set_movie_gain(const float gain) {
  current_->set_volume(gain);
  preloaded_->set_volume(gain);
}

void IntroScreen::show_subtitles(const GlyphFont &font, MovieSubtitles subtitles) {
  font_ = &font;
  subtitles_ = std::move(subtitles);
}

void IntroScreen::open_movie(MoviePlayer &player, const std::string &file) {
  const auto path = roots_.find_asset(file);
  if (!path) {
    SDL_Log("Intro movie %s not found; skipping it", file.c_str());
    player.stop();
    return;
  }
  try {
    player.open(*path);
  } catch (const std::exception &error) {
    SDL_Log("%s; skipping it", error.what());
    player.stop();
  }
}

// Called whenever the sequence starts a new step.
void IntroScreen::enter_step() {
  seen_generation_ = sequence_.generation();
  movie_failed_ = false;
  const auto file = sequence_.movie_file();
  if (file) {
    if (preloaded_file_ == file && preloaded_->is_open()) {
      std::swap(current_, preloaded_);
    } else {
      open_movie(*current_, *file);
    }
    movie_failed_ = !current_->is_open();
  } else {
    // An image step keeps the previous movie's last frame out of sight.
    current_->stop();
  }
  preloaded_->stop();
  preloaded_file_ = sequence_.next_movie_file();
  if (preloaded_file_) {
    open_movie(*preloaded_, *preloaded_file_);
  }
  if (const auto *step = sequence_.step(); step != nullptr) {
    for (const auto &layer : step->layers) {
      static_cast<void>(textures_.get(layer.image));
    }
  }
}

void IntroScreen::tick(const InputFrame &input, const double tick_seconds) {
  if (sequence_.done()) {
    return;
  }
  const bool movie_ended = movie_failed_ || current_->finished();
  if (const auto *step = sequence_.step();
      step != nullptr && step->loop && !movie_failed_ && current_->finished()) {
    // A looping movie starts again for as long as its step lasts.
    if (const auto file = sequence_.movie_file()) {
      open_movie(*current_, *file);
      movie_failed_ = !current_->is_open();
    }
  }
  sequence_.update(tick_seconds, input, movie_ended);
  if (sequence_.generation() != seen_generation_) {
    enter_step();
  } else if (!sequence_.movie_playing() && current_->started()) {
    // Skipped or ended: the sound stops at once, the last frame stays for
    // any end hold or fade.
    current_->set_paused(true);
  }
}

void IntroScreen::advance(const double elapsed_seconds) {
  if (sequence_.movie_playing() && current_->is_open()) {
    current_->update(elapsed_seconds);
  }
}

void IntroScreen::set_paused(const bool paused) {
  if (sequence_.movie_playing()) {
    current_->set_paused(paused);
  }
}

void IntroScreen::draw_image(const IntroStep &step, const Viewport &viewport) {
  const auto alpha = static_cast<Uint8>(sequence_.image_alpha() * 255.0F + 0.5F);
  for (const auto &layer : step.layers) {
    SDL_Texture *image = textures_.get(layer.image);
    if (image == nullptr) {
      continue;
    }
    const auto [left, top, right, bottom] = layer.rect;
    const float width = right - left;
    const float height = bottom - top;
    float x = layer.position[0];
    float y = layer.position[1];
    if (layer.centered) {
      x -= width * 0.5F;
      y -= height * 0.5F;
    }
    const SDL_FRect source{left, top, width, height};
    const auto pixels = viewport.to_pixels(x, y, width, height);
    const SDL_FRect target{pixels.x, pixels.y, pixels.width, pixels.height};
    SDL_SetTextureAlphaMod(image, alpha);
    SDL_RenderTexture(renderer_, image, &source, &target);
  }
}

// The line the movie has reached, centred (0x01c4fa: 320 less half the
// run's width, 0x0be443).
void IntroScreen::draw_subtitle(const Viewport &viewport) {
  if (font_ == nullptr || !subtitles_ || !current_->is_open()) {
    return;
  }
  const Subtitle *line = subtitle_at(*subtitles_, current_->playback_seconds());
  if (line == nullptr) {
    return;
  }
  const std::vector<int> run = glyph_run(*font_, line->text);
  const auto [x, y] = subtitles_->position;
  DrawList list(textures_, *font_, viewport, 0.0);
  list.text(subtitles_->layer, layout_run(*font_, run, x - run_width(*font_, run) * 0.5F, y),
            subtitles_->colour);
  list.flush();
}

std::optional<SceneProgress> IntroScreen::scene_progress() const {
  const IntroStep *step = sequence_.step();
  if (!current_ || !current_->is_open() || step == nullptr ||
      step->type != IntroStep::Type::movie || current_->frame_seconds() <= 0.0) {
    return std::nullopt;
  }
  const double frame = current_->frame_seconds();
  const int total = static_cast<int>(current_->duration_seconds() / frame + 0.5);
  const int shown = std::min(static_cast<int>(current_->playback_seconds() / frame), total);
  return SceneProgress{"Movie " + sequence_.movie_file().value_or(step->id), shown, total};
}

void IntroScreen::draw(const Viewport &viewport) {
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  const auto *step = sequence_.step();
  if (step == nullptr) {
    return;
  }
  if (step->type == IntroStep::Type::movie) {
    current_->draw(viewport);
    draw_subtitle(viewport);
  } else {
    draw_image(*step, viewport);
  }
  if (const float black = sequence_.black_alpha(); black > 0.0F) {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, static_cast<Uint8>(black * 255.0F + 0.5F));
    SDL_RenderFillRect(renderer_, nullptr);
  }
}

} // namespace phantomcrash
