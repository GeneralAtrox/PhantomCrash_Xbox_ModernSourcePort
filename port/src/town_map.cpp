#include "phantomcrash/town_map.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phantomcrash {
namespace {

constexpr double turn = 2.0 * std::numbers::pi;
constexpr float stick_pushed = 0.5F; // right thumbstick deflection that turns the rider

std::array<float, 2> size_of(const Sprite &sprite) {
  return {sprite.rect[2] - sprite.rect[0], sprite.rect[3] - sprite.rect[1]};
}

float lerp(const float from, const float to, const float amount) {
  return from * (1.0F - amount) + to * amount;
}

bool inside(const std::array<float, 4> &box, const std::array<float, 2> point) {
  return point[0] >= box[0] && point[0] < box[2] && point[1] >= box[1] && point[1] < box[3];
}

} // namespace

TownMap::TownMap(TownMapScript script, const std::string &from, QuestSave quest)
    : script_(std::move(script)), quest_(std::move(quest)),
      fade_(script_.fade_in, script_.fade_out),
      repeat_(script_.repeat_delay, script_.repeat_interval) {
  if (const auto start = script_.start_places.find(from); start != script_.start_places.end()) {
    const auto found = std::ranges::find_if(
        script_.places, [&](const TownPlace &place) { return place.id == start->second; });
    if (found != script_.places.end()) {
      place_ = static_cast<int>(found - script_.places.begin());
    }
  }
  previous_ = place_;
  // Turned to the start, forward's first step goes to whichever neighbour
  // stands further left on screen (by its rider spot), and the buttons keep
  // that way round until the map closes.
  const int count = static_cast<int>(script_.places.size());
  if (script_.orient_to_start && count > 2) {
    const auto x = [&](const int step) {
      return script_.places[static_cast<std::size_t>(move_cursor(place_, step, true, count))]
          .rider[0];
    };
    turn_ = x(-1) < x(1) ? -1 : 1;
  }
}

void TownMap::sound(const std::string &name) {
  if (!name.empty()) {
    sounds_.push_back(name);
  }
}

float TownMap::progress(const std::optional<double> since) const {
  if (!since) {
    return 0.0F;
  }
  return static_cast<float>(std::clamp((seconds_ - *since) / script_.pop_seconds, 0.0, 1.0));
}

bool TownMap::moving() const noexcept {
  return moved_ && seconds_ - *moved_ + 1e-9 < script_.pop_seconds;
}

void TownMap::move_to(const int next, const bool wrapped) {
  sound(script_.move_sound);
  sound(script_.place_sound);
  wrapped_ = wrapped;
  previous_ = place_;
  place_ = next;
  moved_ = seconds_;
  bob_from_.reset();
  help_seconds_ = 0.0;
}

// A forward step (+1) goes to the next number and a back step to the one
// before, or the other way round for the whole visit when the map was
// opened turned (see the constructor).
int TownMap::stepped_place(const int step) const {
  return move_cursor(place_, step * turn_, true, static_cast<int>(script_.places.size()));
}

