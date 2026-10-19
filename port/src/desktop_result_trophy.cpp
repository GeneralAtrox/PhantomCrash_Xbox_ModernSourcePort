#include "desktop_draw_list.hpp"
#include "desktop_result_trophy.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/skeleton.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
using json::required;
namespace {
Vec3 vec3(const json::JsonValue &value, const char *what) {
  const auto items = json::numbers<3>(value, what);
  return {items[0], items[1], items[2]};
}

float number(const json::JsonValue &value, const char *key, const float fallback) {
  return static_cast<float>(value.number_or(key, fallback));
}

TrophyAward parse_award(const json::JsonValue &value) {
  TrophyAward award;
  award.model = required(value, "model").string();
  award.start = vec3(required(value, "start"), "start");
  award.end = vec3(required(value, "end"), "end");
  award.start_turn = vec3(required(value, "start_turn"), "start_turn");
  award.end_turn = vec3(required(value, "end_turn"), "end_turn");
  award.plate = parse_sprite(required(value, "plate"));
  return award;
}
} // namespace

TrophyStyle parse_trophy_style(const json::JsonValue &document) {
  try {
    TrophyStyle style;
    const json::JsonValue &camera = required(document, "camera");
    style.eye = vec3(required(camera, "eye"), "eye");
    style.target = vec3(required(camera, "target"), "target");
    style.fov_degrees = number(camera, "fov_degrees", style.fov_degrees);
    style.near_plane = number(camera, "near_plane", style.near_plane);
    style.far_plane = number(camera, "far_plane", style.far_plane);
    if (const auto *light = document.find("light_direction")) {
      style.light_direction = vec3(*light, "light_direction");
    }
    style.frame_rate = number(document, "frame_rate", style.frame_rate);
    style.hold_frames = number(document, "hold_frames", style.hold_frames);
    style.move_frames = number(document, "move_frames", style.move_frames);
    style.spin_per_frame = number(document, "spin_per_frame", style.spin_per_frame);
    style.plate_still_frames = number(document, "plate_still_frames", style.plate_still_frames);
    style.plate_speed = number(document, "plate_speed", style.plate_speed);
    style.plate_travel = number(document, "plate_travel", style.plate_travel);
    style.dim_alpha = number(document, "dim_alpha", 160.0F) / 255.0F;
    style.dim_fade_per_frame = number(document, "dim_fade_per_frame", style.dim_fade_per_frame);
    style.layer = static_cast<int>(document.number_or("layer", style.layer));
    style.dim = parse_sprite(required(document, "dim"));
    for (const json::JsonValue &pair : required(document, "contests").array()) {
      const auto items = json::numbers<2>(pair, "contests");
      style.contests.push_back({static_cast<int>(items[0]), static_cast<int>(items[1])});
    }
    for (const json::JsonValue &award : required(document, "awards").array()) {
      style.awards.push_back(parse_award(award));
    }
    return style;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("trophy: ") + error.what());
  }
}

int trophy_award(const TrophyStyle &style, const int contest) {
  const auto found = std::ranges::find(style.contests, contest,
                                       [](const std::array<int, 2> &pair) { return pair[0]; });
  if (found == style.contests.end() || (*found)[1] < 0 ||
      (*found)[1] >= static_cast<int>(style.awards.size())) {
    return -1;
  }
  return (*found)[1];
}

ResultTrophy::ResultTrophy(WorldRenderer &world, TrophyStyle style, const int award)
    : world_(world), style_(std::move(style)),
      award_(style_.awards.at(static_cast<std::size_t>(award))), frames_(style_.hold_frames) {
  model_ = world_.model(award_.model);
}

void ResultTrophy::tick(const double seconds) {
  const auto frames = static_cast<float>(seconds) * style_.frame_rate;
  frames_ -= frames;
  if (frames_ < 0.0F) {
    spin_ += style_.spin_per_frame * frames;
  }
}

// 0x096466: translated, then turned about z, y and x, then spun about its
// own y.
Mat4 ResultTrophy::placement() const {
  float share = 0.0F;
  if (frames_ < 0.0F) {
    share =
        style_.move_frames > 0.0F ? std::clamp(-frames_ / style_.move_frames, 0.0F, 1.0F) : 1.0F;
  }
  const Vec3 at = award_.start + (award_.end - award_.start) * share;
  const Vec3 turn = award_.start_turn + (award_.end_turn - award_.start_turn) * share;
  return rotation_y(spin_) * rotation_x(turn.x) * rotation_y(turn.y) * rotation_z(turn.z) *
         translation(at);
}

// The screen dims while the trophy comes on and brightens once it moves.
float ResultTrophy::dim_alpha() const {
  const float share =
      frames_ >= 0.0F ? 1.0F : std::clamp(1.0F + frames_ * style_.dim_fade_per_frame, 0.0F, 1.0F);
  return style_.dim_alpha * share;
}

// The plate slides in from the right, stands, and slides out to the left
// as the trophy moves.
float ResultTrophy::plate_offset() const {
  if (frames_ >= 0.0F) {
    return std::clamp((frames_ - style_.plate_still_frames) * style_.plate_speed, 0.0F,
                      style_.plate_travel);
  }
  return std::clamp(frames_ * style_.plate_speed, -style_.plate_travel, 0.0F);
}

void ResultTrophy::draw(DrawList &list, const Viewport &viewport) const {
  list.sprite(style_.dim, dim_alpha());
  if (model_ >= 0) {
    list.custom(style_.layer, [this, viewport] { draw_model(viewport); });
  }
  Sprite plate = award_.plate;
  plate.position[0] += plate_offset();
  list.sprite(plate);
}

void ResultTrophy::draw_model(const Viewport &viewport) const {
  if (viewport.pixel_width <= 0 || viewport.pixel_height <= 0) {
    return;
  }
  WorldFrame frame;
  frame.width = viewport.pixel_width;
  frame.height = viewport.pixel_height;
  frame.eye = style_.eye;
  frame.view = look_at(style_.eye, style_.target, {0.0F, 1.0F, 0.0F});
  frame.projection = perspective(radians(style_.fov_degrees),
                                 static_cast<float>(frame.width) / static_cast<float>(frame.height),
                                 style_.near_plane, style_.far_plane);
  frame.lighting.fog = false;
  frame.lighting.sun_direction = normalize(style_.light_direction);
  WorldItem item;
  item.model = model_;
  item.world = placement();
  frame.items.push_back(std::move(item));
  frame.transparent = true;
  world_.draw(frame);
}

} // namespace phantomcrash
