#include "desktop_keep_sv.hpp"

#include "desktop_draw_list.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {

// The seven slots: the five visible ones and a neighbour either side.
constexpr int slot_count = 7;

float along(const double seconds, const double from, const double length) {
  return static_cast<float>(std::clamp((seconds - from) / std::max(length, 1e-6), 0.0, 1.0));
}

// Grown to `scale` of its size about its centre (0x0bbdb7's anchor 0).
Sprite scaled(const Sprite &sprite, const float scale) {
  return scaled_sprite(sprite, scale, scale);
}

Sprite placed(Sprite sprite, const float x, const float y) {
  sprite.position = {x, y};
  return sprite;
}

} // namespace

KeepSvScreen::KeepSvScreen(TextureCache &textures, const GlyphFont &font, KeepSv logic,
                           const MessageBoxStyle &box, const std::string &from,
                           const FrameStyle &frame, const std::vector<MusicCredit> &credits,
                           QuestSave quest)
    : TownScreen(textures, font, logic.script(), from, frame, credits, std::move(quest)),
      logic_(std::move(logic)), notice_(box, logic_.script().message_text_at) {
  static_cast<void>(textures_.get(logic_.script().intro.frame.image));
  static_cast<void>(textures_.get(logic_.script().saved_time.font.comma.image));
}

void KeepSvScreen::tick(const InputFrame &input, const double tick_seconds) {
  logic_.update(tick_seconds, input);
  tick_frame(tick_seconds, logic_.faded_in());
  MessageBox::Kind kind = MessageBox::Kind::notice;
  if (logic_.view() == KeepSvView::question) {
    kind = MessageBox::Kind::question;
  } else if (logic_.view() == KeepSvView::popup) {
    kind = MessageBox::Kind::plain;
  }
  notice_.update(logic_.message(), kind, logic_.choice(), logic_.script().messages, tick_seconds);
}

std::vector<std::string> KeepSvScreen::take_sounds() {
  std::vector<std::string> sounds = logic_.take_sounds();
  for (std::string &name : notice_.take_sounds()) {
    sounds.push_back(std::move(name));
  }
  return sounds;
}

float KeepSvScreen::title_alpha() const {
  const KeepSvIntro &intro = logic_.script().intro;
  return along(logic_.intro_seconds(), intro.title_from, intro.title_seconds);
}

// 0x042b3a: the frame grows from its centre, then the title fades in.
void KeepSvScreen::draw_intro(DrawList &list) const {
  const KeepSvIntro &intro = logic_.script().intro;
  const float grown = along(logic_.intro_seconds(), 0.0, intro.frame_seconds);
  if (grown > 0.0F) {
    list.sprite(scaled(intro.frame, grown));
  }
  list.sprite(intro.title, title_alpha());
}

// A slot's card: its Scoobee's maker's, damaged, or empty in the visible
// slots past the end of the list; none for an unknown maker.
const ChoiceSprites *KeepSvScreen::card(const int index, const int slot) const {
  const KeepSvSlots &slots = logic_.script().slots;
  if (logic_.scoobee(index) == nullptr) {
    return slot >= 1 && slot <= slots.visible ? &slots.empty : nullptr;
  }
  if (logic_.damaged(index)) {
    return &slots.damaged;
  }
  const int maker = logic_.maker(index);
  if (maker < 0 || maker >= static_cast<int>(slots.maker_cards.size())) {
    return nullptr;
  }
  return &slots.maker_cards[static_cast<std::size_t>(maker)];
}