void TownMap::update(const double tick_seconds, const InputFrame &input) {
  seconds_ += tick_seconds;
  help_seconds_ += tick_seconds;
  const bool ready = fade_.update(tick_seconds);
  if (!shown_ && !fade_.fading_in()) {
    shown_ = seconds_;
  }
  // The rider hovers again once he has arrived, on the clock the map's fade
  // in started (0x3eff2 sets +0x40 back to +0x3c).
  if (!bob_from_ && !moving()) {
    bob_from_ = shown_.value_or(0.0);
    previous_ = place_;
  }
  if (!ready || fade_.leaving()) {
    return;
  }
  // The cursor counts down the places (0x6d1cd on 7 - place, flags 0x1f:
  // sideways, always wrapping), or follows the screen, and waits while the
  // rider travels. A place the mouse moved onto is taken once he has arrived.
  bool fresh = false;
  const int step =
      repeat_.step(tick_seconds, input, script_.back_buttons, script_.forward_buttons, fresh);
  follow_mouse(input, step);
  if (step != 0 && !moving()) {
    const int next = stepped_place(step);
    if (next != place_) {
      move_to(next, std::abs(next - place_) >= 2);
    }
  } else if (hovered_ && *hovered_ != place_ && !moving()) {
    move_to(*hovered_, false);
  }
  bool go = input.any_pressed(script_.confirm_buttons);
  if (input.clicked && input.pointer) {
    if (const auto target = place_at(*input.pointer, true)) {
      if (*target != place_) {
        move_to(*target, false);
      }
      go = true;
    }
  }
  if (go) {
    sound(script_.select_sound);
    leaving_ = seconds_;
    fade_.leave(script_.places[static_cast<std::size_t>(place_)].next);
  }
}

// The mouse is in use from when it moves until it leaves the window or the
// cursor keys move the map on. The right thumbstick, while pushed, turns the
// rider to point its way, as on the original.
void TownMap::follow_mouse(const InputFrame &input, const int step) {
  if (!input.pointer || step != 0) {
    mouse_ = false;
    hovered_.reset();
  } else if (input.pointer_moved) {
    mouse_ = true;
    hovered_ = place_at(*input.pointer, false);
  }
  const auto &stick = input.right_stick;
  if (std::hypot(stick[0], stick[1]) > stick_pushed) {
    mouse_ = false;
    // Match the rider's horizontal rotation; holding down keeps him upright.
    turn_to(std::atan2(-stick[0], -stick[1]));
  } else if (mouse_ && input.pointer) {
    spin(*input.pointer);
  } else {
    // As on letting the thumbstick go, the rider stands up straight again.
    facing_.reset();
    turned_ = 0.0;
  }
}

// 0x3eff2: the rider turns with the thumbstick about a point a quarter of
// his height above his middle, and the turns add up; each full one pays
// while the game has been paid less than the limit (the quest's + 0x113c8),
// with battle sound 19 when the money grew. The port turns him to face the
// mouse instead.
void TownMap::spin(const std::array<float, 2> pointer) {
  const auto rider = unturned_rider();
  if (!rider || !rider->size) {
    return;
  }
  const float dx = pointer[0] - rider->position[0];
  const float dy = pointer[1] - (rider->position[1] - (*rider->size)[1] * 0.25F);
  if (dx * dx + dy * dy < 1.0F) {
    return; // on the pivot: no way to face
  }
  turn_to(std::atan2(dx, -dy));
}

// Faces the rider `angle` radians clockwise from straight up, adding up his
// turns and paying for each full one.
void TownMap::turn_to(const double angle) {
  if (facing_) {
    double delta = angle - *facing_;
    if (delta > std::numbers::pi) {
      delta -= turn;
    } else if (delta < -std::numbers::pi) {
      delta += turn;
    }
    turned_ += delta;
  }
  facing_ = angle;
  if (std::abs(turned_) > turn && quest_.spin_paid < script_.spin_limit) {
    quest_.spin_paid += script_.spin_pay;
    quest_changed_ = true;
    turned_ = 0.0;
    const std::int64_t money = std::min(quest_.money + script_.spin_pay, money_limit);
    if (money != quest_.money) {
      quest_.money = money;
      sound(script_.spin_sound);
    }
  }
}

