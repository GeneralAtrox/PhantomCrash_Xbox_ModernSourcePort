#include "desktop_sv_spec.hpp"

#include "desktop_draw_list.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {

SvSpecScreen::SvSpecScreen(TextureCache &textures, const GlyphFont &font, SvSpec logic,
                           const std::string &from, const FrameStyle &frame,
                           const std::vector<MusicCredit> &credits, QuestSave quest,
                           PreviewWorld world, const RobotPreviewScript &preview)
    : TownScreen(textures, font, logic.script(), from, frame, credits, std::move(quest)),
      logic_(std::move(logic)), preview_(world, preview, "sv_spec") {
  const SpecRobot &robot = logic_.robot();
  if (robot.hangar >= 0 && robot.hangar < static_cast<int>(robot.garage.hangars.size())) {
    preview_.show(
        preview_robot(robot.garage, robot.garage.hangars[static_cast<std::size_t>(robot.hangar)]));
  }
}

void SvSpecScreen::tick(const InputFrame &input, const double tick_seconds) {
  logic_.update(tick_seconds, input);
  tick_frame(tick_seconds, logic_.faded_in());
  preview_.tick(tick_seconds);
}

// 0x03ba94: the SV's and its CHIP's names, the body maker's badge.
void SvSpecScreen::draw_window(DrawList &list) const {
  const SvSpecScript &script = logic_.script();
  const auto name = [&](const std::string &text, const std::array<float, 2> &at) {
    std::vector<int> run = glyph_run(font_, text);
    run.resize(std::min(run.size(), static_cast<std::size_t>(script.name_glyphs)));
    list.text(script.name_layer, layout_run(font_, run, at[0], at[1]), script.name_colour);
  };
  name(logic_.robot().name, script.name_at);
  name(logic_.totals().chip_name, script.chip_name_at);
  const int maker = logic_.maker();
  if (maker >= 0 && maker < static_cast<int>(script.maker_icons.size())) {
    Sprite badge = script.maker_icons[static_cast<std::size_t>(maker)];
    badge.position = script.maker_at;
    list.sprite(badge);
  }
}

// 0x03bc25: each line's value right-aligned, `step` apart.
void SvSpecScreen::draw_vehicle(DrawList &list) const {
  const SvSpecScript &script = logic_.script();
  float y = script.values_top;
  for (const SpecRow &row : script.rows) {
    const std::vector<int> run =
        glyph_run(font_, stat_text({StatRow::level, logic_.value(row.stat), row.decimals}));
    list.text(script.value_layer,
              layout_run(font_, run, script.values_right - run_width(font_, run), y),
              script.value_colour);
    y += script.values_step;
  }
}

// The combat page (a port addition): each line's label and unit lettered as
// the vehicle page's art letters them, its value placed as theirs are.
void SvSpecScreen::draw_combat(DrawList &list) const {
  const SvSpecScript &script = logic_.script();
  const CombatLettering &lettering = script.combat_lettering;
  const float left = lettering.origin[0];
  float top = lettering.origin[1];
  float y = script.values_top;
  for (const CombatRow &row : script.combat_rows) {
    const auto box = [&](const std::array<float, 2> &x, const std::array<float, 2> &ink) {
      return std::array{left + x[0], top + ink[0], left + x[1], top + ink[1]};
    };
    list.label(script.value_layer, row.label, lettering.font, box(lettering.label_x, lettering.ink),
               lettering.colour, lettering.shadow, lettering.shadow_blur);
    if (!row.unit.empty()) {
      list.label(script.value_layer, row.unit, lettering.unit_font,
                 box(lettering.unit_x, row.unit_ink.value_or(lettering.ink)), lettering.colour,
                 lettering.shadow, lettering.shadow_blur);
    }
    const std::vector<int> run =
        glyph_run(font_, stat_text({StatRow::level, logic_.value(row.stat), row.decimals}));
    list.text(script.value_layer,
              layout_run(font_, run, script.values_right - run_width(font_, run), y),
              script.value_colour);
    top += script.values_step;
    y += script.values_step;
  }
}

// 0x03b82e: each slot's weapon, or Not Equip in red.
void SvSpecScreen::draw_weapons(DrawList &list) const {
  const SvSpecScript &script = logic_.script();
  for (std::size_t index = 0; index < script.weapon_slots.size(); ++index) {
    const auto name = logic_.weapon(script.weapon_slots[index]);
    list.text(script.weapon_layer,
              layout_run(font_, glyph_run(font_, name ? *name : script.none_text), script.weapons_x,
                         script.weapon_rows[index]),
              name ? script.weapon_colour : script.none_colour);
  }
}

void SvSpecScreen::draw(const Viewport &viewport) {
  draw_town(viewport, logic_.seconds(), logic_.black_alpha(), [this, &viewport](DrawList &list) {
    const SvSpecScript &script = logic_.script();
    draw_window(list);
    preview_.draw(list, viewport, textures_.renderer());
    if (const auto &panel = script.pages[static_cast<std::size_t>(logic_.page())].panel) {
      list.sprite(*panel);
    }
    for (const Sprite &arrow : script.arrows) {
      list.sprite(arrow);
    }
    for (const Sprite &hint : script.close_hint) {
      list.sprite(hint);
    }
    switch (logic_.page_kind()) {
    case SpecPageKind::vehicle:
      draw_vehicle(list);
      break;
    case SpecPageKind::weapons:
      draw_weapons(list);
      break;
    case SpecPageKind::combat:
      draw_combat(list);
      break;
    }
    list.ticker(script.help_style, glyph_run(font_, logic_.help_text()), logic_.help_seconds());
  });
}

} // namespace phantomcrash
