#include "desktop_option.hpp"

#include "desktop_draw_list.hpp"

#include "phantomcrash/sprite_shapes.hpp"

#include <optional>
#include <utility>

namespace phantomcrash {
namespace {

// The plate is the atlas pill's own outline (data/ui/sprite_shapes.json's
// "pill" and "pill_lit" classes), so it sits where the sprite buttons' frames
// do: the same box inside the item's rect, corner radius and border band.
constexpr std::array<float, 4> plate_box{14.0F, 6.0F, 178.0F, 30.0F}; // texels in the rect
constexpr float plate_radius = 3.0F;        // corner radius, in texels
constexpr float plate_border_width = 3.0F;  // border band inside the box's edge, in texels
constexpr float plate_text_inset_x = 12.0F; // lettering clear of the border, in texels
constexpr float plate_text_inset_y = 4.0F;
constexpr std::uint32_t plate_fill = 0x151515;
constexpr std::uint32_t plate_border = 0xffffff;
constexpr std::uint32_t plate_ink = 0xfcc500;
constexpr std::uint32_t plate_lit_fill = 0xfcc500;
constexpr std::uint32_t plate_lit_border = 0x000000;
constexpr std::uint32_t plate_lit_ink = 0x000000;

FramePlacement option_frame(const OptionScript &script, const bool hud) {
  FramePlacement placement = credit_band(script);
  placement.hud = hud;
  placement.riding_plate = script.riding_plate_y.has_value();
  placement.riding_plate_y = script.riding_plate_y;
  return placement;
}

// The plate sits where the item's normal sprite does, at its rect's size,
// with the pill's box and border inside it.
void draw_plate(DrawList &list, const OptionItem &item, const OptionPlate &plate,
                const bool lit) {
  const Sprite &sprite = item.normal;
  const float width = sprite.rect[2] - sprite.rect[0];
  const float height = sprite.rect[3] - sprite.rect[1];
  const std::array<float, 4> box{sprite.position[0], sprite.position[1],
                                 sprite.position[0] + width, sprite.position[1] + height};
  ShapeOutline outline;
  outline.box = plate_box;
  outline.radius = plate_radius;
  // Inside is the fill; the border is the band from the box's edge inward.
  const std::uint32_t fill = lit ? plate_lit_fill : plate_fill;
  const std::uint32_t border = lit ? plate_lit_border : plate_border;
  outline.stops = {ShapeStop{0.0F, fill, 1.0F}, ShapeStop{0.0F, border, 1.0F},
                   ShapeStop{plate_border_width, border, 1.0F},
                   ShapeStop{plate_border_width, border, 0.0F}};
  const int layer = lit ? item.selected.layer : sprite.layer;
  list.shape(layer, {outline}, box);
  const std::array<float, 4> text_box{box[0] + plate_box[0] + plate_text_inset_x,
                                      box[1] + plate_box[1] + plate_text_inset_y,
                                      box[0] + plate_box[2] - plate_text_inset_x,
                                      box[1] + plate_box[3] - plate_text_inset_y};
  list.label(layer + 1, plate.text, plate.font, text_box, lit ? plate_lit_ink : plate_ink,
             std::nullopt, 0.0F, DisplayLayout{.centered = true});
}

} // namespace

OptionScreen::OptionScreen(TextureCache &textures, const GlyphFont &font, OptionScript script,
                           MessageBoxStyle box, const GameOptions options, const FrameStyle &frame,
                           const std::vector<MusicCredit> &credits,
                           const std::string &returning_from, QuestSave quest, const bool hud)
    : textures_(textures), font_(font),
      menu_(std::move(script), std::move(box), options, returning_from), quest_(std::move(quest)),
      band_(font, frame, credits, option_frame(menu_.script(), hud)) {
  for (const auto &item : menu_.script().items) {
    help_runs_.push_back(glyph_run(font_, item.help.text));
  }
  // Load the textures now rather than on the first frame that needs them.
  static_cast<void>(textures_.get(font_.image));
  static_cast<void>(textures_.get(menu_.box().style().window.image));
  for (const auto &layer : menu_.script().layers) {
    static_cast<void>(textures_.get(layer.image));
  }
  band_.preload(textures_);
}

void OptionScreen::tick(const InputFrame &input, const double tick_seconds) {
  menu_.update(tick_seconds, input);
  if (menu_.faded_in()) {
    band_.start_riding_plate();
  }
  band_.update(tick_seconds);
}

void OptionScreen::draw(const Viewport &viewport) {
  clear_black(textures_);
  const OptionScript &script = menu_.script();
  DrawList list(textures_, font_, viewport, menu_.seconds());
  for (const auto &layer : script.layers) {
    list.sprite(layer);
  }
  for (std::size_t index = 0; index < script.items.size(); ++index) {
    const auto &item = script.items[index];
    const bool lit = static_cast<int>(index) == menu_.cursor();
    if (item.plate) {
      draw_plate(list, item, *item.plate, lit);
      continue;
    }
    list.sprite(item.normal);
    if (lit) {
      // The selected item holds still while a box is open or a fade runs.
      Sprite selected = item.selected;
      if (!menu_.cursor_pulses()) {
        selected.animation = Animation::none;
      }
      list.sprite(selected);
    }
  }
  const auto cursor = static_cast<std::size_t>(menu_.cursor());
  list.ticker(help_style(script.help, script.items[cursor].help), help_runs_[cursor],
              menu_.help_seconds());
  band_.draw(list, quest_);
  list.message_box(menu_.box());
  list.flush();
  fill_black(textures_, menu_.black_alpha());
}

} // namespace phantomcrash
