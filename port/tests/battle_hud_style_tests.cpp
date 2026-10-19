// The battle HUD's styles as data/ui/battle_hud.json and battle_panels.json
// give them, and the HUD's own state: the original's flash, the closing
// lock boxes, the tracking reticle and the message log.

#include "test_support.hpp"

#include <cmath>

#include "phantomcrash/battle_hud_state.hpp"
#include "phantomcrash/battle_hud_style.hpp"
#include "phantomcrash/battle_panels_style.hpp"
#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_shapes.hpp"
#include "phantomcrash/sprite_text.hpp"

using namespace phantomcrash;

namespace {

json::JsonValue shipped(const char *relative) {
  return DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document(relative);
}

bool close_to(const float a, const float b) {
  return std::fabs(a - b) < 1e-4F;
}

} // namespace

TEST_CASE("the shipped battle HUD reads the original's reticle tables") {
  const BattleHudStyle style = parse_battle_hud_style(shipped("ui/battle_hud.json"));
  CHECK(style.sense_range == 75.0F);
  CHECK(style.aim_distance == 64.0F);
  CHECK(close_to(style.safe_scale()[0], 0.9F));
  CHECK(style.sight.markers.size() == 4);
  // 0x203c78, shape 1 (wide): x 135..210, y 45..70.
  CHECK(style.reticle.outer_sizes[1][0] == 135.0F);
  CHECK(style.reticle.outer_sizes[1][3] == 70.0F);
  CHECK(style.reticle.outer_pad == 14.0F);
  CHECK(style.locks.colour == 0x00b496U);
  CHECK(style.team_colour(2) == 0xff8080U);
  CHECK(style.team_colour(0) == 0xffff00U);
  CHECK(style.team_colour(1) == 0x8080ffU);
  CHECK(style.team_colour(3) == 0x00e100U);
  CHECK(style.team_colour(5) == 0xffff00U);
  CHECK(style.team_colour(-1) == 0xffff00U);
  // The name tag (0x04de50): 12 x 12 glyphs centred over the point - 35, 30 up.
  CHECK(style.markers.tag.offset[0] == -35.0F);
  CHECK(style.markers.tag.offset[1] == -30.0F);
  CHECK(style.markers.tag.cell == 12.0F);
  CHECK(style.markers.tag.max_glyphs == 14);
  // The leader icon and versus mode's 1P-4P labels beside the markers.
  CHECK(style.markers.leader.has_value());
  CHECK(style.markers.leader_offset[0] == -62.0F);
  CHECK(style.markers.player_numbers.size() == 4);
  CHECK(style.markers.number_offset[1] == -34.0F);
}

TEST_CASE("a reticle's size runs over its table with the value") {
  const ReticleSizes square{40.0F, 120.0F, 40.0F, 120.0F};
  CHECK(reticle_half_size(square, 0.0F, 999.0F)[0] == 40.0F);
  CHECK(reticle_half_size(square, 999.0F, 999.0F)[1] == 120.0F);
  CHECK(close_to(reticle_half_size(square, 499.5F, 999.0F)[0], 80.0F));
  CHECK(reticle_half_size(square, 5000.0F, 999.0F)[0] == 120.0F); // clamped
  HudPiece corner;
  corner.sprite.rect = {0.0F, 0.0F, 32.0F, 24.0F};
  // (16 + 2) / 640 NDC is 9 units; (12 + 2) / 480 NDC is 7.
  CHECK(close_to(reticle_corner_inset(corner, 2.0F)[0], 9.0F));
  CHECK(close_to(reticle_corner_inset(corner, 2.0F)[1], 7.0F));
}

TEST_CASE("Aim Speed sets the reticle's speed on the port's 0..10 scale") {
  BattleHudStyle::Control control = parse_battle_hud_style(shipped("ui/battle_hud.json")).control;
  control.track_value = 1.0F; // the cats: the original's speed
  CHECK(close_to(track_speed(control), 0.02F));
  control.track_value = 3.0F;
  CHECK(close_to(track_speed(control), 0.036F));
  control.track_value = 10.0F;
  CHECK(close_to(track_speed(control), 0.092F));
  control.track_value = 500.0F; // past the scale's top
  CHECK(close_to(track_speed(control), 0.092F));
  // The original's own constants (0x4f6a0): tracks 1-5 barely differ.
  control.track_base = 0.02F;
  control.track_full = 999.0F;
  control.track_value = 5.0F;
  CHECK(close_to(track_speed(control), 0.0204F));
}