// 0x0426a4: the cards grow one after another and scroll along; the
// cursor's card is lit under the cursor frame, pulsing while the list takes
// input, and not drawn while it scrolls.
void KeepSvScreen::draw_slots(DrawList &list) const {
  const KeepSvScript &script = logic_.script();
  const KeepSvSlots &slots = script.slots;
  const KeepSvIntro &intro = script.intro;
  const int first = logic_.shown_first();
  const float offset = logic_.scroll_offset();
  const KeepSvCursor &cursor = logic_.cursor();
  for (int slot = 0; slot < slot_count; ++slot) {
    const float grown = along(logic_.intro_seconds(), intro.slots_from + intro.slot_stagger * slot,
                              intro.slot_grow_seconds);
    const ChoiceSprites *shown = card(first - 1 + slot, slot);
    if (grown <= 0.0F || shown == nullptr) {
      continue;
    }
    const float x = slots.first[0] + slots.step * static_cast<float>(slot) + offset;
    const float y = slots.first[1];
    list.sprite(scaled(placed(shown->normal, x, y), grown), slots.clip);
    if (slot != cursor.row + 1 || cursor.column != 0 || logic_.scrolling()) {
      continue;
    }
    Sprite lit = placed(shown->selected, x, y);
    if (logic_.view() != KeepSvView::list) {
      lit.animation = Animation::none;
    }
    list.sprite(scaled(lit, grown), slots.clip);
    list.sprite(scaled(placed(slots.cursor, x, y), grown), slots.clip);
  }
}

// The arrows fade in with the title, the big ones only while the list takes
// input and has rows that way; SAVE and EXIT lit while chosen.
void KeepSvScreen::draw_columns(DrawList &list) const {
  const KeepSvScript &script = logic_.script();
  const float alpha = title_alpha();
  list.sprite(script.arrows.left, alpha);
  list.sprite(script.arrows.right, alpha);
  if (logic_.view() == KeepSvView::list) {
    if (logic_.cursor().first > 0) {
      list.sprite(script.arrows.more_left, alpha);
    }
    if (logic_.cursor().first + script.slots.visible < logic_.count()) {
      list.sprite(script.arrows.more_right, alpha);
    }
  }
  list.sprite(script.save.normal);
  if (logic_.view() == KeepSvView::save) {
    list.sprite(script.save.selected);
  }
  list.sprite(script.exit.normal);
  if (logic_.view() == KeepSvView::exit) {
    list.sprite(script.exit.selected);
  }
}

// The chosen Scoobee's name, and the date and time it was kept.
void KeepSvScreen::draw_details(DrawList &list) const {
  if (!logic_.shows_details()) {
    return;
  }
  const KeepSvScript &script = logic_.script();
  const KeepSvDetails &details = script.details;
  const VsScoobee &chosen = *logic_.scoobee(logic_.chosen());
  list.text(
      details.layer,
      layout_run(font_, glyph_run(font_, chosen.name), details.name_at[0], details.name_at[1]),
      details.colour);
  if (const auto when = parse_saved_time(chosen.saved)) {
    for (const Sprite &sprite :
         saved_time_sprites(script.saved_time, details.date, details.time, *when)) {
      list.sprite(sprite);
    }
  }
}

// A popup's options over the message box once it is open (0x042b3a), the
// chosen one lit.
void KeepSvScreen::draw_popup(DrawList &list) const {
  const std::vector<KeepSvPopupOption> *options = logic_.popup();
  if (options == nullptr || !notice_.grown()) {
    return;
  }
  for (std::size_t index = 0; index < options->size(); ++index) {
    const ChoiceSprites &sprites = (*options)[index].sprites;
    list.sprite(sprites.normal);
    if (static_cast<int>(index) == logic_.choice()) {
      list.sprite(sprites.selected);
    }
  }
}

void KeepSvScreen::draw(const Viewport &viewport) {
  draw_town(viewport, logic_.seconds(), logic_.black_alpha(), [this](DrawList &list) {
    const KeepSvScript &script = logic_.script();
    draw_intro(list);
    draw_slots(list);
    draw_columns(list);
    draw_details(list);
    notice_.draw(list);
    draw_popup(list);
    list.ticker(script.help_style, glyph_run(font_, script.help_text), logic_.help_seconds());
  });
}

} // namespace phantomcrash
