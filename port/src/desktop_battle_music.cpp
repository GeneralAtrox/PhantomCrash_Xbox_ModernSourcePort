#include "desktop_battle_music.hpp"

#include "desktop_draw_list.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace phantomcrash {
namespace {

// The overshoot's offsets are one a frame at the original's 60 Hz.
constexpr double arrive_rate = 60.0;
// The discs across the top: one per place in a program.
constexpr int disc_count = music_program_length;

Sprite moved(Sprite sprite, const float dx, const float dy) {
  sprite.position[0] += dx;
  sprite.position[1] += dy;
  return sprite;
}

Sprite placed(Sprite sprite, const float x, const float y) {
  sprite.position = {x, y};
  return sprite;
}

// The sprite scaled by (sx, sy) about its centre.
Sprite scaled(Sprite sprite, const float sx, const float sy) {
  const float width = sprite.rect[2] - sprite.rect[0];
  const float height = sprite.rect[3] - sprite.rect[1];
  sprite.position[0] += width * (1.0F - sx) * 0.5F;
  sprite.position[1] += height * (1.0F - sy) * 0.5F;
  sprite.size = std::array{width * sx, height * sy};
  return sprite;
}

// From `from` at progress 0 to 1 at progress 1.
float grown(const float from, const float progress) {
  return from + (1.0F - from) * progress;
}

// Which panel a view shows on the left; views on one panel do not slide.
int panel_of(const BattleMusicView view) {
  switch (view) {
  case BattleMusicView::choose:
  case BattleMusicView::confirm_set:
    return 1;
  case BattleMusicView::stock:
    return 2;
  default:
    return 0;
  }
}

} // namespace

BattleMusicScreen::BattleMusicScreen(TextureCache &textures, const GlyphFont &font,
                                     BattleMusicScript script, const std::string &from,
                                     const FrameStyle &frame,
                                     const std::vector<MusicCredit> &credits, QuestSave quest)
    : TownScreen(textures, font, script, from, frame, credits, quest), number_font_(frame.hud.font),
      logic_(std::move(script), quest.music, credits, from) {
  static_cast<void>(textures_.get(logic_.script().menu_panel.image));
  static_cast<void>(textures_.get(logic_.script().back_hints.front().image));
}

void BattleMusicScreen::tick(const InputFrame &input, const double tick_seconds) {
  logic_.update(tick_seconds, input);
  tick_frame(tick_seconds, logic_.faded_in());
  if (logic_.done() && quest().music != logic_.music()) {
    QuestSave changed = quest();
    changed.music = logic_.music();
    set_quest(std::move(changed));
  }
}

void BattleMusicScreen::draw_track_number(DrawList &list, const int track, const float left,
                                          const float top, const int layer) const {
  NumberStyle style = logic_.script().discs.number;
  style.right = {0.0F, top};
  float width = 0.0F;
  for (const Sprite &digit : number_sprites_from(number_font_, style, left, track, layer, width)) {
    list.sprite(digit);
  }
}

// 0x36d6b: the tray opens and the twelve discs fan out from the centre as
// the screen opens; the browsed program's label pops into the tray's slot
// and its tracks show on the discs one after another.
void BattleMusicScreen::draw_discs(DrawList &list) const {
  const MusicDiscs &discs = logic_.script().discs;
  const double seconds = logic_.seconds();
  const float fan = static_cast<float>(std::min(1.0, seconds / discs.fan_seconds));
  const float half_width = (discs.disc.rect[2] - discs.disc.rect[0]) * 0.5F;
  const auto &program = logic_.music().programs[static_cast<std::size_t>(logic_.browsed())];
  const float opened = static_cast<float>(std::min(1.0, seconds / discs.tray_seconds));
  list.sprite(
      scaled(discs.tray, grown(discs.tray_from[0], opened), grown(discs.tray_from[1], opened)));
  if (seconds > discs.tray_seconds) {
    const float popped =
        static_cast<float>(std::min(1.0, (seconds - discs.tray_seconds) / discs.label_seconds));
    draw_program_label(list, grown(discs.label_from[0], popped),
                       grown(discs.label_from[1], popped));
  }
  const float middle = static_cast<float>(disc_count - 1) * 0.5F;
  for (int place = 0; place < disc_count; ++place) {
    const float centre = discs.centre + (static_cast<float>(place) - middle) * discs.step * fan;
    Sprite disc = placed(discs.disc, centre - half_width, discs.y);
    disc.layer = discs.first_layer - place;
    list.sprite(disc);
    const double shown = discs.fan_seconds + discs.number_stagger * place;
    if (std::cmp_less(place, program.size()) && seconds >= shown) {
      draw_track_number(list, program[static_cast<std::size_t>(place)],
                        disc.position[0] + discs.number_offset[0], discs.y + discs.number_offset[1],
                        disc.layer);
    }
  }
}

