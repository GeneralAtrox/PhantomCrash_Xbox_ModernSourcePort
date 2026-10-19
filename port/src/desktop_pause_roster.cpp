#include "desktop_pause_menu.hpp"
#include "desktop_draw_list.hpp"
#include "phantomcrash/json_fields.hpp"

#include <algorithm>

namespace phantomcrash {

void PauseMenu::load_roster(const json::JsonValue &data) {
  const auto *value = data.find("roster");
  if (!value) {
    return;
  }
  auto &style = roster_layout_;
  style.at = json::numbers<2>(json::required(*value, "at"), "at");
  const auto number = [&](const char *name, const float fallback) {
    return static_cast<float>(value->number_or(name, fallback));
  };
  style.column_width = number("column_width", style.column_width);
  style.row_height = number("row_height", style.row_height);
  style.scale = number("scale", style.scale);
  style.skull_pixel = number("skull_pixel", style.skull_pixel);
  style.title_y = number("title_y", style.title_y);
  style.summary_y = number("summary_y", style.summary_y);
  style.footer_y = number("footer_y", style.footer_y);
  style.button_y = number("button_y", style.button_y);
  style.help_y = number("help_y", style.help_y);
  style.boss_y = number("boss_y", style.boss_y);
  style.heading_y = number("heading_y", style.heading_y);
  style.boss_scale = number("boss_scale", style.boss_scale);
  style.rows = std::max(1, static_cast<int>(value->number_or("rows", style.rows)));
  style.summary = value->string_or("summary", "Opponents remaining before boss: ");
  style.cleared = value->string_or("cleared", "Roster cleared - boss unlocked");
  style.paging = value->string_or("paging", "UP / DOWN: Roster page");
  style.opponents = value->string_or("opponents", style.opponents);
  style.leaders = value->string_or("leaders", style.leaders);
  style.boss = value->string_or("boss", style.boss);
  const auto &colours = json::required(*value, "colours").array();
  for (std::size_t i = 0; i < style.colours.size() && i < colours.size(); ++i) {
    style.colours[i] = parse_colour(colours[i].string());
  }
  for (const auto &row : json::required(*value, "skull").array()) {
    style.skull.push_back(row.string());
  }
}

std::size_t PauseMenu::roster_pages() const {
  const auto leaders = std::ranges::count(roster_, RosterSource::leader, &RosterStatus::source);
  const auto count = std::max(static_cast<std::size_t>(leaders), roster_.size() - leaders);
  const auto rows = static_cast<std::size_t>(roster_layout_.rows);
  return std::max(std::size_t{1}, (count + rows - 1) / rows);
}

void PauseMenu::set_roster(std::vector<RosterStatus> roster) {
  champion_.reset();
  for (const auto &entry : roster) {
    if (entry.source == RosterSource::champion) {
      champion_ = entry;
      break;
    }
  }
  std::erase_if(roster, [](const auto &entry) { return entry.source == RosterSource::champion; });
  roster_ = std::move(roster);
  roster_page_ = std::min(roster_page_, roster_pages() - 1);
}

void PauseMenu::draw_roster(DrawList &list, const GlyphFont &font, SDL_Renderer *renderer,
                            const Viewport &viewport) const {
  if (!showing_roster()) {
    return;
  }
  const auto &style = roster_layout_;
  const auto text = [&](const std::string &label, const float x, const float y,
                        const std::uint32_t colour, const float width, const float requested_scale,
                        const bool bold = false) {
    const auto run = glyph_run(font, label);
    const float scale = std::min(requested_scale, width / std::max(1.0F, run_width(font, run)));
    auto glyphs = layout_run(font, run, 0, 0);
    for (auto &glyph : glyphs) {
      glyph.x = x + glyph.x * scale;
      glyph.y = y + glyph.y * scale;
      glyph.width *= scale;
      glyph.height *= scale;
    }
    if (bold) {
      list.text(layer_ + 1, glyphs, colour);
      for (auto &glyph : glyphs) {
        glyph.x += 0.6F;
      }
    }
    list.text(layer_ + 1, std::move(glyphs), colour);
  };
  std::vector<SDL_FRect> skull_pixels;
  const auto skull = [&](const float x, const float y) {
    for (std::size_t sy = 0; sy < style.skull.size(); ++sy) {
      for (std::size_t sx = 0; sx < style.skull[sy].size(); ++sx) {
        if (style.skull[sy][sx] == '1') {
          const auto pixel = viewport.to_pixels(x + static_cast<float>(sx) * style.skull_pixel,
                                                y + static_cast<float>(sy) * style.skull_pixel,
                                                style.skull_pixel, style.skull_pixel);
          skull_pixels.push_back({pixel.x, pixel.y, pixel.width, pixel.height});
        }
      }
    }
  };
  const auto remaining = std::ranges::count(roster_, false, &RosterStatus::eliminated);
  const std::string heading =
      remaining > 0 ? style.summary + std::to_string(remaining) : style.cleared;
  text(heading, style.at[0], style.summary_y, title_.colour, style.column_width * 2, style.scale);
  if (champion_) {
    text(style.boss + ": " + champion_->name, style.at[0] + style.row_height, style.boss_y,
         style.colours[static_cast<std::size_t>(RosterSource::champion)],
         style.column_width * 2 - style.row_height, style.boss_scale, true);
    if (champion_->eliminated) {
      skull(style.at[0], style.boss_y);
    }
  }
  std::array<std::vector<const RosterStatus *>, 2> groups;
  for (const auto &entry : roster_) {
    groups[entry.source == RosterSource::leader ? 1 : 0].push_back(&entry);
  }
  const auto rows = static_cast<std::size_t>(style.rows);
  for (std::size_t column = 0; column < groups.size(); ++column) {
    const float x = style.at[0] + static_cast<float>(column) * style.column_width;
    text(column == 0 ? style.opponents : style.leaders, x, style.heading_y, title_.colour,
         style.column_width, style.scale, true);
    const auto &group = groups[column];
    const auto pages = std::max(std::size_t{1}, (group.size() + rows - 1) / rows);
    const auto first = std::min(roster_page_, pages - 1) * rows;
    for (std::size_t i = first; i < std::min(first + rows, group.size()); ++i) {
      const auto &entry = *group[i];
      const float y = style.at[1] + static_cast<float>(i - first) * style.row_height;
      text(entry.name, x + style.row_height, y,
           style.colours[static_cast<std::size_t>(entry.source)],
           style.column_width - style.row_height, style.scale);
      if (entry.eliminated) {
        skull(x, y);
      }
    }
  }
  list.custom(layer_ + 1, [renderer, pixels = std::move(skull_pixels)] {
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    if (!pixels.empty()) {
      SDL_RenderFillRects(renderer, pixels.data(), static_cast<int>(pixels.size()));
    }
  });
  const auto pages = roster_pages();
  if (pages > 1) {
    text(style.paging + "  " + std::to_string(roster_page_ + 1) + "/" + std::to_string(pages),
         style.at[0], style.footer_y, help_.colour, style.column_width * 2, style.scale);
  }
}

} // namespace phantomcrash
