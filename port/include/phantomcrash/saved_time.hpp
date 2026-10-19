#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// A save's date and time in the small digits (0xbcfe2, 0xbd255), as LOAD
// GAME and KEEP SV show them.
struct SavedTimeStyle {
  NumberFont font; // the small digits; its comma is the date's "/"
  Sprite separator;
  int layer{0x103};
};

// The document's "time_font", "time_separator" and "time_layer".
[[nodiscard]] SavedTimeStyle parse_saved_time_style(const json::JsonValue &document);

// The date and time of a save ("YYYY-MM-DD HH:MM:SS"): year, month, day,
// hour, minute and second; none when the text is not one.
[[nodiscard]] std::optional<std::array<int, 6>> parse_saved_time(const std::string &text);

// The local time now as "YYYY-MM-DD HH:MM:SS", for a save's time.
[[nodiscard]] std::string local_time_text();

// Four, two and two digits with "/" between from `date`, and two, two and
// two with ":" from `time`, zero-padded.
[[nodiscard]] std::vector<Sprite> saved_time_sprites(const SavedTimeStyle &style,
                                                     const std::array<float, 2> &date,
                                                     const std::array<float, 2> &time,
                                                     const std::array<int, 6> &when);

} // namespace phantomcrash
