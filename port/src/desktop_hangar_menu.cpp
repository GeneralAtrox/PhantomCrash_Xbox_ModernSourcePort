#include "desktop_hangar_menu.hpp"
#include "phantomcrash/money.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_shop_list.hpp"
#include "phantomcrash/canvas.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace phantomcrash {
namespace {

// The original's picture ends at the canvas edges, so the wheel's cards
// are cut there rather than showing in the widescreen margins.
constexpr std::array<float, 4> canvas_clip{0.0F, 0.0F, canvas_width, canvas_height};

// The hangar's entry in robot_preview.json.
constexpr const char *preview_screen = "hangar";

// `sprite`, whose position is an offset from a card's top-left, placed on
// the card at (x, y).
Sprite on_card(Sprite sprite, const float x, const float y) {
  sprite.position = {sprite.position[0] + x, sprite.position[1] + y};
  return sprite;
}

Sprite coloured(Sprite sprite, const std::uint32_t colour) {
  sprite.colour = colour;
  return sprite;
}

} // namespace

HangarMenuScreen::HangarMenuScreen(TextureCache &textures, const GlyphFont &font, HangarMenu logic,
                                   const MessageBoxStyle &box, const std::string &from,
                                   const FrameStyle &frame, const std::vector<MusicCredit> &credits,
                                   QuestSave quest, int &remembered, PreviewWorld world,
                                   const RobotPreviewScript &preview)
    : TownScreen(textures, font, logic.script(), from, frame, credits, std::move(quest)),
      number_font_(frame.hud.font), logic_(std::move(logic)),
      notice_(box, logic_.script().message_text_at), remembered_(remembered) {
  if (const auto found = preview.screens.find(preview_screen); found != preview.screens.end()) {
    home_ = found->second.place;
  }
  for (int hangar = 0; hangar < garage_hangar_count; ++hangar) {
    robots_.push_back(std::make_unique<RobotPreview>(world, preview, preview_screen));
  }
}

void HangarMenuScreen::tick(const InputFrame &input, const double tick_seconds) {
  logic_.update(tick_seconds, notice_.pointer_input(input, logic_.script().confirm_buttons));
  tick_frame(tick_seconds, logic_.faded_in());
  notice_.update(logic_.message(), logic_.view() == HangarView::buy, logic_.choice(),
                 logic_.script().messages, tick_seconds);
  remembered_ = logic_.card();
  tick_robots(tick_seconds);
  const QuestSave &now = logic_.quest();
  if (quest().money != now.money || quest().riding != now.riding || quest().garage != now.garage) {
    set_quest(now);
  }
}

// 0x01dd99 and 0x01d8ba: an owned hangar with something fitted, within
// reach of the chosen card, has its Scoobee standing on its card and moving
// with it; the chosen one turns, the others face a little further round the
// further off centre they are.
void HangarMenuScreen::tick_robots(const double tick_seconds) {
  const HangarCards &cards = logic_.script().cards;
  const HangarRobots &reach = logic_.script().robots;
  const Garage &garage = logic_.quest().garage;
  for (int hangar = 0; hangar < garage_hangar_count; ++hangar) {
    RobotPreview &robot = *robots_[static_cast<std::size_t>(hangar)];
    const int count = HangarMenu::card_count;
    const int along = ((hangar - logic_.card()) % count + count) % count;
    const HangarStatus &bay = logic_.status(hangar);
    if (std::min(along, count - along) > reach.neighbours || !bay.owned || bay.empty) {
      robot.hide();
      continue;
    }
    robot.show(preview_robot(garage, garage.hangars[static_cast<std::size_t>(hangar)]));
    const float steps = (logic_.card_x(hangar) - cards.centre_x) / cards.step;
    const bool chosen = hangar == logic_.card();
    PreviewPlacement place = home_;
    place.position.x += reach.spacing * steps;
    place.yaw += chosen ? 0.0F : reach.yaw_per_step * steps;
    robot.place(place, chosen);
    robot.tick(tick_seconds);
  }
}

std::vector<std::string> HangarMenuScreen::take_sounds() {
  std::vector<std::string> sounds = logic_.take_sounds();
  for (std::string &name : notice_.take_sounds()) {
    sounds.push_back(std::move(name));
  }
  return sounds;
}

// 0x01d4d1: an owned hangar's card shows EMPTY, or RIDE when ridden, its
// maker's emblem, its Scoobee's name and what keeps it from a battle.
void HangarMenuScreen::draw_hangar(DrawList &list, const int hangar, const float x) const {
  const HangarCards &cards = logic_.script().cards;
  const HangarStatus &bay = logic_.status(hangar);
  const float y = cards.y;
  const std::uint32_t colour = bay.riding ? cards.riding_colour : cards.colour;
  if (!bay.owned) {
    list.sprite(on_card(cards.for_sale, x, y), canvas_clip);
    NumberStyle price = cards.price;
    price.right = {price.right[0] + x, price.right[1] + y};
    for (const Sprite &digit : number_sprites(
             number_font_, price, shown_ny(logic_.script().hangar_price), cards.price_layer)) {
      list.sprite(digit, canvas_clip);
    }
    return;
  }
  if (bay.empty) {
    list.sprite(on_card(cards.empty, x, y), canvas_clip);
    return;
  }
  if (bay.riding) {
    list.sprite(on_card(cards.ride, x, y), canvas_clip);
  }
  if (bay.maker >= 0 && bay.maker < static_cast<int>(cards.emblems.size())) {
    list.sprite(on_card(cards.emblems[static_cast<std::size_t>(bay.maker)], x, y), canvas_clip);
  }
  const Hangar &robot = logic_.quest().garage.hangars[static_cast<std::size_t>(hangar)];
  std::vector<int> name = glyph_run(font_, robot.name);
  name.resize(std::min(name.size(), static_cast<std::size_t>(cards.name_glyphs)));
  list.text(cards.name_layer, layout_run(font_, name, x + cards.name_at[0], y + cards.name_at[1]),
            colour, canvas_clip);
  list.sprite(on_card(cards.name_line, x, y), canvas_clip);
  if (!bay.complete) {
    list.sprite(on_card(cards.incomplete, x, y), canvas_clip);
    return;
  }
  float warning_y = y;
  if (bay.unarmed) {
    list.sprite(on_card(cards.underkill, x, warning_y), canvas_clip);
    warning_y -= cards.warning_step;
  }
  if (bay.overweight) {
    list.sprite(on_card(cards.overweight, x, warning_y), canvas_clip);
  }
}

