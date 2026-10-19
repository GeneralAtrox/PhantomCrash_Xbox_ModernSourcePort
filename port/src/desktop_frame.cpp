#include "desktop_frame.hpp"

#include "desktop_draw_list.hpp"
#include "phantomcrash/money.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {
std::string hud_number(const HudStyle &hud, const NumberStyle &style, std::int64_t value) {
  const auto sprites = number_sprites(hud.font, style, value, hud.layer);
  std::string text;
  for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
    if (it->rect == hud.font.comma.rect) {
      text += ',';
    } else if (it->rect == hud.font.point.rect) {
      text += '.';
    } else {
      for (std::size_t digit = 0; digit < hud.font.digits.size(); ++digit) {
        if (it->rect == hud.font.digits[digit].rect) {
          text += static_cast<char>('0' + digit);
          break;
        }
      }
    }
  }
  return text;
}

void hud_text(DrawList &list, const GlyphFont &font, const std::string &text, float left, float top,
              float width, float height, bool right, int layer, std::uint32_t colour) {
  auto glyphs = layout_run(font, glyph_run(font, text), 0, 0);
  float extent = 0;
  for (const auto &glyph : glyphs) {
    extent = std::max(extent, glyph.x + glyph.width);
  }
  if (extent <= 0 || font.cell <= 0) {
    return;
  }
  const float scale = std::min(height / font.cell, width / extent);
  const float x = right ? left + width - extent * scale : left;
  for (auto &glyph : glyphs) {
    glyph.x = x + glyph.x * scale;
    glyph.y = top + glyph.y * scale;
    glyph.width *= scale;
    glyph.height *= scale;
  }
  list.shaded_text(layer, std::move(glyphs), colour);
}
} // namespace

LocationFrame::LocationFrame(const GlyphFont &font, const FrameStyle &style,
                             const std::vector<MusicCredit> &credits, FramePlacement placement)
    : font_(font), style_(style), credits_(credits), placement_(std::move(placement)),
      meter_(style.meter) {}

void LocationFrame::set_music(const std::string &file) {
  if (file == file_) {
    return;
  }
  file_ = file;
  const std::string line =
      file.empty() ? std::string{} : credit_line(credits_, file, placement_.now_playing_format);
  credit_run_ = glyph_run(font_, line);
  credit_seconds_ = 0.0;
}

void LocationFrame::update(const double tick_seconds) {
  credit_seconds_ += tick_seconds;
  if (plate_seconds_ >= 0.0) {
    plate_seconds_ += tick_seconds;
  }
  meter_.update(tick_seconds, heard_);
}

FramePlacement credit_band(const MenuTickers &tickers) {
  FramePlacement placement;
  placement.hud = false;
  placement.meter = tickers.meter;
  placement.now_playing = tickers.now_playing;
  placement.now_playing_format = tickers.now_playing_format;
  return placement;
}

HudStyle LocationFrame::hud() const {
  HudStyle hud = placement_.hud_at ? placed_hud(style_.hud, *placement_.hud_at) : style_.hud;
  if (hud.save_button && placement_.save_offset) {
    hud.save_button->offset = *placement_.save_offset;
  }
  return hud;
}

bool LocationFrame::day_clicked(const InputFrame &input, const QuestSave &quest) const {
  if (!placement_.hud || !input.clicked || !input.pointer) {
    return false;
  }
  const HudStyle hud = this->hud();
  auto sprites = number_sprites(hud.font, hud.day, quest.day + 1, hud.layer);
  sprites.push_back(hud.day_label);
  // Include the space between DAY- and its digits as one mouse target.
  float left = canvas_width, top = canvas_height, right = 0, bottom = 0;
  for (const auto &sprite : sprites) {
    const auto size = sprite.size.value_or(
        std::array{sprite.rect[2] - sprite.rect[0], sprite.rect[3] - sprite.rect[1]});
    const float x = sprite.position[0] - (sprite.centered ? size[0] * 0.5F : 0);
    const float y = sprite.position[1] - (sprite.centered ? size[1] * 0.5F : 0);
    left = std::min(left, x);
    top = std::min(top, y);
    right = std::max(right, x + size[0]);
    bottom = std::max(bottom, y + size[1]);
  }
  return (*input.pointer)[0] >= left && (*input.pointer)[0] < right && (*input.pointer)[1] >= top &&
         (*input.pointer)[1] < bottom;
}

