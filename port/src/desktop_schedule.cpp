#include "desktop_schedule.hpp"

#include "desktop_contest_info.hpp"
#include "desktop_draw_list.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {

constexpr std::uint32_t lit_colour = 0xffffff;

// Folds a sprite about its centre to `scale_x` of its width.
void fold(Sprite &sprite, const float scale_x) {
  const float width = sprite.rect[2] - sprite.rect[0];
  const float height = sprite.rect[3] - sprite.rect[1];
  sprite.size = std::array<float, 2>{width * scale_x, height};
  sprite.position[0] += width * (1.0F - scale_x) * 0.5F;
}

Sprite placed(Sprite sprite, const float x, const float y) {
  sprite.position = {x, y};
  return sprite;
}

} // namespace

ScheduleScreen::ScheduleScreen(TextureCache &textures, const GlyphFont &font, ScheduleScript script,
                               const ContestTables &contests, const std::string &from,
                               const std::string &back, const FrameStyle &frame,
                               const std::vector<MusicCredit> &credits, QuestSave quest,
                               const MessageBoxStyle &box)
    : TownScreen(textures, font, script, from, frame, credits, quest), contests_(contests),
      fee_font_(frame.hud.font), schedule_(std::move(script), contests, quest, back, from, box) {
  const ScheduleScript &data = schedule_.script();
  static_cast<void>(textures_.get(data.cell_image));
  static_cast<void>(textures_.get(data.day_digits.image));
  static_cast<void>(textures_.get(data.info.fee_unit.image));
}

void ScheduleScreen::tick(const InputFrame &input, const double tick_seconds) {
  schedule_.update(tick_seconds, input);
  tick_frame(tick_seconds, schedule_.faded_in());
}

std::array<float, 2> ScheduleScreen::cell(const int column, const int row) const {
  const ScheduleScript &data = schedule_.script();
  return {data.cell_origin[0] + data.cell_step[0] * static_cast<float>(column),
          data.cell_origin[1] + data.cell_step[1] * static_cast<float>(row)};
}

// 0x35a9f: each cell's venue tile (day, night or closed; lit in the
// cursor's column), the contest class's badge and the CLEAR of a contest won.
void ScheduleScreen::draw_cells(DrawList &list) const {
  const ScheduleScript &data = schedule_.script();
  for (int column = 0; column < data.columns; ++column) {
    const ScheduleColumn view = schedule_.column_view(column);
    if (!view.shown) {
      continue;
    }
    const bool lit = column == schedule_.column();
    for (int row = 0; row < schedule_venues; ++row) {
      const auto [x, y] = cell(column, row);
      const float left = x + view.offset_x;
      const int contest = schedule_.contest_at(view.day, row);
      const Contest *entry = contests_.contest(contest);
      const ScheduleVenueTiles &tiles = data.venues[static_cast<std::size_t>(row)];
      Sprite tile;
      tile.image = data.cell_image;
      tile.position = {left, y};
      tile.layer = data.tile_layer;
      tile.rect = lit ? data.closed_lit : data.closed;
      if (entry != nullptr) {
        tile.rect = entry->night ? (lit ? tiles.night_lit : tiles.night)
                                 : (lit ? tiles.day_lit : tiles.day);
      }
      fold(tile, view.scale_x);
      list.sprite(tile);
      if (entry == nullptr) {
        continue;
      }
      const auto badge = std::ranges::find_if(data.badges, [&](const ScheduleBadge &candidate) {
        return candidate.classes.empty() ||
               std::ranges::find(candidate.classes, entry->contest_class) !=
                   candidate.classes.end();
      });
      if (badge != data.badges.end()) {
        Sprite mark = tile;
        mark.rect = badge->rect;
        mark.position = {left, y};
        mark.layer = data.badge_layer;
        mark.colour = lit && !schedule_.scrolling() ? lit_colour : data.badge_dim;
        fold(mark, view.scale_x);
        list.sprite(mark);
      }
      if (schedule_.won(contest) &&
          std::ranges::find(data.no_clear_mark, contest) == data.no_clear_mark.end()) {
        Sprite clear = placed(data.clear, left, y);
        fold(clear, view.scale_x);
        list.sprite(clear);
      }
    }
  }
}

