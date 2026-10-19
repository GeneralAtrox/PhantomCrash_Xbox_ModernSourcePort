#include "desktop_sonic_amp.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_event_text.hpp"
#include "desktop_shop_dialog.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace phantomcrash {
namespace {

Sprite moved(Sprite sprite, const float dx, const float dy) {
  sprite.position[0] += dx;
  sprite.position[1] += dy;
  return sprite;
}

Sprite placed(Sprite sprite, const float x, const float y) {
  sprite.position = {x, y};
  return sprite;
}

std::string replaced(std::string text, const std::string &key, const std::string &value) {
  if (const auto at = text.find(key); at != std::string::npos) {
    text.replace(at, key.size(), value);
  }
  return text;
}

} // namespace

SonicAmpScreen::SonicAmpScreen(TextureCache &textures, const GlyphFont &font, SonicAmpScript script,
                               const std::string &from, const FrameStyle &frame,
                               const std::vector<MusicCredit> &credits, QuestSave quest)
    : TownScreen(textures, font, script, from, frame, credits, quest), number_font_(frame.hud.font),
      logic_(std::move(script), quest.music, quest.money, credits, from) {
  static_cast<void>(textures_.get(logic_.script().chart.ring.image));
  static_cast<void>(textures_.get(logic_.script().dialog.window.image));
  profiles_.resize(credits.size());
  int loaded = 0;
  for (std::size_t i = 0; i < credits.size(); ++i) {
    if (const auto &profile = credits[i].artist_profile) {
      EventText &text = profiles_[i];
      text.tokens = parse_event_text(font_, profile->text);
      text.position = profile->position;
      text.colour = profile->colour;
      text.layer = profile->layer;
      ++loaded;
    }
  }
  SDL_Log("Sonic Amp: loaded %d artist profiles for %zu tracks", loaded, credits.size());
}

void SonicAmpScreen::tick(const InputFrame &input, const double tick_seconds) {
  const SonicAmpView before = logic_.view();
  logic_.update(tick_seconds, input);
  if (before != logic_.view()) {
    SDL_Log("Sonic Amp: view=%d->%d mouse=%d", int(before), int(logic_.view()), input.clicked);
  }
  if (before != logic_.view() && logic_.view() == SonicAmpView::preview) {
    const int selected = logic_.cursor_track();
    if (const MusicCredit *track = credit(selected)) {
      SDL_Log("Artist profile: track=%d artist=%s text_units=%d", selected, track->artist.c_str(),
              text_units(profiles_[static_cast<std::size_t>(selected)].tokens));
    }
  }
  tick_frame(tick_seconds, logic_.faded_in());
  if (quest().money != logic_.money() || quest().music != logic_.music()) {
    QuestSave changed = quest();
    changed.money = logic_.money();
    changed.music = logic_.music();
    set_quest(std::move(changed));
  }
}

const MusicCredit *SonicAmpScreen::credit(const int track) const {
  const auto &credits = logic_.credits();
  return track >= 0 && track < static_cast<int>(credits.size())
             ? &credits[static_cast<std::size_t>(track)]
             : nullptr;
}

void SonicAmpScreen::draw_number(DrawList &list, const int value, const float left, const float top,
                                 const int layer) const {
  NumberStyle style = logic_.script().grid.page_number;
  style.right = {0.0F, top};
  float width = 0.0F;
  for (const Sprite &digit : number_sprites_from(number_font_, style, left, value, layer, width)) {
    list.sprite(digit);
  }
}

