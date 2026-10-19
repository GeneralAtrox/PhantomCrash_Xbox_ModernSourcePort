#include "desktop_vs_mode.hpp"

#include "desktop_draw_list.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {

float fraction(const double value) {
  return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

Sprite placed(Sprite sprite, const float x, const float y) {
  sprite.position = {x, y};
  return sprite;
}

// Grown to `along` of its height (0x0bbef3 with the width kept).
Sprite tall(const Sprite &sprite, const float along,
            const ScaleAnchor anchor = ScaleAnchor::centre) {
  return scaled_sprite(sprite, 1.0F, along, anchor);
}

// The chosen option lit, the other plain.
void draw_yes_no(DrawList &list, const ChoiceSprites &yes, const ChoiceSprites &no,
                 const int choice, const float scale) {
  list.sprite(scaled_sprite(choice == 0 ? yes.selected : yes.normal, scale, scale));
  list.sprite(scaled_sprite(choice == 1 ? no.selected : no.normal, scale, scale));
}

} // namespace

VsModeScreen::VsModeScreen(TextureCache &textures, const GlyphFont &font, VsMode logic,
                           const MessageBoxStyle &box, const std::string &from,
                           const FrameStyle &frame, const std::vector<MusicCredit> &credits,
                           QuestSave quest)
    : TownScreen(textures, font, logic.script(), from, frame, credits, std::move(quest)),
      logic_(std::move(logic)), notice_(box, logic_.script().message_text_at) {
  static_cast<void>(textures_.get(logic_.script().record_font.digits[0].image));
}

void VsModeScreen::tick(const InputFrame &input, const double tick_seconds) {
  logic_.update(tick_seconds, input);
  tick_frame(tick_seconds, logic_.faded_in());
  notice_.update(logic_.message(), MessageBox::Kind::question, logic_.choice(),
                 logic_.script().messages, tick_seconds);
}

std::vector<std::string> VsModeScreen::take_sounds() {
  std::vector<std::string> sounds = logic_.take_sounds();
  for (std::string &name : notice_.take_sounds()) {
    sounds.push_back(std::move(name));
  }
  return sounds;
}

const ChoiceSprites &VsModeScreen::card(const int index) const {
  const VsCards &cards = logic_.script().cards;
  if (const int preset = logic_.preset(index); preset >= 0) {
    return cards.presets[static_cast<std::size_t>(preset)].card;
  }
  const int maker = logic_.maker(index);
  if (logic_.usable(index) && maker >= 0 && maker < static_cast<int>(cards.makers.size())) {
    return cards.makers[static_cast<std::size_t>(maker)];
  }
  return cards.empty;
}

const Sprite *VsModeScreen::icon(const int index) const {
  const VsCards &cards = logic_.script().cards;
  if (const int preset = logic_.preset(index); preset >= 0) {
    return &cards.presets[static_cast<std::size_t>(preset)].icon;
  }
  const int maker = logic_.maker(index);
  if (maker < 0 || maker >= static_cast<int>(cards.icons.size())) {
    return nullptr;
  }
  return &cards.icons[static_cast<std::size_t>(maker)];
}

void VsModeScreen::draw_name(DrawList &list, const std::string &name,
                             const std::array<float, 2> &at, const int layer) const {
  list.text(layer, layout_run(font_, glyph_run(font_, name), at[0], at[1]),
            logic_.script().text_colour);
}

// The phase's message grows, stays and shrinks through the opening; Are
// you ready? grows with YES and NO.
void VsModeScreen::draw_popup(DrawList &list) const {
  const VsModeScript &script = logic_.script();
  const VsPopup &popup = script.popup;
  if (logic_.step() == VsStep::intro) {
    const VsIntro &intro = script.intro;
    const double seconds = logic_.phase_seconds();
    float scale = fraction(seconds / std::max(intro.grow_seconds, 1e-6));
    if (seconds >= intro.shrink_from) {
      scale = fraction((intro.end - seconds) / std::max(intro.end - intro.shrink_from, 1e-6));
    }
    if (scale > 0.0F) {
      list.sprite(scaled_sprite(popup.window, scale, scale));
      const Sprite &message = logic_.phase() == VsPhase::entry ? popup.entry : popup.select;
      list.sprite(scaled_sprite(message, scale, scale));
    }
    return;
  }
  const float scale = logic_.popup().along();
  if (scale <= 0.0F) {
    return;
  }
  list.sprite(scaled_sprite(popup.window, scale, scale));
  list.sprite(scaled_sprite(popup.question, scale, scale));
  draw_yes_no(list, popup.yes, popup.no, logic_.choice(), scale);
}

// 0x03ffc9: the panel grows from its corner and its label opens; then
// what it shows, and No Controller or No Entry over it.
void VsModeScreen::draw_player(DrawList &list, const int port) const {
  const VsModeScript &script = logic_.script();
  const VsTimings &timings = script.timings;
  const VsPlayer &player = logic_.player(port);
  const VsPlayerLayout &layout = script.players[static_cast<std::size_t>(port)];
  const double opened = player.panel.along() * (timings.panel_grow + timings.label_seconds);
  const float grown = fraction(opened / std::max(timings.panel_grow, 1e-6));
  if (grown <= 0.0F) {
    return;
  }
  Sprite panel = layout.panel;
  if (player.joined) {
    panel.colour = timings.joined_colour;
  }
  list.sprite(scaled_sprite(panel, grown, grown, layout.grow_from));
  const float label =
      fraction((opened - timings.panel_grow) / std::max(timings.label_seconds, 1e-6));
  if (label > 0.0F) {
    list.sprite(tall(layout.label, label));
  }
  const bool entry = logic_.phase() == VsPhase::entry;
  if (entry) {
    draw_entry(list, port);
  } else {
    draw_strip(list, port);
    draw_chosen(list, port);
  }
  if (const float over = player.overlay.along(); over > 0.0F) {
    list.sprite(tall(entry ? layout.no_controller : layout.no_entry, over));
  }
}