// 0x359ec: the day numbers above the columns, the days scrolled to.
void ScheduleScreen::draw_days(DrawList &list) const {
  const ScheduleScript &data = schedule_.script();
  const ScheduleDigits &digits = data.day_digits;
  for (int column = 0; column < data.columns; ++column) {
    const int day = (schedule_.first_day() + column) % calendar_days + 1;
    const float step = data.cell_step[0] * static_cast<float>(column);
    const std::array<std::pair<std::array<float, 2>, int>, 2> places{
        {{digits.tens, day / 10}, {digits.units, day % 10}}};
    for (const auto &[place, digit] : places) {
      Sprite sprite;
      sprite.image = digits.image;
      sprite.rect = digits.digits[static_cast<std::size_t>(digit)];
      sprite.position = {place[0] + step, place[1]};
      sprite.layer = digits.layer;
      list.sprite(sprite);
    }
  }
}

void ScheduleScreen::draw_cursor(DrawList &list) const {
  const ScheduleScript &data = schedule_.script();
  list.sprite(data.exit);
  if (schedule_.row() == Schedule::exit_row && schedule_.button() == Schedule::exit_button) {
    list.sprite(placed(data.exit_selected, data.exit.position[0], data.exit.position[1]));
  } else if (schedule_.row() == Schedule::exit_row) {
    // SKIP TO DAY keeps the day it wakes on marked.
    const int column = schedule_.column();
    list.sprite(placed(data.cursor_day, cell(column, 0)[0], data.cursor_day_y));
  } else if (!schedule_.popup().is_open() && !schedule_.scrolling()) {
    const auto [x, y] = cell(schedule_.column(), schedule_.row());
    list.sprite(placed(data.cursor_frame, x, y));
    list.sprite(placed(data.cursor_pulse, x, y));
    list.sprite(placed(data.cursor_day, x, data.cursor_day_y));
  }
  if (schedule_.scrolling()) {
    // The original lights the left arrow; the port lights the side the
    // calendar scrolls towards.
    Sprite arrow = data.scroll_arrow;
    if (schedule_.scroll_direction() > 0) {
      const float width = arrow.rect[2] - arrow.rect[0];
      arrow.flip_x = true;
      arrow.position[0] = canvas_width - arrow.position[0] - width;
    }
    list.sprite(arrow);
  }
}

// 0x01469d: the contest's details, grown out of its cell.
void ScheduleScreen::draw_popup(DrawList &list) const {
  const ScheduleScript &data = schedule_.script();
  const ContestInfoStyle &info = data.info;
  const auto [x, y] = cell(schedule_.column(), schedule_.row());
  const std::array<float, 2> centre{x + data.cell_step[0] * 0.5F, y + data.cell_step[1] * 0.5F};
  list.sprite(grown_panel(info.panel, schedule_.popup(), centre));
  if (schedule_.popup().grown()) {
    draw_contest_info(list, font_, fee_font_, info, contests_, schedule_.selected_contest());
  }
}

void ScheduleScreen::draw_skip(DrawList &list) const {
  const DaySkipScript &skip = schedule_.script().skip;
  if (skip.enabled && schedule_.quest().cleared) {
    const EnemyLevelTag &tag = skip.tag;
    list.label(tag.layer, enemy_level_text(tag, schedule_.quest().laps), tag.font, tag.box,
               tag.colour, tag.shadow, tag.shadow_blur);
  }
  if (!schedule_.skip_offered()) {
    return;
  }
  list.sprite(skip.button);
  if (schedule_.row() == Schedule::exit_row && schedule_.button() == Schedule::skip_button) {
    list.sprite(placed(skip.button_selected, skip.button.position[0], skip.button.position[1]));
  }
  if (schedule_.box().is_open()) {
    list.message_box(schedule_.box());
  }
}

void ScheduleScreen::draw(const Viewport &viewport) {
  draw_town(viewport, schedule_.seconds(), schedule_.black_alpha(), [this](DrawList &list) {
    draw_cells(list);
    draw_days(list);
    draw_cursor(list);
    if (schedule_.popup().is_open()) {
      draw_popup(list);
    }
    draw_skip(list);
    const Contest *contest = contests_.contest(schedule_.selected_contest());
    if (contest != nullptr && !schedule_.scrolling()) {
      list.ticker(schedule_.script().ticker, glyph_run(font_, contest->name),
                  schedule_.ticker_seconds());
    }
  });
}

} // namespace phantomcrash
