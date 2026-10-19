#include "phantomcrash/town_screen.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {

void parse_town_basics(const json::JsonValue &document, TownBasics &basics) {
  parse_menu_basics(document, basics);
  basics.scene = static_cast<int>(json::required(document, "scene").number());
  if (const auto *music = document.find("music_from")) {
    for (const auto &[from, track] : music->object()) {
      basics.music_from[from] = track.string();
    }
  }
  basics.frame = json::member(document, "frame", parse_frame_placement);
}

std::string town_music(const TownBasics &basics, const std::string &from) {
  const auto found = basics.music_from.find(from);
  return found != basics.music_from.end() ? found->second : basics.music;
}

void SoundQueue::play(const std::string &sound) {
  if (!sound.empty()) {
    sounds_.push_back(sound);
  }
}

std::vector<std::string> SoundQueue::take() {
  return std::exchange(sounds_, {});
}

PopupGrowth parse_popup_growth(const json::JsonValue &value) {
  PopupGrowth growth;
  growth.seconds = value.number_or("grow_seconds", growth.seconds);
  growth.from_scale = static_cast<float>(value.number_or("grow_from_scale", growth.from_scale));
  if (growth.seconds <= 0.0) {
    throw std::runtime_error("grow_seconds must be above 0");
  }
  return growth;
}

float GrowingPopup::growing() const noexcept {
  return open_ ? static_cast<float>(std::max(left_, 0.0) / growth_.seconds) : 0.0F;
}

std::array<float, 2> pop_open(const float t, const float widest, const float thin) {
  if (t <= 0.0F) {
    return {0.0F, 0.0F};
  }
  if (t < 0.5F) {
    return {t * 2.0F * widest, thin};
  }
  const float u = std::min((t - 0.5F) * 2.0F, 1.0F);
  return {widest - (widest - 1.0F) * u, thin + (1.0F - thin) * u};
}

float growing_after(const PopupGrowth &growth, const double seconds) {
  return growth.seconds > 0.0
             ? static_cast<float>(1.0 - std::clamp(seconds / growth.seconds, 0.0, 1.0))
             : 0.0F;
}

Sprite grown_panel(const Sprite &panel, const float growing, const float from_scale,
                   const std::optional<std::array<float, 2>> &from) {
  const float scale = 1.0F - growing * (1.0F - from_scale);
  Sprite out = panel;
  const float width = panel.rect[2] - panel.rect[0];
  const float height = panel.rect[3] - panel.rect[1];
  const auto size = panel.size.value_or(std::array<float, 2>{width, height});
  out.size = std::array<float, 2>{size[0] * scale, size[1] * scale};
  // The panel's centre moves from the item to where the panel rests.
  const std::array<float, 2> rest{panel.position[0] + size[0] * 0.5F,
                                  panel.position[1] + size[1] * 0.5F};
  const std::array<float, 2> start = from.value_or(rest);
  const std::array<float, 2> centre{rest[0] + (start[0] - rest[0]) * growing,
                                    rest[1] + (start[1] - rest[1]) * growing};
  out.position = {centre[0] - size[0] * scale * 0.5F, centre[1] - size[1] * scale * 0.5F};
  return out;
}

Sprite grown_panel(const Sprite &panel, const GrowingPopup &popup,
                   const std::array<float, 2> &from) {
  return grown_panel(panel, popup.growing(), popup.growth().from_scale, from);
}

} // namespace phantomcrash
