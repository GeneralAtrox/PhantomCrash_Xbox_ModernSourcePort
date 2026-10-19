#include "desktop_used_sv.hpp"
#include "phantomcrash/money.hpp"

#include "desktop_draw_list.hpp"
#include "phantomcrash/canvas.hpp"

#include <utility>

namespace phantomcrash {
namespace {

Sprite placed(Sprite sprite, const float x, const float y, const int layer) {
  sprite.position = {x, y};
  sprite.layer = layer;
  return sprite;
}

} // namespace

UsedSvScreen::UsedSvScreen(TextureCache &textures, const GlyphFont &font, UsedSv logic,
                           const MessageBoxStyle &box, const std::string &from,
                           const FrameStyle &frame, const std::vector<MusicCredit> &credits,
                           QuestSave quest, int &remembered, PreviewWorld world,
                           const RobotPreviewScript &preview)
    : TownScreen(textures, font, logic.script(), from, frame, credits, std::move(quest)),
      number_font_(frame.hud.font), logic_(std::move(logic)),
      purchase_(box, logic_.script().purchase), remembered_(remembered),
      preview_(world, preview, "module_shop") {
  static_cast<void>(textures_.get(logic_.script().plate.image));
}

void UsedSvScreen::tick(const InputFrame &input, const double tick_seconds) {
  const auto before = logic_.purchase().view();
  logic_.update(tick_seconds, purchase_.pointer_input(input, logic_.script().confirm_buttons));
  if (before != logic_.purchase().view()) {
    SDL_Log("Shop purchase: view=%d->%d action=%d mouse=%d", int(before),
            int(logic_.purchase().view()), logic_.purchase().action(), input.clicked);
  }
  tick_frame(tick_seconds, logic_.faded_in());
  purchase_.update(logic_.purchase(), tick_seconds);
  remembered_ = logic_.cursor();
  const QuestSave &now = logic_.quest();
  if (quest().money != now.money || quest().riding != now.riding ||
      quest().garage.free_stock() != now.garage.free_stock()) {
    set_quest(now);
  }
  // 0x226c6: the SV under the cursor stands in Wild Machines' preview (its
  // mode 3, as BUY MODULE's); none over EXIT.
  const int offer = logic_.offer_of(logic_.cursor());
  if (offer >= 0 && logic_.opened()) {
    preview_.show(preview_record(logic_.offers()[static_cast<std::size_t>(offer)].robot));
  } else {
    preview_.hide();
  }
  preview_.tick(tick_seconds);
}

std::vector<std::string> UsedSvScreen::take_sounds() {
  std::vector<std::string> sounds = logic_.take_sounds();
  for (std::string &name : purchase_.take_sounds()) {
    sounds.push_back(std::move(name));
  }
  return sounds;
}

// 0x230f4: the gears turning, the plate sliding in and the strip fading in.
void UsedSvScreen::draw_backdrop(DrawList &list) const {
  const UsedSvScript &script = logic_.script();
  for (Sprite gear : script.gears) {
    gear.angle = logic_.gear_angle();
    list.sprite(gear);
  }
  Sprite plate = script.plate;
  plate.position[0] += logic_.plate_offset();
  list.sprite(plate);
  list.sprite(script.strip, logic_.strip_alpha());
}

// 0x2022b: each item's icon sliding in to its slot, the chosen one lit.
void UsedSvScreen::draw_slots(DrawList &list) const {
  const UsedSvScript &script = logic_.script();
  for (int item = 0; item < logic_.item_count(); ++item) {
    const int slot = logic_.slot_of(item);
    const float x = logic_.slot_x(slot);
    const float y = script.slots[static_cast<std::size_t>(slot)][1];
    const ShopIcon &icon = logic_.icon(item);
    list.sprite(placed(icon.sprite, x, y, script.icon_layer));
    if (item == logic_.cursor()) {
      list.sprite(placed(icon.lit, x, y, script.lit_layer));
    }
  }
}

// 0x230f4: the panel grows open over an SV; then its maker's badge, its
// name and its price.
void UsedSvScreen::draw_info(DrawList &list) const {
  const UsedSvScript &script = logic_.script();
  const float open = logic_.info_open();
  if (open <= 0.0F) {
    return;
  }
  list.sprite(grown_panel(script.info_panel, 1.0F - open, script.info_growth.from_scale));
  const int offer = logic_.offer_of(logic_.cursor());
  if (open < 1.0F || offer < 0) {
    return;
  }
  const SvOffer &shown = logic_.offers()[static_cast<std::size_t>(offer)];
  if (shown.maker >= 0 && shown.maker < static_cast<int>(script.badges.size())) {
    list.sprite(script.badges[static_cast<std::size_t>(shown.maker)]);
  }
  list.text(
      script.name_layer,
      layout_run(font_, glyph_run(font_, shown.robot.name), script.name_at[0], script.name_at[1]),
      script.name_colour);
  list.sprite(script.price_label);
  for (const Sprite &digit :
       money_sprites(hud(), script.price, shown_ny(shown.price), script.price_label.layer)) {
    list.sprite(digit);
  }
}

void UsedSvScreen::draw(const Viewport &viewport) {
  draw_town(viewport, logic_.seconds(), logic_.black_alpha(), [this, &viewport](DrawList &list) {
    const UsedSvScript &script = logic_.script();
    draw_backdrop(list);
    preview_.draw(list, viewport, textures_.renderer());
    draw_slots(list);
    draw_info(list);
    purchase_.draw(list, logic_.purchase(), logic_.quest().garage);
    if (logic_.opened()) {
      list.ticker(script.help_style, glyph_run(font_, logic_.help_text()), logic_.help_seconds());
    }
  });
}

} // namespace phantomcrash
