#include "desktop_records.hpp"

#include "desktop_draw_list.hpp"

#include <utility>

namespace phantomcrash {
namespace {

// The labels and values are drawn above the page's panel (0x32f1e).
constexpr int record_text_layer = 0x108;

} // namespace

RecordsScreen::RecordsScreen(TextureCache &textures, const GlyphFont &font, RecordsScript script,
                             const ContestTables &contests, const StoryProgress progress,
                             const std::string &from, const std::string &back,
                             const FrameStyle &frame, const std::vector<MusicCredit> &credits,
                             QuestSave quest, WorldRenderer *world)
    : TownScreen(textures, font, script, from, frame, credits, quest), contests_(contests),
      records_(std::move(script), contests, std::move(quest), progress, back) {
  const RecordsScript &data = records_.script();
  static_cast<void>(textures_.get(data.menu.panel.image));
  static_cast<void>(textures_.get(data.convention.hint.front().image));
  help_run_ = glyph_run(font_, data.help_text);
  if (data.trophies) {
    trophies_.emplace(world, textures_.renderer(), *data.trophies);
  }
}

void RecordsScreen::tick(const InputFrame &input, const double tick_seconds) {
  records_.update(tick_seconds, input);
  if (trophies_) {
    trophies_->tick(tick_seconds);
  }
  tick_frame(tick_seconds, records_.faded_in());
}

void RecordsScreen::draw_text(DrawList &list, const std::string &text, const float x, const float y,
                              const int layer, const std::uint32_t colour, const float alpha,
                              const bool right) const {
  const std::vector<int> run = glyph_run(font_, text);
  const float left = right ? x - run_width(font_, run) : x;
  list.text(layer, layout_run(font_, run, left, y), colour, std::nullopt, alpha);
}

// 0x3360f: the panel and items, the cursor's item lit; behind a page they
// show faintly (0x33aed).
void RecordsScreen::draw_menu(DrawList &list, const bool behind) const {
  const RecordsMenu &menu = records_.script().menu;
  const float alpha = behind ? menu.behind_alpha : 1.0F;
  Sprite panel = menu.panel;
  panel.layer = behind ? menu.behind_panel_layer : panel.layer;
  list.sprite(panel, alpha);
  for (int item = 0; item < static_cast<int>(menu.items.size()); ++item) {
    Sprite label = menu.items[static_cast<std::size_t>(item)].label;
    label.layer = behind ? menu.behind_items_layer : label.layer;
    list.sprite(label, alpha);
    if (item == records_.menu_cursor() && records_.view() == RecordsView::menu) {
      Sprite lit = label;
      lit.colour = menu.lit_colour;
      lit.layer = menu.lit_layer;
      lit.animation = Animation::pulse;
      list.sprite(lit);
    }
  }
}

void RecordsScreen::draw_row(DrawList &list, const RecordPage &page, const int row) const {
  const RecordsScript &data = records_.script();
  const RowLook look = row_look(data.reveal, row, records_.view_seconds());
  if (!look.shown) {
    return;
  }
  const float y = page.first_y + page.step_y * static_cast<float>(row);
  if (look.bar) {
    Sprite bar = data.reveal.bar;
    const float width = bar.rect[2] - bar.rect[0];
    bar.size = std::array<float, 2>{width * look.bar_scale, bar.rect[3] - bar.rect[1]};
    bar.position = {data.reveal.bar_x + width * (1.0F - look.bar_scale) * 0.5F,
                    y - data.reveal.bar_above};
    list.sprite(bar, look.bar_alpha);
  }
  const RecordRow &entry = page.rows[static_cast<std::size_t>(row)];
  draw_text(list, entry.label, page.label_x, y, record_text_layer, look.colour, look.alpha);
  const RecordNumbers &numbers = data.numbers;
  const RecordText text = record_text(entry.format, records_.value(entry), numbers);
  if (entry.format == RecordFormat::time) {
    draw_text(list, text.digits, numbers.time_left, y, record_text_layer, look.colour, look.alpha);
    return;
  }
  // The unit hangs past the right edge the digits end at (0x331c0).
  draw_text(list, text.digits, numbers.right, y, record_text_layer, look.colour, look.alpha, true);
  if (!text.unit.empty()) {
    draw_text(list, text.unit, numbers.right, y, record_text_layer, look.colour, look.alpha);
  }
}

// 0x33d99 and 0x34123: the panel grows in, then the rows appear in turn.
void RecordsScreen::draw_page(DrawList &list) const {
  const RecordPage *page = records_.page();
  if (page == nullptr) {
    return;
  }
  draw_menu(list, true);
  const float growing = growing_after(page->growth, records_.view_seconds());
  list.sprite(grown_panel(page->panel, growing, page->growth.from_scale));
  if (growing > 0.0F) {
    return;
  }
  for (const Sprite &hint : page->hint) {
    list.sprite(hint);
  }
  for (int row = 0; row < static_cast<int>(page->rows.size()); ++row) {
    draw_row(list, *page, row);
  }
}

void RecordsScreen::draw(const Viewport &viewport) {
  draw_town(viewport, records_.seconds(), records_.black_alpha(),
            [this, &viewport](DrawList &list) {
              if (const auto &help = records_.script().help) {
                list.ticker(*help, help_run_, records_.seconds());
              }
              switch (records_.view()) {
              case RecordsView::menu:
                draw_menu(list, false);
                break;
              case RecordsView::page:
                draw_page(list);
                break;
              case RecordsView::convention:
              case RecordsView::venue:
              case RecordsView::regulation:
                draw_convention(list);
                if (trophies_) {
                  trophies_->draw(list, viewport, trophy_places());
                }
                break;
              }
            });
}

} // namespace phantomcrash