// ENTER NOW or the entered sign in the box; the next-step hint from the
// panel's edge.
void VsModeScreen::draw_entry(DrawList &list, const int port) const {
  const VsPlayer &player = logic_.player(port);
  const VsPlayerLayout &layout = logic_.script().players[static_cast<std::size_t>(port)];
  if (const float along = player.content.along(); along > 0.0F) {
    if (player.joined) {
      list.sprite(tall(layout.entered, along));
      list.sprite(tall(layout.entered_box, along));
    } else {
      list.sprite(tall(layout.enter_now, along));
    }
    list.sprite(tall(layout.box, along));
  }
  if (const float along = player.extra.along(); along > 0.0F) {
    const ScaleAnchor edge =
        layout.hint_from_bottom ? ScaleAnchor::bottom_left : ScaleAnchor::top_left;
    list.sprite(tall(layout.hint, along, edge));
  }
}

// The Scoobee strip (0x03f5d9, 0x03f503): its labels and arrows, the
// player's wins and losses, the cards and the chosen card's name.
void VsModeScreen::draw_strip(DrawList &list, const int port) const {
  const VsModeScript &script = logic_.script();
  const VsPlayer &player = logic_.player(port);
  const VsPlayerLayout &layout = script.players[static_cast<std::size_t>(port)];
  const float along = player.content.along();
  if (along <= 0.0F || !player.joined) {
    return;
  }
  Sprite left = layout.arrow_left;
  Sprite right = layout.arrow_right;
  if (player.cards.direction() < 0) {
    left.colour = script.timings.joined_colour;
  } else if (player.cards.direction() > 0) {
    right.colour = script.timings.joined_colour;
  }
  for (const Sprite *sprite :
       {&layout.sv_name, &layout.win_lose, &layout.strip, static_cast<const Sprite *>(&left),
        static_cast<const Sprite *>(&right)}) {
    list.sprite(tall(*sprite, along));
  }
  if (along < 1.0F) {
    return;
  }
  const VsSession &session = logic_.session();
  const auto at = static_cast<std::size_t>(port);
  for (const auto &[style, value] :
       {std::pair{&layout.wins, session.wins[at]}, std::pair{&layout.losses, session.losses[at]}}) {
    for (const Sprite &digit :
         number_sprites(script.record_font, *style, value, script.record_layer)) {
      list.sprite(digit);
    }
  }
  draw_cards(list, port);
  if (!player.cards.scrolling()) {
    draw_name(list, logic_.name(player.chosen()), layout.name_at, script.name_layer);
  }
}

// The visible cards and one either side, pushed along while the strip
// scrolls; the chosen card lit, pulsing until it is picked.
void VsModeScreen::draw_cards(DrawList &list, const int port) const {
  const VsModeScript &script = logic_.script();
  const VsCards &cards = script.cards;
  const VsPlayer &player = logic_.player(port);
  const VsPlayerLayout &layout = script.players[static_cast<std::size_t>(port)];
  const int first = player.cards.shown_first(player.first);
  const float offset = player.cards.offset();
  const std::array<float, 4> clip{layout.clip[0], 0.0F, layout.clip[1], 480.0F};
  for (int slot = 0; slot < cards.visible + 2; ++slot) {
    const int index = first - 1 + slot;
    if (index < 0 || index >= logic_.count()) {
      continue;
    }
    const float x = layout.cards_at[0] + cards.step * static_cast<float>(slot) + offset;
    const float y = layout.cards_at[1];
    const ChoiceSprites &shown = card(index);
    list.sprite(placed(shown.normal, x, y), clip);
    if (slot == player.row + 1 && !player.cards.scrolling()) {
      Sprite lit = placed(shown.selected, x, y);
      if (player.decided) {
        lit.animation = Animation::none;
      }
      list.sprite(lit, clip);
    }
  }
}

// The your SV! window with the picked Scoobee's icon and name.
void VsModeScreen::draw_chosen(DrawList &list, const int port) const {
  const VsModeScript &script = logic_.script();
  const VsPlayer &player = logic_.player(port);
  const VsPlayerLayout &layout = script.players[static_cast<std::size_t>(port)];
  const float along = player.extra.along();
  if (along <= 0.0F || !player.joined) {
    return;
  }
  list.sprite(tall(layout.your_sv, along));
  if (along < 1.0F) {
    return;
  }
  if (const Sprite *shown = icon(player.chosen())) {
    list.sprite(placed(*shown, layout.icon_at[0], layout.icon_at[1]));
  }
  draw_name(list, logic_.name(player.chosen()), layout.chosen_name_at, script.chosen_layer);
}

void VsModeScreen::draw(const Viewport &viewport) {
  draw_town(viewport, logic_.seconds(), logic_.black_alpha(), [this](DrawList &list) {
    const VsModeScript &script = logic_.script();
    if (logic_.phase() == VsPhase::area) {
      draw_area(list);
    } else {
      for (const Sprite &sprite : script.backdrop) {
        list.sprite(sprite);
      }
      list.sprite(logic_.phase() == VsPhase::entry ? script.entry_hint : script.select_hint);
      for (int port = 0; port < controller_ports; ++port) {
        draw_player(list, port);
      }
      draw_popup(list);
    }
    notice_.draw(list);
    list.ticker(script.help_style, glyph_run(font_, logic_.help_text()), logic_.help_seconds());
  });
}

} // namespace phantomcrash