TEST_CASE("a battle HUD without its sight is refused") {
  bool threw = false;
  try {
    static_cast<void>(parse_battle_hud_style(json::parse("{}")));
  } catch (const std::exception &) {
    threw = true;
  }
  CHECK(threw);
}

TEST_CASE("the shipped battle panels read the original's records") {
  const BattlePanelsStyle style = parse_battle_panels_style(shipped("ui/battle_panels.json"));
  CHECK(style.widescreen);
  CHECK(style.durability.frame.position[0] == 245.0F);
  CHECK(style.durability.width == 128.0F);
  CHECK(style.durability.colour == 0xffc823U);
  CHECK(style.optic.width == 72.0F);
  CHECK(style.weapons.right.arm[0] == 487.0F);
  CHECK(style.weapons.left.shoulder[1] == 124.0F);
  CHECK(style.weapons.right.bar_mirrored);
  CHECK(style.weapons.left.digit_positions[3][0] == 48.0F);
  CHECK(style.weapons.right.digit_positions[3][0] == 67.0F);
  CHECK(style.weapons.icons.at(hud_kind::missile).homing_left.has_value());
  CHECK(!style.weapons.icons.at(hud_kind::cannon).homing_left.has_value());
  CHECK(style.weapons.icons.at(hud_kind::grenade).readout == WeaponReadout::ammo);
  CHECK(style.weapons.icons.at(hud_kind::beam).readout == WeaponReadout::charge);
  CHECK(style.weapons.icons.at(hud_kind::melee).readout == WeaponReadout::none);
  CHECK(style.colon_advance == 8.0F);
  CHECK(style.day.label.position[0] == 396.0F);
  CHECK(style.time.number.right[0] == 586.0F);
  CHECK(style.shootdown.number.digits == 4);
  CHECK(style.shootdown.number.pad);
  CHECK(style.messages.life_frames == 120.0F);
  CHECK(style.shootdown.anchor == HudAnchor::left);
  CHECK(style.money.number.commas);
}

TEST_CASE("anchored panels keep to the window's edges") {
  CHECK(anchor_x(HudAnchor::left, 56.0F, -100.0F, 740.0F) == -44.0F);
  CHECK(anchor_x(HudAnchor::right, 487.0F, -100.0F, 740.0F) == 587.0F);
  CHECK(anchor_x(HudAnchor::centre, 245.0F, -100.0F, 740.0F) == 245.0F);
}

TEST_CASE("the HUD flash follows the original's frame counter") {
  // Frame 0 is the first colour; half way through it has gone 0.8 of the
  // way at frame 4 of 10, then swings past the first colour the other way.
  CHECK(hud_flash(0x000000, 0x640000, 0.0, 10.0F) == 0x000000U);
  CHECK(hud_flash(0x000000, 0x640000, 4.0 / 60.0 + 1e-6, 10.0F) == 0x500000U);
  CHECK(hud_flash(0x640000, 0x000000, 6.0 / 60.0 + 1e-6, 10.0F) == 0x780000U);
  CHECK(close_to(hud_wave(0.0, 60.0F), 0.0F));
  CHECK(close_to(hud_wave(30.0 / 60.0 + 1e-6, 60.0F), 1.0F));
}

TEST_CASE("a lock box closes as its lock time runs out") {
  CHECK(lock_spread_left(0.0F, 30.0F) == 1.0F);
  CHECK(close_to(lock_spread_left(1.0F, 30.0F), 29.0F / 30.0F));
  CHECK(close_to(lock_spread_left(2.0F, 30.0F), 29.0F / 30.0F * 28.0F / 30.0F));
  CHECK(lock_spread_left(30.0F, 30.0F) == 0.0F);
  CHECK(lock_spread_left(5.0F, 0.0F) == 0.0F);
}