// 0x36aba: the browsed program's name in the tray's slot, in 18-pixel
// cells centred on name_centre, or its PROGRAM n label when it has none.
void BattleMusicScreen::draw_program_label(DrawList &list, const float sx, const float sy) const {
  const BattleMusicScript &script = logic_.script();
  const MusicDiscs &discs = script.discs;
  const auto browsed = static_cast<std::size_t>(logic_.browsed());
  const Sprite &label = script.program_lit[browsed];
  std::string name = logic_.music().names[browsed];
  name.erase(name.find_last_not_of(' ') + 1);
  if (name.empty()) {
    list.sprite(scaled(label, sx, sy));
    return;
  }
  const std::vector<int> run = glyph_run(font_, name);
  const float cell = font_.cell;
  const auto [x, y] = discs.name_centre;
  std::vector<GlyphQuad> glyphs = layout_cells(
      font_, run, x - cell * static_cast<float>(run.size()) * 0.5F, y - cell * 0.5F, cell);
  for (GlyphQuad &glyph : glyphs) {
    glyph.x = x + (glyph.x - x) * sx;
    glyph.y = y + (glyph.y - y) * sy;
    glyph.width *= sx;
    glyph.height *= sy;
  }
  list.text(label.layer, std::move(glyphs), discs.name_colour);
}

// 0x37d6f: the menu, its cursor over the chosen item and the sweep under
// it arriving with an overshoot, or EXIT lit.
void BattleMusicScreen::draw_menu(DrawList &list, const float offset) const {
  const BattleMusicScript &script = logic_.script();
  list.sprite(moved(script.menu_panel, offset, 0.0F));
  list.sprite(moved(script.exit, offset, 0.0F));
  if (logic_.on_exit()) {
    list.sprite(moved(script.exit_lit, offset, 0.0F));
    return;
  }
  const float dy = script.item_y[static_cast<std::size_t>(logic_.menu_cursor())];
  list.sprite(moved(script.menu_cursor, offset, dy));
  float overshoot = 0.0F;
  const auto frame = static_cast<std::size_t>(logic_.arrive_seconds() * arrive_rate);
  if (frame < script.arrive.size()) {
    overshoot = logic_.arrived_upwards() ? -script.arrive[frame] : script.arrive[frame];
  }
  list.sprite(moved(script.menu_sweep, offset, dy + overshoot));
}

// The television: its static, the scan band rolling down it, the bars
// flashing now and then and the chosen item's caption rising into place as
// it fades in.
void BattleMusicScreen::draw_television(DrawList &list) const {
  const BattleMusicScript &script = logic_.script();
  const MusicTelevision &tv = script.television;
  const double seconds = logic_.seconds();
  list.sprite(tv.frame);
  const auto flicker = static_cast<std::size_t>(seconds / (tv.static_period * 0.5));
  list.sprite(tv.static_frames[flicker % tv.static_frames.size()]);
  const double rolled = std::fmod(seconds, tv.roll_seconds) / tv.roll_seconds;
  list.sprite(moved(tv.roll, 0.0F, tv.roll_distance * static_cast<float>(rolled)));
  const auto bar = static_cast<std::size_t>(std::fmod(seconds, tv.bars_cycle) / tv.bars_seconds);
  if (bar < tv.bars.size()) {
    list.sprite(tv.bars[bar]);
  }
  if (logic_.on_exit()) {
    return;
  }
  std::string caption = script.items[static_cast<std::size_t>(logic_.menu_cursor())];
  if (logic_.view() == BattleMusicView::play_mode) {
    caption = logic_.mode_choice() == 1 ? "random" : "sequential";
  }
  const auto found = script.captions.find(caption);
  if (found == script.captions.end()) {
    return;
  }
  const float shown =
      static_cast<float>(std::min(1.0, logic_.arrive_seconds() / tv.caption_seconds));
  const Sprite text =
      placed(found->second, tv.caption_at[0], tv.caption_at[1] + tv.caption_rise * (1.0F - shown));
  list.sprite(text, shown);
}

void BattleMusicScreen::draw_programs(DrawList &list, const float offset,
                                      const bool outgoing) const {
  const BattleMusicScript &script = logic_.script();
  list.sprite(moved(script.program_panel, offset, 0.0F));
  // 0x36a51: the pulsing cursor on the browsed row.
  const float row_y = script.program_cursor_y[static_cast<std::size_t>(logic_.browsed())];
  list.sprite(placed(script.program_cursor, script.program_cursor.position[0] + offset, row_y));
  if (logic_.view() == BattleMusicView::confirm_set) {
    list.sprite(script.set_prompt);
  } else if (outgoing && logic_.view_before() == BattleMusicView::confirm_set) {
    list.sprite(script.set_ok); // the program is set, as it slides away
  }
  for (const Sprite &hint : script.back_hints) {
    list.sprite(hint);
  }
}

