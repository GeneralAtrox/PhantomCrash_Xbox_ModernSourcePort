// Text decoding as the Python tools do it: Shift-JIS (Python's cp932 codec,
// including its error handling), Latin-1 and UTF-16, NFKC and str.strip().
// All results are UTF-8.
#pragma once

#include <string>
#include <string_view>

namespace phantomcrash::extract {

enum class DecodeErrors { strict, replace };

// Python's bytes.decode("cp932", errors): an undecodable lead byte becomes
// U+FFFD (replace) or throws (strict), and decoding resumes at the next byte.
[[nodiscard]] std::string decode_cp932(std::string_view raw,
                                       DecodeErrors errors = DecodeErrors::strict);
[[nodiscard]] std::string decode_latin1(std::string_view raw);
[[nodiscard]] std::string decode_utf16le(std::string_view raw);

[[nodiscard]] std::string nfkc(std::string_view utf8);
// Python's str.strip(): Unicode whitespace off both ends.
[[nodiscard]] std::string strip(std::string_view utf8);
// The first code point of a UTF-8 string ("" when empty).
[[nodiscard]] std::string first_character(std::string_view utf8);

// Full-width Shift-JIS text as plain Unicode, without its padding
// (game_tables._plain: NFKC of the strict decode, stripped).
[[nodiscard]] std::string plain(std::string_view raw);

void append_utf8(std::string &out, char32_t code);

} // namespace phantomcrash::extract