// 0x01d4d1: the card, grey or orange for the ridden hangar, with its
// number; EXIT's card is the way out's own picture.
void HangarMenuScreen::draw_card(DrawList &list, const int card) const {
  const HangarCards &cards = logic_.script().cards;
  const float x = logic_.card_x(card);
  const float width = cards.card.rect[2] - cards.card.rect[0];
  if (x >= canvas_width || x + width <= 0.0F) {
    return;
  }
  if (card == HangarMenu::exit_card) {
    list.sprite(on_card(cards.exit, x, cards.y), canvas_clip);
    return;
  }
  const std::uint32_t colour = logic_.status(card).riding ? cards.riding_colour : cards.colour;
  list.sprite(coloured(on_card(cards.card, x, cards.y), colour), canvas_clip);
  for (const Sprite &number : cards.numbers[static_cast<std::size_t>(card)]) {
    list.sprite(coloured(on_card(number, x, cards.y), colour), canvas_clip);
  }
  draw_hangar(list, card, x);
}

// 0x01debc with 0x06c530: the plates slide in with the line and EXIT; the
// ridden hangar's plate orange, the chosen one gold, pulsing on the wheel.
void HangarMenuScreen::draw_plates(DrawList &list) const {
  const HangarPlates &plates = logic_.script().plates;
  const float slide = logic_.plates_slide();
  std::vector<float> xs;
  for (std::size_t index = 0; index < plates.plates.size(); ++index) {
    const float x = plates.from_x[index] + (plates.to_x[index] - plates.from_x[index]) * slide;
    xs.push_back(x);
    for (const Sprite &plate : plates.plates[index]) {
      list.sprite(shifted(plate, x, plates.y));
    }
  }
  const float offset = xs.back() - plates.to_x.back();
  list.sprite(shifted(plates.line, offset, 0.0F));
  list.sprite(shifted(plates.exit, offset, 0.0F));
  const auto mark = [&](const int hangar, const std::uint32_t colour, const int layer,
                        const bool pulse) {
    for (Sprite sprite : plates.marks[static_cast<std::size_t>(hangar)]) {
      sprite = shifted(sprite, xs[static_cast<std::size_t>(hangar)], plates.y);
      sprite.colour = colour;
      sprite.layer = layer;
      sprite.animation = pulse ? Animation::pulse : Animation::none;
      list.sprite(sprite);
    }
  };
  const int riding = logic_.quest().garage.riding;
  if (riding >= 0 && riding < garage_hangar_count) {
    mark(riding, plates.riding_colour, plates.marks[0].front().layer, false);
  }
  const bool active = logic_.view() == HangarView::wheel;
  if (logic_.card() == HangarMenu::exit_card) {
    list.sprite(plates.exit_lit);
  } else {
    mark(logic_.card(), plates.chosen_colour, plates.chosen_layer, active);
  }
}

// 0x01d1e9: the window pops open, the items grey with the chosen one lit
// and struck-out marks over those that cannot be chosen.
void HangarMenuScreen::draw_menu(DrawList &list) const {
  const HangarMenuStyle &menu = logic_.script().menu;
  if (!draw_pop_window(list, menu.window, logic_.menu_seconds())) {
    return;
  }
  for (std::size_t index = 0; index < menu.items.size(); ++index) {
    const HangarMenuItem &item = menu.items[index];
    list.sprite(item.sprite);
    if (static_cast<int>(index) == logic_.menu_item()) {
      Sprite lit = coloured(item.sprite, menu.lit_colour);
      lit.layer = menu.lit_layer;
      lit.animation = logic_.view() == HangarView::menu ? Animation::pulse : Animation::none;
      list.sprite(lit);
    }
    if (!logic_.can_choose(item.action)) {
      Sprite strike = menu.strike;
      strike.position[1] = item.sprite.position[1];
      list.sprite(strike, menu.strike_alpha);
    }
  }
}

void HangarMenuScreen::draw(const Viewport &viewport) {
  draw_town(viewport, logic_.seconds(), logic_.black_alpha(), [this, &viewport](DrawList &list) {
    const HangarMenuScript &script = logic_.script();
    for (const Sprite &band : script.bands) {
      list.sprite(band);
    }
    for (int card = 0; card < HangarMenu::card_count; ++card) {
      draw_card(list, card);
    }
    draw_plates(list);
    for (const auto &robot : robots_) {
      robot->draw(list, viewport, textures_.renderer());
    }
    if (logic_.menu_open()) {
      draw_menu(list);
    }
    notice_.draw(list);
    list.ticker(script.help_style, glyph_run(font_, logic_.help_text()), logic_.help_seconds());
  });
}

} // namespace phantomcrash