// 0x376d0: a page of the owned tracks, each numbered, NEW! on those not
// yet seen, the cursor or OK lit, the page arrows and the button hints.
void BattleMusicScreen::draw_stock(DrawList &list, const float offset) const {
  const BattleMusicScript &script = logic_.script();
  const MusicStock &stock = script.stock;
  for (int row = 0; row < stock.rows; ++row) {
    for (int column = 0; column < stock.columns; ++column) {
      const float x = stock.origin[0] + stock.step[0] * static_cast<float>(column) + offset;
      const float y = stock.origin[1] + stock.step[1] * static_cast<float>(row);
      // Every slot is drawn, an empty one without a number (0x376d0).
      list.sprite(placed(stock.slot, x, y));
      if (const int track = logic_.stock_track(logic_.page(), row * stock.columns + column);
          track >= 0) {
        draw_track_number(list, track, x + stock.number_offset[0], y + stock.number_offset[1],
                          stock.slot.layer);
        if (const auto owned = logic_.music().owned.find(track);
            owned != logic_.music().owned.end() && owned->second == TrackState::fresh) {
          list.sprite(placed(stock.fresh, x + stock.fresh_offset[0], y + stock.fresh_offset[1]));
        }
      }
      if (!logic_.on_ok() && row == logic_.row() && column == logic_.column()) {
        list.sprite(placed(stock.cursor, x, y));
      }
    }
  }
  list.sprite(moved(stock.ok, offset, 0.0F));
  if (logic_.on_ok()) {
    list.sprite(moved(stock.ok_lit, offset, 0.0F));
  }
  if (logic_.pages() > 1) {
    for (const Sprite &arrow : stock.arrows) {
      list.sprite(moved(arrow, offset, 0.0F));
    }
  }
  for (const Sprite &hint : stock.hints) {
    list.sprite(moved(hint, offset, 0.0F));
  }
  // STOCK pops in once the panel has settled; it stays while the panel
  // slides out.
  const double since = logic_.view() == BattleMusicView::stock ? logic_.view_seconds() : 1.0e9;
  if (stock.heading && since > stock.heading_seconds) {
    const auto popped =
        static_cast<float>(std::min(1.0, (since - stock.heading_seconds) / stock.heading_grow));
    list.sprite(scaled(*stock.heading, grown(stock.heading_from[0], popped),
                       grown(stock.heading_from[1], popped)));
  }
  // The track's CD jacket in its frame (0x376d0).
  list.sprite(stock.portrait_frame);
  const int track = logic_.cursor_track();
  if (track >= 0 && logic_.credits()[static_cast<std::size_t>(track)].jacket) {
    Sprite jacket = *logic_.credits()[static_cast<std::size_t>(track)].jacket;
    jacket.position = stock.portrait_at;
    jacket.layer = stock.portrait_frame.layer + 1;
    list.sprite(jacket);
  }
}

void BattleMusicScreen::draw_mode(DrawList &list) const {
  const BattleMusicScript &script = logic_.script();
  list.sprite(script.mode_panel);
  list.sprite(
      moved(script.mode_ring, script.mode_step * static_cast<float>(logic_.mode_choice()), 0.0F));
  for (const Sprite &hint : script.cancel_hints) {
    list.sprite(hint);
  }
}

void BattleMusicScreen::draw_panel(DrawList &list, const BattleMusicView view, const float offset,
                                   const bool outgoing) const {
  switch (panel_of(view)) {
  case 1:
    draw_programs(list, offset, outgoing);
    break;
  case 2:
    draw_stock(list, offset);
    break;
  default:
    draw_menu(list, offset);
    break;
  }
}

void BattleMusicScreen::draw(const Viewport &viewport) {
  draw_town(viewport, logic_.seconds(), logic_.black_alpha(), [this](DrawList &list) {
    const BattleMusicScript &script = logic_.script();
    draw_discs(list);
    // 0x383c7: the old panel slides out to the left over the first half of
    // the slide, the new one in from the left over the second.
    const double half = script.slide_seconds * 0.5;
    const double t = logic_.view_seconds();
    const bool sliding =
        panel_of(logic_.view()) != panel_of(logic_.view_before()) && t < script.slide_seconds;
    if (sliding && t < half) {
      draw_panel(list, logic_.view_before(), -script.slide_distance * static_cast<float>(t / half),
                 true);
    } else if (sliding) {
      draw_panel(list, logic_.view(),
                 -script.slide_distance * static_cast<float>(1.0 - (t - half) / half), false);
    } else {
      draw_panel(list, logic_.view(), 0.0F, false);
    }
    if (logic_.view() == BattleMusicView::play_mode) {
      draw_mode(list);
    }
    if (logic_.view() != BattleMusicView::stock) {
      draw_television(list);
    }
    HelpLine line{logic_.help_text(), script.help.trail};
    if (logic_.cursor_track() >= 0) {
      line.trail = script.track_trail;
    }
    list.ticker(help_style(script.help_style, line), glyph_run(font_, line.text),
                logic_.help_seconds());
  });
}

} // namespace phantomcrash
