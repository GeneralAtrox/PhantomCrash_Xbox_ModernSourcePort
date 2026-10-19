// The group flashes: flip-books of models or sprites started with an effect
// group (0x0659e5) and changed each frame by their update (table 0x215ec8).
#include "phantomcrash/effect_system.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phantomcrash {

namespace {

// fade (0x0666be): opaque, then 17 less alpha a frame over the last 15.
constexpr int fade_frames = 15;
constexpr int fade_step = 17;
// rise (0x066770): each frame it climbs and drifts in its own space, and
// its space grows by age / (2 × frames) + 1; its alpha falls from 0x70.
constexpr float rise_drift = 0.06F;
constexpr float rise_climb = 0.12F;
constexpr float rise_alpha = 112.0F;
// pulse (0x0668ab): the scale swings on a phase shared by every pulse, and
// the alpha's top bit flips each frame.
constexpr float pulse_swing = 0.5F;
constexpr float pulse_phase_step = 0.2F;
constexpr std::uint8_t pulse_flicker = 0x80;

} // namespace

bool EffectSystem::start_flashes(const int group, const Vec3 position, const float scale,
                                 const int instance) {
  bool any = false;
  for (const int id : bank_->groups[static_cast<std::size_t>(group)].flashes) {
    if (id < 0 || id >= static_cast<int>(bank_->flashes.size()) ||
        flashes_.size() >= static_cast<std::size_t>(flash_slots)) {
      continue;
    }
    EffectFlashState &flash = flashes_.emplace_back();
    flash.flash = id;
    flash.instance = instance;
    flash.group = group;
    flash.position = flash.previous = position;
    flash.scale = scale;
    update(flash);
    any = true;
  }
  return any;
}

void EffectSystem::step_flashes() {
  for (EffectFlashState &flash : flashes_) {
    const EffectFlash &data = bank_->flashes[static_cast<std::size_t>(flash.flash)];
    flash.previous = flash.position;
    flash.age += data.step;
    if (flash.age < static_cast<int>(data.frames.size())) {
      update(flash);
    }
  }
  std::erase_if(flashes_, [&](const EffectFlashState &flash) {
    return flash.age >=
           static_cast<int>(bank_->flashes[static_cast<std::size_t>(flash.flash)].frames.size());
  });
}

void EffectSystem::update(EffectFlashState &flash) {
  const EffectFlash &data = bank_->flashes[static_cast<std::size_t>(flash.flash)];
  const int frames = static_cast<int>(data.frames.size());
  switch (data.update) {
  case FlashUpdate::fade: {
    const int fading = flash.age - (frames - fade_frames);
    flash.alpha =
        static_cast<std::uint8_t>(fading > 0 ? std::max(255 - fading * fade_step, 0) : 255);
    break;
  }
  case FlashUpdate::rise: {
    const Vec3 drift{random(-rise_drift, rise_drift), rise_climb, random(-rise_drift, rise_drift)};
    flash.position = flash.position + drift * flash.grow;
    const float lived = static_cast<float>(flash.age) / static_cast<float>(std::max(frames, 1));
    flash.grow *= lived * 0.5F + 1.0F;
    flash.alpha = static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(rise_alpha) - static_cast<int>(lived * rise_alpha), 0, 255));
    break;
  }
  case FlashUpdate::pulse:
    flash.scale = std::sin(pulse_phase_) * pulse_swing + 1.0F;
    flash.alpha ^= pulse_flicker;
    pulse_phase_ += pulse_phase_step;
    if (pulse_phase_ > std::numbers::pi_v<float>) {
      pulse_phase_ -= std::numbers::pi_v<float>;
    }
    break;
  case FlashUpdate::still:
    break;
  }
}

} // namespace phantomcrash
