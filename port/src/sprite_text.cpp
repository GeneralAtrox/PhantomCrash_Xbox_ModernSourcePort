#include "phantomcrash/sprite_text.hpp"

#include "phantomcrash/json_classes.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace phantomcrash {

namespace {

float number(const json::JsonValue &value, const char *key, const float fallback) {
  return static_cast<float>(value.number_or(key, fallback));
}

std::array<float, 4> box(const json::JsonValue &value) {
  const auto &list = value.array();
  if (list.size() != 4) {
    throw std::runtime_error("sprite text region needs 4 coordinates");
  }
  return {static_cast<float>(list[0].number()), static_cast<float>(list[1].number()),
          static_cast<float>(list[2].number()), static_cast<float>(list[3].number())};
}

} // namespace

SpriteTextStyle parse_sprite_text_style(const json::JsonValue &entry) {
  SpriteTextStyle text;
  const json::JsonValue *source = entry.find("source");
  if (source == nullptr) {
    throw std::runtime_error("sprite text needs a source");
  }
  text.source = parse_sprite(*source);
  text.text = entry.string_or("text", "");
  if (const auto *region = entry.find("region")) {
    text.region = box(*region);
  }
  if (const auto *text_box = entry.find("text_box")) {
    text.text_box = box(*text_box);
  }
  if (const auto *colour = entry.find("colour")) {
    text.colour = parse_colour(colour->string());
  }
  text.opacity = std::clamp(number(entry, "opacity", 1.0F), 0.0F, 1.0F);
  text.stroke_width = std::clamp(number(entry, "stroke_width", 0.0F), 0.0F, 2.0F);
  text.centered = entry.boolean_or("centered", false);
  text.line_pitch = std::max(number(entry, "line_pitch", 0.0F), 0.0F);
  text.font = entry.string_or("font", "");
  text.erase = entry.boolean_or("erase", false);
  text.art = entry.boolean_or("art", true);
  text.stretch = entry.boolean_or("stretch", false);
  text.capitals = entry.boolean_or("capitals", false);
  text.slant = std::clamp(number(entry, "slant", 0.0F), -1.0F, 1.0F);
  if (entry.find("erase_slant") != nullptr) {
    text.erase_slant = std::clamp(number(entry, "erase_slant", 0.0F), -1.0F, 1.0F);
  }
  if (const auto *region = entry.find("erase_region")) {
    text.erase_region = box(*region);
  }
  if (const auto *fill = entry.find("erase_fill")) {
    text.erase_fill = parse_colour(fill->string());
  }
  if (const auto *outline = entry.find("outline")) {
    text.outline = parse_colour(outline->string());
  }
  text.outline_width = std::clamp(number(entry, "outline_width", 1.0F), 0.0F, 4.0F);
  if (const auto *shadow = entry.find("shadow")) {
    text.shadow = parse_text_shadow(*shadow);
    text.shadow_blur = std::clamp(number(*shadow, "blur", 0.0F), 0.0F, 4.0F);
  }
  return text;
}

TextShadow parse_text_shadow(const json::JsonValue &value) {
  TextShadow shadow;
  if (const auto *offset = value.find("offset")) {
    const auto &pair = offset->array();
    if (pair.size() != 2) {
      throw std::runtime_error("text shadow offset needs 2 numbers");
    }
    shadow.offset = {static_cast<float>(pair[0].number()), static_cast<float>(pair[1].number())};
  }
  if (const auto *colour = value.find("colour")) {
    shadow.colour = parse_colour(colour->string());
  }
  shadow.alpha = std::clamp(number(value, "alpha", shadow.alpha), 0.0F, 1.0F);
  return shadow;
}

SpriteTextTable parse_sprite_text_table(const json::JsonValue &document) {
  SpriteTextTable table;
  if (const auto *shadow = document.find("shadow")) {
    table.shadow = parse_text_shadow(*shadow);
  }
  if (const auto *fonts = document.find("fonts")) {
    for (const auto &[name, value] : fonts->object()) {
      DisplayFont font;
      for (const char *key : {"faces", "files"}) {
        if (const auto *list = value.find(key)) {
          auto &out = std::string_view(key) == "faces" ? font.faces : font.files;
          for (const auto &item : list->array()) {
            out.push_back(item.string());
          }
        }
      }
      font.weight = static_cast<int>(value.number_or("weight", font.weight));
      font.italic = value.boolean_or("italic", false);
      table.fonts[name] = std::move(font);
    }
  }
  const auto *classes = document.find("classes");
  if (const auto *replacements = document.find("replacements")) {
    for (const auto &entry : replacements->array()) {
      table.replacements.push_back(parse_sprite_text_style(json::with_classes(entry, classes)));
    }
  }
  return table;
}

} // namespace phantomcrash