// The place under the mouse: the chosen place on its sign, which is drawn
// over the map, or the place whose number (or, with `names`, name) it is
// on. Moving the mouse picks by number alone: the riders stand clear of the
// other numbers but by other names, and circling him must not move him.
std::optional<int> TownMap::place_at(const std::array<float, 2> point, const bool names) const {
  if (const auto shown = sign(); shown && shown->size) {
    const auto [width, height] = *shown->size;
    const std::array<float, 4> box{
        shown->position[0] - width * 0.5F, shown->position[1] - height * 0.5F,
        shown->position[0] + width * 0.5F, shown->position[1] + height * 0.5F};
    if (inside(box, point)) {
      return place_;
    }
  }
  for (std::size_t index = 0; index < script_.places.size(); ++index) {
    const TownPlace &place = script_.places[index];
    const auto [width, height] = size_of(place.badge);
    const std::array<float, 4> badge{place.badge.position[0], place.badge.position[1],
                                     place.badge.position[0] + width,
                                     place.badge.position[1] + height};
    if (inside(names ? place.hit : badge, point)) {
      return static_cast<int>(index);
    }
  }
  return std::nullopt;
}

// 0x3edd2: the sign is centred and scaled by its growth, its centre moving
// from `sign_from` to the middle of its place.
std::optional<Sprite> TownMap::sign() const {
  const float grown = progress(moved_ ? moved_ : shown_);
  if (grown <= 0.0F) {
    return std::nullopt;
  }
  const TownPlace &place = script_.places[static_cast<std::size_t>(place_)];
  Sprite sign = place.sign;
  const auto [width, height] = size_of(sign);
  sign.centered = true;
  sign.size = std::array<float, 2>{width * grown, height * grown};
  sign.position = {sign.position[0] + lerp(place.sign_from[0], width * 0.5F, grown),
                   sign.position[1] + lerp(place.sign_from[1], height * 0.5F, grown)};
  return sign;
}

// 0x3eadc: the rider travels between the places' spots, hovers (whole units,
// as the original truncates) while at rest, and grows out of and back into
// the chosen place's number as he appears and goes. He faces the way the
// place he comes from says, the other way after a move round past the ends.
std::optional<Sprite> TownMap::unturned_rider() const {
  float scale = progress(shown_);
  if (leaving_) {
    scale = 1.0F - progress(leaving_);
  }
  if (scale <= 0.0F) {
    return std::nullopt;
  }
  const TownPlace &from = script_.places[static_cast<std::size_t>(previous_)];
  const TownPlace &to = script_.places[static_cast<std::size_t>(place_)];
  const float travelled = moved_ ? progress(moved_) : 1.0F;
  Sprite rider = script_.rider;
  const auto [width, height] = size_of(rider);
  float x = lerp(from.rider[0], to.rider[0], travelled) + width * 0.5F;
  float y = lerp(from.rider[1], to.rider[1], travelled) + height * 0.5F;
  if (bob_from_) {
    const double phase = turn * (seconds_ - *bob_from_);
    x += std::trunc(static_cast<float>(std::sin(phase / script_.bob_x_seconds)) * script_.bob);
    y += std::trunc(static_cast<float>(std::sin(phase / script_.bob_y_seconds)) * script_.bob);
  }
  const auto [badge_width, badge_height] = size_of(to.badge);
  const float badge_x = to.badge.position[0] + std::trunc(badge_width * 0.5F);
  const float badge_y = to.badge.position[1] + std::trunc(badge_height * 0.5F);
  rider.centered = true;
  rider.position = {lerp(badge_x, x, scale), lerp(badge_y, y, scale)};
  rider.size = std::array<float, 2>{width * scale, height * scale};
  rider.flip_x = (moving() ? from.rider_flipped : to.rider_flipped) != wrapped_;
  return rider;
}

// The rider as drawn: turned to face the mouse while it is in use, about
// the point a quarter of his height above his middle (0x3eadc's pivot).
std::optional<Sprite> TownMap::rider() const {
  auto rider = unturned_rider();
  if (rider && rider->size && facing_) {
    const auto [width, height] = *rider->size;
    rider->angle = static_cast<float>(*facing_ * 180.0 / std::numbers::pi);
    rider->pivot = std::array<float, 2>{width * 0.5F, height * 0.25F};
  }
  return rider;
}

} // namespace phantomcrash