// 0x2b1ec: the icons along the bottom, the chosen one lit, and over the
// catalogues the bouncing arrows and "Be sure to also watch these!".
void SonicAmpScreen::draw_menu(DrawList &list) const {
  const SonicAmpScript &script = logic_.script();
  for (std::size_t i = 0; i < script.items.size(); ++i) {
    list.sprite(script.items[i].sprite);
    if (static_cast<int>(i) == logic_.menu_cursor()) {
      list.sprite(script.items[i].lit);
    }
  }
  if (logic_.view() != SonicAmpView::menu) {
    return;
  }
  const double bounce = std::fmod(logic_.seconds(), script.arrow_seconds) / script.arrow_seconds;
  for (const Sprite &arrow : script.arrows) {
    list.sprite(moved(arrow, 0.0F, script.arrow_drop * static_cast<float>(bounce)),
                static_cast<float>(1.0 - bounce));
  }
  const double pulse =
      std::fmod(logic_.seconds(), script.arrow_seconds * 2.0) / script.arrow_seconds;
  list.sprite(script.watch, static_cast<float>(std::abs(pulse - 1.0)));
}

// 0x29de7: the television's static, tint and rolling scan band, the rank's
// jacket fading in and out, the ring's segments for the ranks shown so far
// and the rank, title and artist beside it.
void SonicAmpScreen::draw_chart(DrawList &list) const {
  const SonicAmpChart &chart = logic_.script().chart;
  const double seconds = logic_.seconds();
  list.sprite(chart.ring);
  const auto flicker = static_cast<std::size_t>(seconds / (chart.static_period * 0.5));
  list.sprite(chart.static_frames[flicker % chart.static_frames.size()]);
  list.sprite(chart.tint, chart.tint_alpha);
  const double rolled = std::fmod(seconds, chart.scan_seconds) / chart.scan_seconds;
  list.sprite(moved(chart.scan, 0.0F, chart.scan_distance * static_cast<float>(rolled)),
              chart.scan_alpha);
  const int rank = logic_.rank();
  for (int k = rank; k < static_cast<int>(chart.segments.size()); ++k) {
    Sprite segment = chart.segments[static_cast<std::size_t>(k)];
    segment.colour = k == rank ? chart.segment_lit.colour : chart.segment_colour;
    segment.animation = k == rank ? chart.segment_lit.animation : segment.animation;
    list.sprite(segment);
  }
  const MusicCredit *shown = credit(logic_.chart_track(rank));
  if (shown == nullptr) {
    return;
  }
  if (shown->jacket) {
    const double t = logic_.rank_seconds();
    const double alpha = std::min({1.0, t / chart.fade_in, (chart.seconds - t) / chart.fade_out});
    Sprite jacket = *shown->jacket;
    const float width = (jacket.rect[2] - jacket.rect[0]) * chart.jacket_scale;
    const float height = (jacket.rect[3] - jacket.rect[1]) * chart.jacket_scale;
    jacket.size = std::array<float, 2>{width, height};
    jacket.position = chart.jacket_centre;
    jacket.centered = true;
    jacket.layer = chart.jacket_layer;
    list.sprite(jacket, static_cast<float>(std::max(0.0, alpha)));
  }
  char number[8];
  std::snprintf(number, sizeof number, "%02d", rank + 1);
  const std::string line =
      replaced(replaced(chart.rank_format, "{rank}", number), "{title}", shown->title);
  list.ticker(chart.rank_style, glyph_run(font_, line), logic_.rank_seconds());
  list.ticker(chart.artist_style, glyph_run(font_, shown->artist), logic_.rank_seconds());
}

