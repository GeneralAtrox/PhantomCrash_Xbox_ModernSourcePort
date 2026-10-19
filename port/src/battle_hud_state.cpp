#include "phantomcrash/battle_hud_state.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {

float channel(const std::uint32_t colour, const int shift) {
  return static_cast<float>((colour >> shift) & 0xff);
}

std::uint32_t frame_count(const double seconds) {
  return static_cast<std::uint32_t>(std::max(seconds, 0.0) * hud_frames_per_second);
}

} // namespace

std::uint32_t hud_flash(const std::uint32_t from, const std::uint32_t to, const double seconds,
                        const float period_frames) {
  const auto period = std::max(static_cast<std::uint32_t>(period_frames), 1U);
  const float half = static_cast<float>(period) * 0.5F;
  const auto m = static_cast<float>(frame_count(seconds) % period);
  const float t = (m < half ? m : half - m) / half;
  std::uint32_t out = 0;
  for (const int shift : {16, 8, 0}) {
    const float value = channel(from, shift) + (channel(to, shift) - channel(from, shift)) * t;
    out |= static_cast<std::uint32_t>(std::clamp(value, 0.0F, 255.0F)) << shift;
  }
  return out;
}

float hud_wave(const double seconds, const float period_frames) {
  const auto period = std::max(static_cast<std::uint32_t>(period_frames), 2U);
  const float half = static_cast<float>(period / 2);
  const auto m = static_cast<float>(frame_count(seconds) % period);
  return m < half ? m / half : 1.0F - (m - half) / half;
}

float lock_spread_left(const float frames, const float lock_frames) {
  if (lock_frames <= 0.0F || frames >= lock_frames) {
    return 0.0F;
  }
  float left = 1.0F;
  const int whole = static_cast<int>(frames);
  for (int k = 1; k <= whole; ++k) {
    left *= (lock_frames - static_cast<float>(k)) / lock_frames;
  }
  return std::max(left, 0.0F);
}

void ReticleTracker::update(const std::optional<std::array<float, 2>> target,
                            const float speed_per_frame, const float frames,
                            const std::array<float, 2> half_size) {
  auto goal = target.value_or(std::array<float, 2>{});
  for (std::size_t axis = 0; axis < offset_.size(); ++axis) {
    const float limit = std::max(half_size[axis], 0.0F);
    offset_[axis] = std::clamp(offset_[axis], -limit, limit);
    goal[axis] = std::clamp(goal[axis], -limit, limit);
  }
  const float dx = goal[0] - offset_[0];
  const float dy = goal[1] - offset_[1];
  const float distance = std::sqrt(dx * dx + dy * dy);
  const float step = speed_per_frame * frames;
  if (distance <= step || distance <= 0.0F) {
    offset_ = goal;
    return;
  }
  offset_[0] += dx * step / distance;
  offset_[1] += dy * step / distance;
}

void HudMessageLog::push(const int kind) {
  pending_.push_back(kind);
}

void HudMessageLog::update(const float frames) {
  const auto count = static_cast<std::size_t>(std::max(style_.slots, 1));
  slots_.resize(count);
  const auto used = [&] {
    return std::count_if(slots_.begin(), slots_.end(), [](const Slot &s) { return s.kind >= 0; });
  };
  // A new message enters at the bottom while there is room.
  while (!pending_.empty() && used() < static_cast<std::ptrdiff_t>(count)) {
    std::rotate(slots_.rbegin(), slots_.rbegin() + 1, slots_.rend());
    slots_.front() = {pending_.front(), style_.life_frames};
    pending_.pop_front();
    scroll_ += style_.push_scroll;
  }
  // A full log hurries its oldest settled messages out.
  const float settled = style_.life_frames - style_.fade_frames;
  for (std::size_t i = 0; i < pending_.size() && i < count; ++i) {
    Slot &slot = slots_[count - 1 - i];
    if (slot.kind < 0) {
      continue;
    }
    if (slot.life > settled) {
      break;
    }
    slot.life = std::min(slot.life, style_.fade_frames);
  }
  if (scroll_ > 0.0F) {
    const float rise = std::max(scroll_ * style_.scroll_decay * frames, style_.min_scroll);
    scroll_ = std::max(scroll_ - rise, 0.0F);
  }
  for (Slot &slot : slots_) {
    if (slot.kind >= 0) {
      slot.life -= frames;
      if (slot.life <= 0.0F) {
        slot = {};
      }
    }
  }
}

std::vector<HudMessageLog::Entry> HudMessageLog::entries() const {
  std::vector<Entry> out;
  const float settled = style_.life_frames - style_.fade_frames;
  for (std::size_t i = 0; i < slots_.size(); ++i) {
    const Slot &slot = slots_[i];
    if (slot.kind < 0) {
      continue;
    }
    Entry entry{slot.kind, static_cast<float>(i) * style_.step + scroll_, 1.0F};
    if (slot.life > settled) {
      const float d = (slot.life - settled) / style_.fade_frames;
      entry.offset += (style_.enter - entry.offset) * d;
      entry.alpha = 1.0F - d;
    } else if (slot.life < style_.fade_frames) {
      const float d = (style_.fade_frames - slot.life) / style_.fade_frames;
      entry.offset += (style_.leave - entry.offset) * d;
      entry.alpha = 1.0F - d;
    }
    out.push_back(entry);
  }
  return out;
}

} // namespace phantomcrash