TEST_CASE("the reticle follows its target no faster than its speed") {
  ReticleTracker tracker;
  tracker.update(std::array<float, 2>{0.3F, 0.4F}, 0.05F, 2.0F, {1.0F, 1.0F});
  CHECK(close_to(tracker.offset()[0], 0.06F));
  CHECK(close_to(tracker.offset()[1], 0.08F));
  tracker.update(std::array<float, 2>{0.3F, 0.4F}, 0.05F, 100.0F, {1.0F, 1.0F});
  CHECK(close_to(tracker.offset()[0], 0.3F));
  tracker.update(std::nullopt, 1.0F, 1.0F, {1.0F, 1.0F}); // back to the sight
  CHECK(tracker.offset()[0] == 0.0F);
}

TEST_CASE("a target cannot drag the tracking point beyond the chip box") {
  ReticleTracker tracker;
  const std::array<float, 2> half{0.2F, 0.1F};
  for (int frame = 0; frame < 120; ++frame) {
    const float position = static_cast<float>(frame) * 0.01F;
    tracker.update(std::array<float, 2>{position, -position}, 0.05F, 1.0F, half);
    CHECK(std::abs(tracker.offset()[0]) <= half[0]);
    CHECK(std::abs(tracker.offset()[1]) <= half[1]);
  }
  CHECK(close_to(tracker.offset()[0], half[0]));
  CHECK(close_to(tracker.offset()[1], -half[1]));
  tracker.update(std::nullopt, 0.05F, 10.0F, half);
  CHECK(tracker.offset()[0] == 0.0F);
  CHECK(tracker.offset()[1] == 0.0F);
}

TEST_CASE("changing to a narrower chip box bounds the current tracking offset immediately") {
  ReticleTracker tracker;
  tracker.update(std::array<float, 2>{-0.4F, 0.2F}, 1.0F, 1.0F, {0.5F, 0.3F});
  tracker.update(std::array<float, 2>{-0.4F, 0.2F}, 1.0F, 0.0F, {0.1F, 0.3F});
  CHECK(close_to(tracker.offset()[0], -0.1F));
  CHECK(close_to(tracker.offset()[1], 0.2F));
  tracker.update(std::array<float, 2>{-0.4F, 0.2F}, 1.0F, 0.0F, {0.3F, 0.05F});
  CHECK(close_to(tracker.offset()[0], -0.1F));
  CHECK(close_to(tracker.offset()[1], 0.05F));
}

TEST_CASE("messages slide in, rise a step for each newer one and fade out") {
  HudMessageLog log(HudMessageLog::Style{});
  log.push(hud_message_hit);
  log.update(1.0F);
  auto entries = log.entries();
  CHECK(entries.size() == 1);
  if (entries.size() != 1) {
    return;
  }
  CHECK(entries[0].kind == hud_message_hit);
  CHECK(entries[0].alpha < 0.2F); // just arrived: fading in from below
  CHECK(entries[0].offset > 0.0F);
  log.push(hud_message_bingo);
  for (int frame = 0; frame < 30; ++frame) {
    log.update(1.0F);
  }
  entries = log.entries();
  CHECK(entries.size() == 2);
  if (entries.size() != 2) {
    return;
  }
  CHECK(entries[0].kind == hud_message_bingo); // the newest at the bottom
  CHECK(close_to(entries[0].offset, 0.0F));
  CHECK(close_to(entries[1].offset, -15.0F));
  CHECK(entries[1].alpha == 1.0F);
  for (int frame = 0; frame < 120; ++frame) {
    log.update(1.0F);
  }
  CHECK(log.entries().empty());
}

