#include "phantomcrash/saved_time.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>
#include <utility>

namespace phantomcrash {

SavedTimeStyle parse_saved_time_style(const json::JsonValue &document) {
  SavedTimeStyle style;
  style.font = json::member(document, "time_font", parse_number_font);
  style.separator = json::member(document, "time_separator", parse_sprite);
  style.layer = static_cast<int>(document.number_or("time_layer", style.layer));
  return style;
}

std::optional<std::array<int, 6>> parse_saved_time(const std::string &text) {
  // "YYYY-MM-DD HH:MM:SS": each field's first character and length.
  constexpr std::array<std::pair<std::size_t, std::size_t>, 6> fields{
      {{0, 4}, {5, 2}, {8, 2}, {11, 2}, {14, 2}, {17, 2}}};
  if (text.size() != 19) {
    return std::nullopt;
  }
  std::array<int, 6> time{};
  for (std::size_t index = 0; index < fields.size(); ++index) {
    const auto [start, length] = fields[index];
    const char *first = text.data() + start;
    const auto result = std::from_chars(first, first + length, time[index]);
    if (result.ec != std::errc{} || result.ptr != first + length) {
      return std::nullopt;
    }
  }
  return time;
}

std::string local_time_text() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char text[32]{};
  std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
  return text;
}

std::vector<Sprite> saved_time_sprites(const SavedTimeStyle &style,
                                       const std::array<float, 2> &date,
                                       const std::array<float, 2> &time,
                                       const std::array<int, 6> &when) {
  const NumberFont &font = style.font;
  std::vector<Sprite> sprites;
  const auto put = [&](const Sprite &source, float &x, const float y, const float advance) {
    Sprite sprite = source;
    sprite.position = {x, y};
    sprite.layer = style.layer;
    sprites.push_back(std::move(sprite));
    x += advance;
  };
  // Zero-padded, left to right (0xbc6d2 with flag 0x10).
  const auto number = [&](const int value, const int digits, float &x, const float y) {
    int scale = 1;
    for (int digit = 1; digit < digits; ++digit) {
      scale *= 10;
    }
    for (; scale > 0; scale /= 10) {
      const auto digit = static_cast<std::size_t>((std::max(value, 0) / scale) % 10);
      put(font.digits[digit], x, y, font.advance);
    }
  };
  float x = date[0];
  number(when[0], 4, x, date[1]);
  put(font.comma, x, date[1], font.comma_advance);
  number(when[1], 2, x, date[1]);
  put(font.comma, x, date[1], font.comma_advance);
  number(when[2], 2, x, date[1]);
  x = time[0];
  for (std::size_t field = 3; field < when.size(); ++field) {
    if (field != 3) {
      put(style.separator, x, time[1], font.advance);
    }
    number(when[field], 2, x, time[1]);
  }
  return sprites;
}

} // namespace phantomcrash
