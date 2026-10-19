#include "desktop_attract_demo.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_world_renderer.hpp"
#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
using json::required;

namespace {
// Phases end within this of their length, as the front end's do.
constexpr double time_epsilon = 1e-9;

std::vector<int> integers(const json::JsonValue &array) {
  std::vector<int> values;
  for (const json::JsonValue &value : array.array()) {
    values.push_back(static_cast<int>(value.number()));
  }
  return values;
}
} // namespace

AttractDemoScript parse_attract_demo(const json::JsonValue &document) {
  AttractDemoScript script;
  script.seconds = required(document, "seconds").number();
  script.fade_in = required(document, "fade_in").number();
  script.fade_out = required(document, "fade_out").number();
  script.music = required(document, "music").string();
  script.contests = integers(required(document, "contests"));
  if (script.contests.empty()) {
    throw std::runtime_error("attract_demo: contests is empty");
  }
  script.first_stage = static_cast<int>(required(document, "first_stage").number());
  script.next_stage = integers(required(document, "next_stage"));
  script.timeout_goto = required(document, "timeout_goto").string();
  const json::JsonValue &skip = required(document, "skip");
  script.skip_buttons = buttons_from_json(required(skip, "buttons"));
  script.skip_goto = required(skip, "goto").string();
  script.prompt = parse_sprite(required(document, "prompt"));
  return script;
}

AttractStage attract_stage(const int index, const int count) {
  const int stage = index >= 0 && index < count ? index : 0;
  return {stage / 2, stage % 2 != 0};
}

AttractDemoScreen::AttractDemoScreen(TextureCache &textures, WorldRenderer *world,
                                     AttractDemoScript script,
                                     std::unique_ptr<BackdropBattle> battle)
    : textures_(textures), world_(world), script_(std::move(script)),
      backdrop_(nullptr, WorldFrame{}) {
  backdrop_.adopt(std::move(battle));
  static_cast<void>(textures_.get(script_.prompt.image));
}

void AttractDemoScreen::tick(const InputFrame &input, const double seconds) {
  if (done_) {
    return;
  }
  seconds_ += seconds;
  phase_time_ += seconds;
  backdrop_.tick(seconds);
  switch (phase_) {
  case Phase::fade_in:
    // 0x013473 takes no input until the fade is over.
    if (phase_time_ + time_epsilon >= script_.fade_in) {
      phase_ = Phase::running;
      phase_time_ = 0.0;
    }
    return;
  case Phase::running:
    if (input.any_pressed(script_.skip_buttons)) {
      next_ = script_.skip_goto; // fade(0): at once
      done_ = true;
    } else if (phase_time_ + time_epsilon >= script_.seconds) {
      phase_ = Phase::fade_out;
      phase_time_ = 0.0;
    }
    return;
  case Phase::fade_out:
    if (phase_time_ + time_epsilon >= script_.fade_out) {
      next_ = script_.timeout_goto;
      done_ = true;
    }
    return;
  }
}

float AttractDemoScreen::black() const {
  const auto share = [this](const double length) {
    return length > 0.0 ? static_cast<float>(std::clamp(phase_time_ / length, 0.0, 1.0)) : 1.0F;
  };
  switch (phase_) {
  case Phase::fade_in:
    return 1.0F - share(script_.fade_in);
  case Phase::fade_out:
    return share(script_.fade_out);
  case Phase::running:
    break;
  }
  return 0.0F;
}

void AttractDemoScreen::draw(const Viewport &viewport) {
  clear_black(textures_);
  if (world_ != nullptr && world_->available()) {
    backdrop_.draw(*world_, viewport);
  }
  draw_sprite(textures_, script_.prompt, viewport, seconds_);
  if (const float alpha = black(); alpha > 0.0F) {
    SDL_Renderer *renderer = textures_.renderer();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(alpha * 255.0F + 0.5F));
    SDL_RenderFillRect(renderer, nullptr);
  }
}

} // namespace phantomcrash
