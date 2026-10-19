// NAME ENTRY from the keyboard (a port addition). Typing a character the
// pages have puts the cursor on its key and types it, as confirm there does;
// Backspace deletes as the delete button does, and Enter ends the entry as
// END does. A typed ASCII character finds its full-width key (the pages'
// letters and digits are full-width), or the key script.typed_aliases names.

#include "phantomcrash/name_entry.hpp"

#include <array>
#include <cstdint>

namespace phantomcrash {
namespace {

constexpr const char *space = "\xe3\x80\x80"; // U+3000, the game's space
// ASCII '!'..'~' sit at U+FF01..U+FF5E in full width.
constexpr char32_t first_printable = 0x21;
constexpr char32_t last_printable = 0x7e;
constexpr char32_t full_width_offset = 0xfee0;

// U+0800..U+FFFF as UTF-8.
std::string three_byte_utf8(const char32_t code) {
  return {static_cast<char>(0xe0 | (code >> 12)), static_cast<char>(0x80 | ((code >> 6) & 0x3f)),
          static_cast<char>(0x80 | (code & 0x3f))};
}

// The character's full-width form, or empty when it has none.
std::string full_width(const std::string &character) {
  if (character.size() != 1) {
    return {};
  }
  const auto code = static_cast<char32_t>(static_cast<unsigned char>(character[0]));
  if (code < first_printable || code > last_printable) {
    return {};
  }
  return three_byte_utf8(code + full_width_offset);
}

} // namespace

void NameEntry::keyboard(const InputFrame &input) {
  for (const std::string &character : split_characters(input.typed)) {
    type_key(character);
  }
  for (int erased = 0; erased < input.erased && !name_.empty(); ++erased) {
    name_.pop_back();
    sound(script_.delete_sound);
  }
  if (input.text_submit) {
    sound(script_.select_sound);
    finish();
  }
}

void NameEntry::type_key(const std::string &character) {
  if (character == " " || character == space) {
    sound(script_.select_sound);
    type(space);
    return;
  }
  std::vector<std::string> wanted;
  if (const auto alias = script_.typed_aliases.find(character);
      alias != script_.typed_aliases.end()) {
    wanted.push_back(alias->second);
  }
  wanted.push_back(character);
  if (std::string wide = full_width(character); !wide.empty()) {
    wanted.push_back(std::move(wide));
  }
  // The page showing first, then the others in order.
  std::vector<int> order{page_};
  for (int page = 0; page < end_page(); ++page) {
    if (page != page_) {
      order.push_back(page);
    }
  }
  for (const std::string &key : wanted) {
    for (const int page : order) {
      if (page == end_page()) {
        continue;
      }
      const auto &rows = script_.pages[static_cast<std::size_t>(page)].rows;
      for (std::size_t row = 0; row < rows.size(); ++row) {
        for (std::size_t column = 0; column < rows[row].size(); ++column) {
          if (rows[row][column] != key) {
            continue;
          }
          if (page != page_) {
            previous_ = page_;
            page_ = page;
            page_alpha_ = 0.0;
            help_.restart(page_);
          }
          tabs_ = false;
          tab_row_cursor_ = tab_row();
          row_ = static_cast<int>(row);
          column_ = static_cast<int>(column);
          sound(script_.select_sound);
          type(key);
          return;
        }
      }
    }
  }
}

} // namespace phantomcrash
