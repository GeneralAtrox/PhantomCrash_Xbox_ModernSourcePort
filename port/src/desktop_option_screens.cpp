#include "desktop_option_screens.hpp"

#include "desktop_draw_list.hpp"

#include <utility>

namespace phantomcrash {
namespace {

void preload(TextureCache &textures, const std::vector<Sprite> &sprites) {
  for (const auto &sprite : sprites) {
    static_cast<void>(textures.get(sprite.image));
  }
}

template <typename Items>
std::vector<std::vector<int>> help_runs(const GlyphFont &font, const Items &items) {
  std::vector<std::vector<int>> runs;
  for (const auto &item : items) {
    runs.push_back(glyph_run(font, item.help.text));
  }
  return runs;
}

// A number's digits, right-aligned so the last one ends at `right`.
void digits(DrawList &list, const DigitStyle &style, const std::array<float, 2> &right, int value) {
  float x = right[0];
  for (int index = 0; index < style.digits; ++index) {
    x -= style.advance;
    Sprite digit;
    digit.image = style.image;
    digit.rect = style.rects[static_cast<std::size_t>(value % 10)];
    digit.position = {x, right[1]};
    digit.layer = style.layer;
    digit.colour = style.colour;
    list.sprite(digit);
    value /= 10;
  }
}

Sprite placed(Sprite sprite, const std::array<float, 2> &position) {
  sprite.position = position;
  return sprite;
}

} // namespace

SoundConfigScreen::SoundConfigScreen(TextureCache &textures, const GlyphFont &font,
                                     SoundConfigScript script, const GameOptions options,
                                     const FrameStyle &frame,
                                     const std::vector<MusicCredit> &credits)
    : MenuScreen(textures, std::move(script), options), font_(font),
      help_runs_(help_runs(font, menu_.script().items)),
      band_(font, frame, credits, credit_band(menu_.script())) {
  band_.preload(textures_);
  static_cast<void>(textures_.get(font_.image));
  static_cast<void>(textures_.get(menu_.script().digits.image));
  preload(textures_, menu_.script().layers);
}

void SoundConfigScreen::draw(const Viewport &viewport) {
  clear_black(textures_);
  const SoundConfigScript &script = menu_.script();
  DrawList list(textures_, font_, viewport, menu_.seconds());
  for (const auto &layer : script.layers) {
    list.sprite(layer);
  }
  for (std::size_t index = 0; index < script.items.size(); ++index) {
    const SoundItem &item = script.items[index];
    list.sprite(item.normal);
    // The selected item pulses throughout, fades included (0x3b426).
    if (static_cast<int>(index) == menu_.cursor()) {
      list.sprite(item.selected);
    }
    if (item.action == SoundAction::exit) {
      continue;
    }
    const int value = menu_.value(static_cast<int>(index));
    digits(list, script.digits, item.number, value);
    if (item.bar && value > 0) {
      Sprite bar = item.bar->sprite;
      bar.size = std::array<float, 2>{static_cast<float>(value) * item.bar->per_step,
                                      bar.rect[3] - bar.rect[1]};
      list.sprite(bar);
    }
  }
  const auto cursor = static_cast<std::size_t>(menu_.cursor());
  list.ticker(help_style(script.help, script.items[cursor].help), help_runs_[cursor],
              menu_.help_seconds());
  band_.draw(list, QuestSave{});
  list.flush();
  fill_black(textures_, menu_.black_alpha());
}

ButtonConfigScreen::ButtonConfigScreen(TextureCache &textures, const GlyphFont &font,
                                       ButtonConfigScript script, const GameOptions options,
                                       const FrameStyle &frame,
                                       const std::vector<MusicCredit> &credits)
    : MenuScreen(textures, std::move(script), options), font_(font),
      help_runs_(help_runs(font, menu_.script().rows)),
      band_(font, frame, credits, credit_band(menu_.script())) {
  band_.preload(textures_);
  static_cast<void>(textures_.get(font_.image));
  preload(textures_, menu_.script().layers);
  static_cast<void>(textures_.get(menu_.script().icons[0].image));
}

void ButtonConfigScreen::draw(const Viewport &viewport) {
  clear_black(textures_);
  const ButtonConfigScript &script = menu_.script();
  DrawList list(textures_, font_, viewport, menu_.seconds());
  for (const auto &layer : script.layers) {
    list.sprite(layer);
  }
  const int cursor = menu_.cursor();
  for (std::size_t index = 0; index < script.rows.size(); ++index) {
    const ButtonRow &row = script.rows[index];
    const int number = static_cast<int>(index);
    // The selected row's bar replaces its dimmed one.
    if (number == cursor) {
      list.sprite(placed(row.bar_selected.value_or(script.bar_selected), row.bar));
    } else {
      list.sprite(placed(row.bar_normal.value_or(script.bar_normal), row.bar));
    }
    list.sprite(row.label);
    if (row.kind == ButtonRowKind::choice) {
      list.sprite(row.values[static_cast<std::size_t>(menu_.choice(number))]);
      if (number == cursor) {
        for (const auto &arrow : script.right_arrow) {
          list.sprite(placed(arrow, row.right_arrow));
        }
        for (const auto &arrow : script.left_arrow) {
          list.sprite(placed(arrow, row.left_arrow));
        }
      }
    } else if (row.kind == ButtonRowKind::binding) {
      const auto button = static_cast<std::size_t>(menu_.binding(number));
      list.sprite(placed(script.icons[button], row.icon_position));
      if (number == cursor) {
        list.sprite(script.markers[button]);
      }
    }
  }
  const auto selected = static_cast<std::size_t>(cursor);
  list.ticker(help_style(script.help, script.rows[selected].help), help_runs_[selected],
              menu_.help_seconds());
  band_.draw(list, QuestSave{});
  list.flush();
  fill_black(textures_, menu_.black_alpha());
}

ScreenAdjustScreen::ScreenAdjustScreen(TextureCache &textures, const GlyphFont &font,
                                       ScreenAdjustScript script, MessageBoxStyle box,
                                       const GameOptions options)
    : MenuScreen(textures, std::move(script), std::move(box), options), font_(font) {
  static_cast<void>(textures_.get(font_.image));
  static_cast<void>(textures_.get(menu_.box().style().window.image));
  preload(textures_, menu_.script().layers);
}

void ScreenAdjustScreen::draw(const Viewport &viewport) {
  clear_black(textures_);
  const ScreenAdjustScript &script = menu_.script();
  DrawList list(textures_, font_, viewport, menu_.seconds());
  for (const auto &layer : script.layers) {
    list.sprite(layer);
  }
  const auto &offset = menu_.offset();
  for (const auto &sprite : script.fixed) {
    list.sprite(placed(sprite, {sprite.position[0] - static_cast<float>(offset[0]),
                                sprite.position[1] - static_cast<float>(offset[1])}));
  }
  list.message_box(menu_.box());
  list.flush();
  fill_black(textures_, menu_.black_alpha());
}

} // namespace phantomcrash
