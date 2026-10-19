#include "desktop_name_entry.hpp"

#include "desktop_draw_list.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {

NameEntryScreen::NameEntryScreen(TextureCache &textures, const GlyphFont &font,
                                 NameEntryScript script, MessageBoxStyle box, QuestSave save,
                                 const FrameStyle &frame, const std::vector<MusicCredit> &credits)
    : textures_(textures), font_(font), entry_(std::move(script), std::move(box), std::move(save)),
      frame_(font, frame, credits, entry_.script().frame) {
  const NameEntryScript &data = entry_.script();
  for (const auto &page : data.pages) {
    std::vector<int> glyphs;
    for (const auto &row : page.rows) {
      for (const auto &character : row) {
        glyphs.push_back(glyph_run(font_, character).front());
      }
    }
    page_glyphs_.push_back(std::move(glyphs));
    help_runs_.push_back(glyph_run(font_, page.help.text));
  }
  // Load the textures now rather than on the first frame that needs them.
  frame_.preload(textures_);
  static_cast<void>(textures_.get(entry_.box().style().window.image));
  for (const auto &layer : data.layers) {
    static_cast<void>(textures_.get(layer.image));
  }
  static_cast<void>(textures_.get(data.layout.slide_box.image));
  for (const auto &hint : data.layout.hints) {
    static_cast<void>(textures_.get(hint.image));
  }
}

void NameEntryScreen::tick(const InputFrame &input, const double tick_seconds) {
  entry_.update(tick_seconds, input);
  frame_.update(tick_seconds);
}

// A page's characters in their grid cells (0x2d9ca; End has none).
void NameEntryScreen::draw_page(DrawList &list, const int page, const float alpha) const {
  const NameEntryScript &script = entry_.script();
  const NameEntryLayout &layout = script.layout;
  const auto &glyphs = page_glyphs_[static_cast<std::size_t>(page)];
  if (glyphs.empty()) {
    return;
  }
  std::vector<GlyphQuad> quads;
  for (int row = 0; row < script.rows; ++row) {
    for (int column = 0; column < script.columns; ++column) {
      const float x = layout.grid_position[0] + static_cast<float>(column) * layout.grid_step[0];
      const float y = layout.grid_position[1] + static_cast<float>(row) * layout.grid_step[1];
      quads.push_back(
          glyph_cell(font_, glyphs[static_cast<std::size_t>(row * script.columns + column)], x, y));
    }
  }
  list.text(layout.grid_layer, std::move(quads), layout.grid_colour, std::nullopt, alpha);
}

void NameEntryScreen::draw_cursors(DrawList &list) const {
  const NameEntryScript &script = entry_.script();
  const NameEntryLayout &layout = script.layout;
  const float alpha = entry_.cursor_alpha();
  if (!entry_.on_tabs()) {
    Sprite cursor = layout.grid_cursor;
    cursor.position[0] += static_cast<float>(entry_.column()) * layout.grid_step[0];
    cursor.position[1] += static_cast<float>(entry_.row()) * layout.grid_step[1];
    list.sprite(cursor, alpha);
  }
  Sprite tab = layout.tab_cursor;
  tab.position[0] += static_cast<float>(entry_.page()) * layout.tab_step;
  list.sprite(tab, alpha);
  // After the name; on the last place once the name is full.
  int place = static_cast<int>(entry_.name().size());
  if (place >= script.max_length) {
    place = script.max_length - 1;
  }
  Sprite name = layout.name_cursor;
  name.position[0] += static_cast<float>(place) * layout.name_advance;
  list.sprite(name, alpha);
}

void NameEntryScreen::draw(const Viewport &viewport) {
  clear_black(textures_);
  const NameEntryScript &script = entry_.script();
  const NameEntryLayout &layout = script.layout;
  DrawList list(textures_, font_, viewport, entry_.seconds());
  for (const auto &layer : script.layers) {
    list.sprite(layer);
  }
  for (const auto &layer : layout.intro_layers) {
    list.sprite(layer, entry_.intro());
  }
  // The box slides in from the 4:3 picture's right edge, cut off there as the
  // original's screen edge cuts it, not showing in the widescreen margin.
  Sprite box = layout.slide_box;
  box.position[0] = entry_.slide_x();
  const float visible = std::min(box.rect[2] - box.rect[0], canvas_width - box.position[0]);
  if (visible > 0.0F) {
    box.rect[2] = box.rect[0] + visible;
    list.sprite(box);
  }
  for (const auto &hint : layout.hints) {
    list.sprite(hint, entry_.hints_alpha());
  }
  if (const auto [scale_x, scale_y] = entry_.name_box_scale(); scale_x > 0.0F) {
    Sprite name_box = layout.name_box;
    name_box.size = std::array<float, 2>{(name_box.rect[2] - name_box.rect[0]) * scale_x,
                                         (name_box.rect[3] - name_box.rect[1]) * scale_y};
    list.sprite(name_box);
  }
  if (entry_.name_shown()) {
    list.sprite(layout.label);
    std::string name;
    for (const auto &character : entry_.name()) {
      name += character;
    }
    list.text(layout.name_layer,
              layout_cells(font_, glyph_run(font_, name), layout.name_position[0],
                           layout.name_position[1], layout.name_advance),
              layout.name_colour);
  }
  // The keyboard shows once the grid has faded in; a new page fades in over
  // the one before it.
  if (entry_.intro() >= 1.0F) {
    draw_page(list, entry_.page(), entry_.page_alpha());
    if (const int previous = entry_.previous_page(); previous >= 0) {
      draw_page(list, previous, 1.0F - entry_.page_alpha());
    }
    draw_cursors(list);
  }
  const auto page = static_cast<std::size_t>(entry_.page());
  list.ticker(help_style(script.help, script.pages[page].help), help_runs_[page],
              entry_.help_seconds());
  frame_.draw(list, entry_.save());
  list.message_box(entry_.box());
  list.flush();
  fill_black(textures_, entry_.black_alpha());
}

} // namespace phantomcrash