// 0x2ad11, 0x2a9f0: the grid's band, the jackets on their shadows, SOLD
// OUT! on the tracks owned and the cursor.
void SonicAmpScreen::draw_grid(DrawList &list) const {
  const SonicAmpGrid &grid = logic_.script().grid;
  const auto band = static_cast<float>(std::min(1.0, logic_.grid_seconds() / grid.band_seconds));
  list.sprite(grid.band, band);
  std::array<float, 2> origin = grid.spot_origin;
  if (logic_.spot()) {
    list.sprite(grid.spot_badge);
  } else {
    origin = grid.catalogue_origin;
    origin[1] += grid.short_page_offset * static_cast<float>(grid.catalogue_rows - logic_.rows());
    draw_pages(list);
  }
  for (int row = 0; row < logic_.rows(); ++row) {
    for (int column = 0; column < logic_.columns(); ++column) {
      const MusicCredit *track = credit(logic_.slot_track(row, column));
      if (track == nullptr) {
        continue;
      }
      const float x = origin[0] + grid.step[0] * static_cast<float>(column);
      const float y = origin[1] + grid.step[1] * static_cast<float>(row);
      list.sprite(moved(grid.shadow, x, y));
      if (track->jacket_small) {
        Sprite jacket = placed(*track->jacket_small, x, y);
        jacket.layer = grid.jacket_layer;
        list.sprite(jacket);
      }
      if (logic_.music().owns(logic_.slot_track(row, column))) {
        list.sprite(placed(grid.sold_out, x, y));
      }
      if (row == logic_.row() && column == logic_.column()) {
        list.sprite(placed(grid.cursor, x + grid.cursor_offset[0], y + grid.cursor_offset[1]));
      }
    }
  }
  for (const Sprite &hint : grid.hints) {
    list.sprite(hint);
  }
}

// A catalogue's page of how many, and its arrows.
void SonicAmpScreen::draw_pages(DrawList &list) const {
  const SonicAmpGrid &grid = logic_.script().grid;
  list.sprite(grid.page_box);
  const int layer = grid.page_box.layer + 1;
  draw_number(list, logic_.page() + 1, grid.page_number.right[0], grid.page_number.right[1], layer);
  list.sprite(grid.page_slash);
  draw_number(list, logic_.pages(), grid.total_at[0], grid.total_at[1], layer);
  if (logic_.pages() > 1) {
    for (const Sprite &arrow : grid.page_arrows) {
      list.sprite(arrow);
    }
  }
}

void SonicAmpScreen::draw_actions(DrawList &list) const {
  const auto &actions = logic_.script().actions;
  for (std::size_t i = 0; i < actions.size(); ++i) {
    list.sprite(static_cast<int>(i) == logic_.action() ? actions[i].lit : actions[i].sprite);
  }
}

// 0x29fdc: the track's panel with its large jacket, the artist's name and
// web address, with the original profile text script (credit +0x1a0).
void SonicAmpScreen::draw_preview(DrawList &list) const {
  const SonicAmpScript &script = logic_.script();
  for (const Sprite &layer : script.preview_layers) {
    list.sprite(layer);
  }
  if (const MusicCredit *track = credit(logic_.cursor_track()); track != nullptr) {
    const auto show = [&](const std::optional<Sprite> &sprite, const std::array<float, 2> &at) {
      if (sprite) {
        Sprite shown = placed(*sprite, at[0], at[1]);
        shown.layer = script.preview_layer;
        list.sprite(shown);
      }
    };
    show(track->jacket, script.preview_jacket);
    show(track->artist_name, script.preview_name);
    show(track->artist_link, script.preview_link);
    const EventText &profile = profiles_[static_cast<std::size_t>(logic_.cursor_track())];
    draw_event_text(list, font_, {&profile, -1}, logic_.seconds(), {});
  }
  for (const Sprite &hint : script.preview_hints) {
    list.sprite(hint);
  }
}

void SonicAmpScreen::draw(const Viewport &viewport) {
  draw_town(viewport, logic_.seconds(), logic_.black_alpha(), [this](DrawList &list) {
    const SonicAmpScript &script = logic_.script();
    const SonicAmpView view = logic_.view();
    if (view == SonicAmpView::preview) {
      draw_preview(list);
    } else {
      draw_menu(list);
      draw_chart(list);
      if (view != SonicAmpView::menu) {
        draw_grid(list);
      }
      if (view == SonicAmpView::actions) {
        draw_actions(list);
      }
      draw_shop_dialog(list, font_, script.dialog, logic_.saying(), script.price);
    }
    HelpLine line{logic_.help_text(), script.help.trail};
    if (view != SonicAmpView::menu) {
      line.trail = script.track_trail;
    }
    list.ticker(help_style(script.help_style, line), glyph_run(font_, line.text),
                logic_.help_seconds());
  });
}

} // namespace phantomcrash
