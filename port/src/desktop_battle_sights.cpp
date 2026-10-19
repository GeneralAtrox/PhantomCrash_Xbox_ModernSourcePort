#include "desktop_battle_sights.hpp"

#include "desktop_battle_view.hpp"
#include "desktop_text.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace phantomcrash {
namespace {

constexpr float quarter_turn = 90.0F;
constexpr float half_turn = 180.0F;
constexpr float degrees_per_radian = 180.0F / pi;
constexpr std::uint32_t white = 0xffffff;

// The pulsing warning colour: red 0xc8 with green on the 2D renderer's wave.
std::uint32_t pulse_colour(const std::uint32_t red, const double clock) {
  const auto green = static_cast<std::uint32_t>(animation_alpha(Animation::fade, clock) * 255.0F);
  return (red & 0xff0000) | (std::min<std::uint32_t>(green, 0xff) << 8);
}

} // namespace

std::optional<BattleSights::CanvasPoint> BattleSights::View::project(const Vec3 point) const {
  ScreenPoint at;
  if (!project_point(view_projection, point, viewport.pixel_width, viewport.pixel_height, at)) {
    return std::nullopt;
  }
  return CanvasPoint{(viewport.origin_x + at.x - viewport.offset_x) / viewport.scale,
                     (viewport.origin_y + at.y - viewport.offset_y) / viewport.scale};
}

bool BattleSights::View::on_screen(const Vec3 point) const {
  ScreenPoint at;
  return project_point(view_projection, point, viewport.pixel_width, viewport.pixel_height, at) &&
         at.x >= 0.0F && at.y >= 0.0F && at.x < static_cast<float>(viewport.pixel_width) &&
         at.y < static_cast<float>(viewport.pixel_height);
}

BattleSights::BattleSights(TextureCache &textures, const GlyphFont &font, BattleHudStyle style)
    : textures_(textures), font_(font), style_(std::move(style)) {}

void BattleSights::put(const Viewport &viewport, const HudPiece &piece, const float x,
                       const float y, const bool centered, const float angle, const bool flip,
                       const std::optional<std::uint32_t> colour, const float alpha,
                       const float width_scale) const {
  Sprite sprite = piece.sprite;
  sprite.position = {x, y};
  sprite.centered = centered;
  sprite.angle = angle;
  sprite.flip_x = flip;
  sprite.colour = colour.value_or(piece.sprite.colour);
  sprite.size = std::array<float, 2>{(sprite.rect[2] - sprite.rect[0]) * width_scale * pieces_,
                                     (sprite.rect[3] - sprite.rect[1]) * pieces_};
  draw_sprite(textures_, sprite, viewport, 0.0, piece.alpha * alpha);
}

void BattleSights::set_screen(const std::array<float, 2> canvas, const float pieces) {
  screen_ = {canvas[0] / canvas_width, canvas[1] / canvas_height};
  split_pieces_ = pieces;
  pieces_ = pieces;
}

std::array<float, 2> BattleSights::offset_scale() const {
  const auto scale = style_.safe_scale();
  return {scale[0] * screen_[0], scale[1] * screen_[1]};
}

std::array<float, 2> BattleSights::inner_half() const {
  const auto &control = style_.control;
  return reticle_half_size(
      style_.reticle.inner_sizes[static_cast<std::size_t>(control.inner_shape)],
      control.inner_value, control.value_max);
}

std::array<float, 2> BattleSights::outer_half() const {
  const auto &control = style_.control;
  return reticle_half_size(
      style_.reticle.outer_sizes[static_cast<std::size_t>(control.outer_shape)],
      control.outer_value, control.value_max);
}

void BattleSights::draw(const Viewport &viewport, const Battle &battle, const Mat4 &view_projection,
                        const float elapsed, const double clock) {
  const View view{viewport, view_projection};
  const float frames = std::max(elapsed, 0.0F) * hud_frames_per_second;
  const CanvasPoint sight = sight_point(view, battle);
  track(view, battle, sight, frames);
  boxes_ = boxes_on(viewport);
  pieces_ = 1.0F;
  draw_markers(view, battle);
  pieces_ = split_pieces_;
  draw_reticle(viewport, reticle_);
  draw_sight(view, battle, sight, clock);
  draw_locks(view, battle);
}