TEST_CASE("the shipped sprite text names plain atlas lettering and a shadow") {
  const SpriteTextTable table = parse_sprite_text_table(shipped("ui/sprite_text.json"));
  CHECK(table.shadow.alpha > 0.0F);
  CHECK(close_to(table.shadow.offset[0], 1.0F));
  CHECK(table.shadow.colour == 0x000000U);
  bool money = false;
  bool for_sale = false;
  for (const SpriteTextStyle &entry : table.replacements) {
    CHECK(!entry.text.empty());
    if (entry.font.empty()) {
      // Atlas-font lettering is the common HUD art's.
      CHECK(entry.source.image == "textures/scroll/common_2d_tex/1.png");
    } else {
      CHECK(table.fonts.contains(entry.font));
    }
    money = money || (entry.text == "MONEY" && close_to(entry.source.rect[0], 400.0F));
    if (entry.text == "For Sale!") {
      // Erased along the plate's leaning edge, the italic lettering upright.
      for_sale = entry.erase_slant.has_value() && close_to(*entry.erase_slant, 0.4F) &&
                 close_to(entry.slant, 0.15F);
    }
  }
  CHECK(money);
  CHECK(for_sale);
}

TEST_CASE("the shipped sprite shapes outline boxes inside their sprites") {
  const std::vector<SpriteShape> shapes = parse_sprite_shapes(shipped("ui/sprite_shapes.json"));
  REQUIRE(!shapes.empty());
  for (const SpriteShape &shape : shapes) {
    REQUIRE(!shape.outlines.empty());
    const float width = shape.rect[2] - shape.rect[0];
    const float height = shape.rect[3] - shape.rect[1];
    for (const ShapeOutline &outline : shape.outlines) {
      const auto columns = static_cast<float>(outline.count[0] - 1);
      const auto rows = static_cast<float>(outline.count[1] - 1);
      CHECK(outline.box[0] >= 0.0F);
      CHECK(outline.box[1] >= 0.0F);
      CHECK(outline.box[2] + outline.step[0] * columns <= width);
      CHECK(outline.box[3] + outline.step[1] * rows <= height);
    }
  }
}

TEST_CASE("a shape's contour moves its edge and corners by the distance") {
  ShapeOutline outline;
  outline.box = {0.0F, 0.0F, 20.0F, 10.0F};
  outline.radius = 2.0F;
  // Outward the corners round by as much; inward they turn square.
  const auto outer = shape_contour(outline, outline.box, 1.0F, 4);
  REQUIRE(outer.size() == 20U);
  CHECK(close_to(outer[0][0], -1.0F));
  CHECK(close_to(outer[0][1], 2.0F));
  CHECK(close_to(outer[4][0], 2.0F));
  CHECK(close_to(outer[4][1], -1.0F));
  const auto inner = shape_contour(outline, outline.box, -3.0F, 4);
  CHECK(close_to(inner[0][0], 3.0F));
  CHECK(close_to(inner[0][1], 3.0F));
  CHECK(close_to(inner[10][0], 17.0F));
  CHECK(close_to(inner[10][1], 7.0F));
}

TEST_CASE("a path outline reads its offset contour, shade direction and shaded stops") {
  const auto shapes = parse_sprite_shapes(json::parse(R"({"shapes": [{
    "image": "plate.png", "rect": [0, 0, 32, 32], "outlines": [{
      "path": "M 0 0 L 10 0 L 10 10 L 0 10 Z", "offset": [1, 2], "shade_toward": [1, 1],
      "stops": [[-1, "#102030", 0.5, "#405060", 0.25], [0, "#ffffff"]]}]}]})"));
  REQUIRE(shapes.size() == 1U);
  const ShapeOutline &outline = shapes[0].outlines.at(0);
  REQUIRE(outline.path.size() == 4U);
  CHECK(close_to(outline.box[0], 1.0F));
  CHECK(close_to(outline.box[3], 12.0F));
  REQUIRE(outline.shade_toward.has_value());
  CHECK(close_to((*outline.shade_toward)[0], std::sqrt(0.5F)));
  CHECK(outline.stops[0].shade_colour == 0x405060U);
  CHECK(close_to(outline.stops[0].shade_alpha, 0.25F));
  CHECK(close_to(outline.stops[1].shade_alpha, outline.stops[1].alpha));
  // Inward each corner moves along its bisector, into the square.
  const auto inner = shape_contour(outline, outline.box, -1.0F, 4);
  REQUIRE(inner.size() == 4U);
  CHECK(close_to(inner[0][0], 2.0F));
  CHECK(close_to(inner[0][1], 3.0F));
  CHECK(close_to(inner[2][0], 10.0F));
  CHECK(close_to(inner[2][1], 11.0F));
}
