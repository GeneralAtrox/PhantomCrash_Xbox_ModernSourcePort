#include "desktop_load_game.hpp"

#include "desktop_draw_list.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {

LoadGameScreen::LoadGameScreen(TextureCache &textures, const GlyphFont &font,
                               const PartTables &parts, LoadGameScript script,
                               std::vector<SaveSlot> slots, const MessageBoxStyle &box,
                               const FrameStyle &frame, const std::vector<MusicCredit> &credits,
                               QuestSave quest, const bool hud, SaveLoadHooks hooks,
                               const bool unreadable)
    : textures_(textures), font_(font), parts_(parts), style_(frame),
      menu_(std::move(script), std::move(slots), box, std::move(hooks), unreadable),
      quest_(std::move(quest)), frame_(font, frame, credits, with_hud(menu_.script().frame, hud)) {
  const LoadGameScript &data = menu_.script();
  help_run_ = glyph_run(font_, data.help_text);
  for (const auto &layer : data.layers) {
    static_cast<void>(textures_.get(layer.image));
  }
  static_cast<void>(textures_.get(data.saved_time.font.comma.image));
  static_cast<void>(textures_.get(box.window.image));
  frame_.preload(textures_);
}

void LoadGameScreen::tick(const InputFrame &input, const double tick_seconds) {
  menu_.update(tick_seconds, input);
  frame_.update(tick_seconds);
}

std::optional<Sprite> LoadGameScreen::profile_image(const QuestSave &quest,
                                                    const SaveSlotStyle &slot) const {
  const auto &garage = quest.garage;
  if (garage.riding < 0 || garage.riding >= static_cast<int>(garage.hangars.size())) {
    return std::nullopt;
  }
  const auto &hangar = garage.hangars[static_cast<std::size_t>(garage.riding)];
  const auto stock = garage.stock.find(hangar.part(PartSlot::chip));
  if (!hangar.owned || stock == garage.stock.end() || stock->second.kind != PartKind::chip) {
    return std::nullopt;
  }
  const int chip = stock->second.id;
  if (chip < 0 || chip >= static_cast<int>(parts_.chips.size()) || !parts_.chips[chip].face) {
    return std::nullopt;
  }
  const auto &style = menu_.script();
  Sprite face = *parts_.chips[chip].face;
  const float width = face.rect[2] - face.rect[0], height = face.rect[3] - face.rect[1];
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  const float scale = std::min(style.profile_size[0] / width, style.profile_size[1] / height);
  face.size = std::array<float, 2>{width * scale, height * scale};
  face.centered = true;
  face.position = {slot.plate.position[0] + style.profile_offset[0] + style.profile_size[0] / 2,
                   slot.plate.position[1] + style.profile_offset[1] + style.profile_size[1] / 2};
  face.layer = style.profile_layer;
  return face;
}

void LoadGameScreen::draw_details(DrawList &list, const QuestSave &quest) const {
  const SaveDetailsStyle &style = menu_.script().details;
  const auto text = [&](const std::string &value, const std::array<float, 2> &position) {
    std::vector<int> run = glyph_run(font_, value);
    run.resize(std::min(run.size(), static_cast<std::size_t>(style.max_glyphs)));
    list.text(style.text_layer, layout_run(font_, run, position[0], position[1]),
              style.text_colour);
  };
  text(quest.player_name, style.name);
  // The Scoobee's name shows only while one is ridden (slot 0-9, 0x6b65a).
  if (!quest.riding.empty()) {
    text(quest.riding, style.riding);
  }
  Sprite label = style.day_label;
  label.layer = style.number_layer;
  list.sprite(label);
  for (const auto &sprite :
       number_sprites(style_.hud.font, style.day, quest.day + 1, style.number_layer)) {
    list.sprite(sprite);
  }
  for (const auto &sprite : details_money_sprites(style_.hud.font, style, quest.money)) {
    list.sprite(sprite);
  }
}

void LoadGameScreen::draw(const Viewport &viewport) {
  clear_black(textures_);
  const LoadGameScript &script = menu_.script();
  DrawList list(textures_, font_, viewport, menu_.seconds());
  for (const auto &layer : script.layers) {
    list.sprite(layer);
  }
  const LoadGame::Phase phase = menu_.phase();
  if (phase != LoadGame::Phase::no_records) {
    for (std::size_t index = 0; index < script.slots.size(); ++index) {
      const SaveSlotStyle &slot = script.slots[index];
      const auto &record = menu_.slots()[index];
      const auto &quest = record.quest;
      const auto card_sprite = [&](Sprite sprite) {
        if (!record.selectable) {
          sprite.colour = 0x606060;
        }
        list.sprite(sprite);
      };
      card_sprite(slot.plate);
      if (!record.owner.empty()) {
        const auto label = [&](const std::string &text, const float y) {
          auto glyphs = layout_run(font_, glyph_run(font_, text), 0, 0);
          for (auto &glyph : glyphs) {
            glyph.x = slot.plate.position[0] + glyph.x * 0.55F;
            glyph.y = y + glyph.y * 0.55F;
            glyph.width *= 0.55F;
            glyph.height *= 0.55F;
          }
          const std::array<float, 4> clip{slot.plate.position[0], y, slot.plate.position[0] + 128,
                                          y + 18};
          list.text(script.details.text_layer, std::move(glyphs),
                    record.selectable ? 0xffff80 : 0x909090, clip);
        };
        label(record.owner, slot.plate.position[1] - 18);
        label(quest ? quest->player_name : "Empty", slot.plate.position[1] + 154);
      }
      if (static_cast<int>(index) == menu_.slot() && phase != LoadGame::Phase::exit) {
        // The lit plate pulses while the cursor is on the slots and holds
        // still behind a notice.
        Sprite lit = slot.lit;
        if (phase != LoadGame::Phase::slots) {
          lit.animation = Animation::none;
        }
        list.sprite(lit);
      }
      if (!quest) {
        card_sprite(slot.empty);
        continue;
      }
      if (const auto portrait = profile_image(*quest, slot)) {
        card_sprite(*portrait);
      }
      if (const auto time = parse_saved_time(quest->saved)) {
        for (const auto &sprite : saved_time_sprites(script, slot, *time)) {
          card_sprite(sprite);
        }
      }
      if (quest->cleared) {
        card_sprite(slot.clear);
      }
    }
    list.sprite(script.exit.normal);
    if (phase == LoadGame::Phase::exit) {
      list.sprite(script.exit.selected);
    } else if (const auto &quest = menu_.slots()[static_cast<std::size_t>(menu_.slot())].quest) {
      draw_details(list, *quest);
    }
  }
  list.ticker(script.help, help_run_, menu_.seconds());
  frame_.draw(list, quest_);
  list.message_box(menu_.box());
  list.flush();
  fill_black(textures_, menu_.black_alpha());
}

} // namespace phantomcrash