void BattleSights::set_fire_control(const ChipStats &chip) {
  auto &control = style_.control;
  control.inner_shape = std::clamp(chip.inner_shape, 0, reticle_shapes - 1);
  control.inner_value = chip.inner_value;
  control.outer_shape = std::clamp(chip.outer_shape, 0, reticle_shapes - 1);
  control.outer_value = chip.outer_value;
  control.track_value = chip.track;
  control.lock_frames = chip.lock_frames;
}

const Combatant &BattleSights::viewer(const Battle &battle) const {
  return battle.robots().at(static_cast<std::size_t>(viewer_));
}

BattleSights::CanvasPoint BattleSights::sight_point(const View &view, const Battle &battle) const {
  const Combatant &player = viewer(battle);
  const Vec3 aim_point = player.aim_origin() + battle.aim(player) * style_.aim_distance;
  if (const auto at = view.project(aim_point)) {
    return *at;
  }
  return {(view.viewport.visible_left() + view.viewport.visible_right()) * 0.5F,
          view.viewport.visible_middle()};
}

void BattleSights::track(const View &view, const Battle &battle, const CanvasPoint sight,
                         const float frames) {
  const Combatant &player = viewer(battle);
  const float height = battle.settings().robot_height;
  const auto scale = offset_scale();
  const std::array<float, 2> ndc{style_.safe_area[0] * 0.5F * screen_[0],
                                 style_.safe_area[1] * 0.5F * screen_[1]};
  const auto half = inner_half();
  // Check the chip's box around the sight, not the previous tracked point:
  // moving the acquisition box with the target lets tracking drift unbounded.
  std::vector<std::pair<float, CanvasPoint>> candidates;
  for (const Combatant &robot : battle.robots()) {
    const float distance = length(robot.mover.position() - player.mover.position());
    if (distance > style_.control.track_range || !view.on_screen(robot.centre(height)) ||
        !battle.target_visible(player, robot)) {
      continue;
    }
    if (const auto at = view.project(robot.centre(height))) {
      candidates.emplace_back(distance, *at);
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  std::optional<std::array<float, 2>> target;
  for (const auto &[distance, at] : candidates) {
    if (std::abs(at.x - sight.x) <= half[0] * scale[0] &&
        std::abs(at.y - sight.y) <= half[1] * scale[1]) {
      target = std::array<float, 2>{(at.x - sight.x) / ndc[0], (at.y - sight.y) / ndc[1]};
      break;
    }
  }
  tracker_.update(target, track_speed(style_.control), frames,
                  {half[0] * scale[0] / ndc[0], half[1] * scale[1] / ndc[1]});
  reticle_ = {sight.x + tracker_.offset()[0] * ndc[0], sight.y + tracker_.offset()[1] * ndc[1]};
}

// The reticle's canvas centre and box half sizes in the screen's
// normalised device coordinates.
LockBoxes BattleSights::boxes_on(const Viewport &viewport) const {
  const auto width = static_cast<float>(std::max(viewport.pixel_width, 1));
  const auto height = static_cast<float>(std::max(viewport.pixel_height, 1));
  const auto scale = offset_scale();
  const auto ndc_half = [&](const std::array<float, 2> half) {
    return std::array<float, 2>{2.0F * half[0] * scale[0] * viewport.scale / width,
                                2.0F * half[1] * scale[1] * viewport.scale / height};
  };
  const float x = viewport.to_pixel_x(reticle_.x) - viewport.origin_x;
  const float y = viewport.to_pixel_y(reticle_.y) - viewport.origin_y;
  return {{2.0F * x / width - 1.0F, 1.0F - 2.0F * y / height},
          ndc_half(inner_half()),
          ndc_half(outer_half())};
}

void BattleSights::corners(const Viewport &viewport, const HudPiece &piece,
                           const CanvasPoint centre, const std::array<float, 2> half,
                           const float pad) const {
  const auto scale = offset_scale();
  const auto inset = reticle_corner_inset(piece, pad);
  const float dx = (half[0] - inset[0]) * scale[0];
  const float dy = (half[1] - inset[1]) * scale[1];
  // The top-left piece; mirrored for the top right, turned half round
  // for the bottom right and both for the bottom left (0x4faad).
  put(viewport, piece, centre.x - dx, centre.y - dy, true);
  put(viewport, piece, centre.x + dx, centre.y - dy, true, 0.0F, true);
  put(viewport, piece, centre.x + dx, centre.y + dy, true, half_turn);
  put(viewport, piece, centre.x - dx, centre.y + dy, true, half_turn, true);
}

void BattleSights::draw_reticle(const Viewport &viewport, const CanvasPoint centre) const {
  const auto &reticle = style_.reticle;
  const auto &control = style_.control;
  corners(viewport, reticle.inner[static_cast<std::size_t>(control.inner_shape)], centre,
          inner_half(), reticle.inner_pad);
  corners(viewport, reticle.outer[static_cast<std::size_t>(control.outer_shape)], centre,
          outer_half(), reticle.outer_pad);
  put(viewport, reticle.diamond, centre.x, centre.y, true);
}

void BattleSights::draw_sight(const View &view, const Battle &battle, const CanvasPoint sight,
                              const double clock) const {
  const Combatant &player = viewer(battle);
  const float heading = player.mover.aim_heading();
  const float height = battle.settings().robot_height;
  const auto scale = offset_scale();
  const auto &s = style_.sight;
  const std::uint32_t warning = pulse_colour(s.fill.sprite.colour, clock);
  const float arrow_alpha = s.arrow_alpha / std::max(s.fill.alpha, 1e-3F);
  bool hidden_robot = false;
  for (const Combatant &robot : battle.robots()) {
    if (&robot == &player || !robot.alive) {
      continue;
    }
    const Vec3 to = robot.centre(height) - player.centre(height);
    if (length(to) > style_.sense_range) {
      continue;
    }
    if (view.on_screen(robot.centre(height))) {
      hidden_robot = hidden_robot || robot.camouflaged;
      continue;
    }
    // Off screen and ahead: an arrow from the sight toward it (0x503b9).
    const float right = dot(to, heading_right(heading));
    if (dot(to, heading_forward(heading)) <= 0.0F || (right == 0.0F && to.y == 0.0F)) {
      continue;
    }
    const float angle = std::atan2(-to.y, right);
    const float x = sight.x + s.arrow_radius * std::cos(angle) * scale[0];
    const float y = sight.y + s.arrow_radius * std::sin(angle) * scale[1];
    const float turn = angle * degrees_per_radian - quarter_turn;
    put(view.viewport, s.fill, x, y, true, turn, false, warning, arrow_alpha);
    put(view.viewport, s.outline, x, y, true, turn, false, white, arrow_alpha);
  }
  put(view.viewport, s.ring, sight.x, sight.y, true);
  for (const auto &[dx, dy, angle] : s.markers) {
    const float x = sight.x + dx * scale[0];
    const float y = sight.y + dy * scale[1];
    if (hidden_robot) {
      put(view.viewport, s.fill, x, y, true, angle, false, warning);
    } else {
      put(view.viewport, s.outline, x, y, true, angle);
    }
  }
}

// The lock boxes close on the robot each weapon is locking onto, over the
// fire control's lock time, as the battle holds the lock (0x52490).
void BattleSights::draw_locks(const View &view, const Battle &battle) {
  const Combatant &player = viewer(battle);
  const float height = battle.settings().robot_height;
  const auto scale = offset_scale();
  const auto &locks = style_.locks;
  constexpr std::array<WeaponSlot, 4> slots{WeaponSlot::right_shoulder, WeaponSlot::left_shoulder,
                                            WeaponSlot::right_arm, WeaponSlot::left_arm};
  const std::array<const HudPiece *, 4> pieces{&locks.right_shoulder, &locks.left_shoulder,
                                               &locks.melee, &locks.melee};
  const auto outer = outer_half();
  const float cx = locks.corner[0] * scale[0];
  const float cy = locks.corner[1] * scale[1];
  constexpr std::array<std::array<float, 2>, 4> signs{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
  for (std::size_t key = 0; key < slots.size(); ++key) {
    LockBox &box = locks_[key];
    const auto slot = static_cast<std::size_t>(slots[key]);
    const WeaponTuning *weapon = player.weapons[slot];
    const bool arm = key >= 2;
    int target = player.lock_candidates[slot];
    if (weapon == nullptr || (arm && weapon->kind != WeaponKind::melee) ||
        (!arm && !lock_sights(*weapon)) || target < 0 ||
        target >= static_cast<int>(battle.robots().size()) ||
        !view.on_screen(battle.robots()[static_cast<std::size_t>(target)].centre(height)) ||
        !battle.target_visible(player, battle.robots()[static_cast<std::size_t>(target)])) {
      target = -1;
    }
    const auto at =
        target >= 0 ? view.project(battle.robots()[static_cast<std::size_t>(target)].centre(height))
                    : std::nullopt;
    if (!at) {
      box.target = -1;
      continue;
    }
    std::array<CanvasPoint, 4> goals{};
    for (std::size_t k = 0; k < goals.size(); ++k) {
      goals[k] = {at->x + signs[k][0] * cx, at->y + signs[k][1] * cy};
    }
    if (box.target != target) {
      // A new lock opens at the outer reticle's corners (0x52490).
      box = {target, 0.0F, {}};
      for (std::size_t k = 0; k < goals.size(); ++k) {
        box.spread[k] = {reticle_.x + signs[k][0] * outer[0] * scale[0] - goals[k].x,
                         reticle_.y + signs[k][1] * outer[1] * scale[1] - goals[k].y};
      }
    }
    box.frames = player.lock_held[slot] * hud_frames_per_second;
    const float left = lock_spread_left(box.frames, style_.control.lock_frames);
    for (std::size_t k = 0; k < goals.size(); ++k) {
      put(view.viewport, *pieces[key], goals[k].x + box.spread[k][0] * left,
          goals[k].y + box.spread[k][1] * left, true, quarter_turn * static_cast<float>(k), false,
          locks.colour);
    }
  }
}

void BattleSights::draw_markers(const View &view, const Battle &battle) const {
  const Combatant &player = viewer(battle);
  const float height = battle.settings().robot_height;
  const auto &markers = style_.markers;
  for (const Combatant &robot : battle.robots()) {
    if (&robot == &player || !robot.alive || robot.camouflaged ||
        length(robot.mover.position() - player.mover.position()) > style_.sense_range ||
        !view.on_screen(robot.centre(height))) {
      continue;
    }
    const auto p = view.project(robot.centre(height));
    if (!p) {
      continue;
    }
    const std::uint32_t colour =
        style_.team_colour(robot.marker_colour >= 0 ? robot.marker_colour : robot.team);
    put(view.viewport, markers.frame, p->x + markers.frame_offset[0],
        p->y + markers.frame_offset[1], false, 0.0F, false, colour);
    const float armour =
        robot.max_hp > 0.0F ? std::clamp(robot.hp / robot.max_hp, 0.0F, 1.0F) : 0.0F;
    put(view.viewport, markers.bar, p->x + markers.bar_offset[0], p->y + markers.bar_offset[1],
        false, 0.0F, false, colour, 1.0F, armour);
    draw_marker_extras(view.viewport, robot, *p, colour);
  }
}

// The tag, and the leader icon beside it; a robot without one may show
// its versus player number instead (0x04de50).
void BattleSights::draw_marker_extras(const Viewport &viewport, const Combatant &robot,
                                      const CanvasPoint point, const std::uint32_t colour) const {
  const auto &markers = style_.markers;
  if (robot.tag.empty()) {
    const auto number = static_cast<std::size_t>(robot.player_number);
    if (robot.player_number >= 0 && number < markers.player_numbers.size()) {
      put(viewport, markers.player_numbers[number], point.x + markers.number_offset[0],
          point.y + markers.number_offset[1], false);
    }
    return;
  }
  const std::optional<float> half = draw_tag(viewport, robot.tag, point, colour);
  if (half && markers.defeated &&
      (defeated_.contains({robot.name, robot.tag}) || marked_tags_.contains(robot.tag))) {
    put(viewport, *markers.defeated,
        point.x + markers.defeated_offset[0] - *half * markers.tag.advance,
        point.y + markers.defeated_offset[1], false);
  }
  if (half && robot.leader && markers.leader) {
    put(viewport, *markers.leader, point.x + markers.leader_offset[0] - *half * markers.tag.advance,
        point.y + markers.leader_offset[1], false);
  }
}

std::optional<float> BattleSights::draw_tag(const Viewport &viewport, const std::string &tag,
                                            const CanvasPoint point,
                                            const std::uint32_t colour) const {
  const auto &style = style_.markers.tag;
  const std::vector<int> run = glyph_run(font_, tag);
  if (run.empty() || static_cast<int>(run.size()) > style.max_glyphs) {
    return std::nullopt;
  }
  // Starts half the glyphs' advance left of the offset (n = (glyphs + 1) / 2).
  const auto half = static_cast<float>((run.size() + 1) / 2);
  std::vector<GlyphQuad> glyphs =
      layout_cells(font_, run, point.x + style.offset[0] - half * style.advance,
                   point.y + style.offset[1], style.advance);
  for (GlyphQuad &glyph : glyphs) {
    glyph.width = style.cell;
    glyph.height = style.cell;
  }
  draw_glyphs(textures_, font_, glyphs, viewport, colour);
  return half;
}

} // namespace phantomcrash