bool LocationFrame::save_clicked(const InputFrame &input) {
  const HudStyle hud = this->hud();
  const auto box = placement_.hud ? hud_save_box(hud) : std::nullopt;
  save_shown_ = box.has_value();
  save_lit_ = box && input.pointer && (*input.pointer)[0] >= (*box)[0] &&
              (*input.pointer)[0] < (*box)[2] && (*input.pointer)[1] >= (*box)[1] &&
              (*input.pointer)[1] < (*box)[3];
  return save_lit_ && input.clicked;
}

void LocationFrame::draw(DrawList &list, const QuestSave &quest) const {
  if (placement_.hud) {
    const HudStyle hud = this->hud();
    const auto &day = hud.day_label;
    const float height = day.size ? (*day.size)[1] : day.rect[3] - day.rect[1];
    hud_text(list, font_, "DAY-" + hud_number(hud, hud.day, quest.day + 1), day.position[0],
             day.position[1], hud.day.right[0] - day.position[0], height, false, hud.layer,
             hud.day.colour);
    hud_text(list, font_, hud_number(hud, hud.money, shown_ny(quest.money)), hud.day.right[0] + 12,
             hud.money.right[1], hud.money.right[0] - hud.day.right[0] - 12, height, true,
             hud.layer, hud.money.colour);
    const auto &unit = hud.money_unit;
    const auto size =
        unit.size.value_or(std::array{unit.rect[2] - unit.rect[0], unit.rect[3] - unit.rect[1]});
    hud_text(list, font_, "NY", unit.position[0], unit.position[1], size[0], size[1], false,
             unit.layer, unit.colour);
    if (const auto box = hud_save_box(hud); box && save_shown_) {
      const HudSaveButton &button = *hud.save_button;
      list.shape(hud.layer, save_lit_ ? button.lit : button.normal, *box);
    }
  }
  // The plate shows only while a Scoobee is ridden (0x1d4a4).
  if (placement_.riding_plate && plate_seconds_ >= 0.0 && !quest.riding.empty()) {
    const RidingPlateStyle &style =
        placement_.riding_plate_style ? *placement_.riding_plate_style : style_.riding_plate;
    // The original's picture ends at x 0 and 640, so the sliding plate is
    // cut there rather than showing in the widescreen margin.
    Sprite plate = style.plate;
    plate.position[1] = placement_.riding_plate_y.value_or(plate.position[1]);
    const float x = plate.position[0] + riding_plate_offset(style, plate_seconds_);
    const float cut = std::max(-x, 0.0F);
    const float full = plate.rect[2] - plate.rect[0];
    const float width = full - cut - std::max(x + full - canvas_width, 0.0F);
    if (width > 0.0F) {
      plate.rect[0] += cut;
      plate.rect[2] = plate.rect[0] + width;
      plate.position[0] = x + cut;
      plate.size = std::array<float, 2>{width, plate.rect[3] - plate.rect[1]};
      list.sprite(plate);
    }
    std::vector<int> name = glyph_run(font_, quest.riding);
    name.resize(std::min(name.size(), static_cast<std::size_t>(style.max_glyphs)));
    list.text(
        style.name_layer,
        layout_run(font_, name, x + style.name_offset[0], plate.position[1] + style.name_offset[1]),
        style.name_colour, std::array<float, 4>{0.0F, 0.0F, canvas_width, canvas_height});
  }
  if (placement_.meter) {
    for (const auto &sprite : meter_sprites(style_.meter, *placement_.meter, meter_.levels())) {
      list.sprite(sprite);
    }
  }
  list.ticker(placement_.now_playing, credit_run_, credit_seconds_);
}

void LocationFrame::preload(TextureCache &textures) const {
  static_cast<void>(textures.get(font_.image));
  static_cast<void>(textures.get(style_.hud.day_label.image));
  static_cast<void>(textures.get(style_.meter.lit.image));
  if (placement_.riding_plate) {
    const RidingPlateStyle &plate =
        placement_.riding_plate_style ? *placement_.riding_plate_style : style_.riding_plate;
    static_cast<void>(textures.get(plate.plate.image));
  }
}

} // namespace phantomcrash
