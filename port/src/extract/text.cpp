#include "text.hpp"

#include "binary.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <vector>

namespace phantomcrash::extract {
namespace {

constexpr char32_t replacement = 0xFFFD;

std::u16string to_utf16(std::string_view utf8) {
  std::u16string out;
  std::size_t i = 0;
  while (i < utf8.size()) {
    const auto lead = static_cast<unsigned char>(utf8[i]);
    const int extra = lead < 0x80 ? 0 : lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : 1;
    char32_t code = extra == 0 ? lead : lead & (0x3F >> extra);
    for (int k = 1; k <= extra; ++k) {
      code = (code << 6) | (static_cast<unsigned char>(utf8.at(i + static_cast<std::size_t>(k))) &
                            0x3F);
    }
    i += static_cast<std::size_t>(extra) + 1;
    if (code >= 0x10000) {
      out += static_cast<char16_t>(0xD800 + ((code - 0x10000) >> 10));
      out += static_cast<char16_t>(0xDC00 + ((code - 0x10000) & 0x3FF));
    } else {
      out += static_cast<char16_t>(code);
    }
  }
  return out;
}

std::string from_utf16(std::u16string_view text) {
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    char32_t code = text[i];
    if (code >= 0xD800 && code < 0xDC00 && i + 1 < text.size()) {
      code = 0x10000 + ((code - 0xD800) << 10) + (text[i + 1] - 0xDC00);
      ++i;
    }
    append_utf8(out, code);
  }
  return out;
}

bool is_space(char32_t c) {
  return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 ||
         c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
         c == 0x202F || c == 0x205F || c == 0x3000;
}

// A two-byte cp932 character, or U+FFFD when the pair maps to nothing.
char32_t decode_pair(unsigned char lead, unsigned char trail) {
  const char pair[2]{static_cast<char>(lead), static_cast<char>(trail)};
  wchar_t wide[4]{};
  const int count = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, pair, 2, wide, 4);
  return count == 1 ? static_cast<char32_t>(wide[0]) : replacement;
}

} // namespace

void append_utf8(std::string &out, const char32_t code) {
  if (code < 0x80) {
    out += static_cast<char>(code);
  } else if (code < 0x800) {
    out += static_cast<char>(0xC0 | (code >> 6));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else if (code < 0x10000) {
    out += static_cast<char>(0xE0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (code >> 18));
    out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  }
}

// Mirrors CPython's _codecs_jp cp932 decoder: single bytes up to 0x80 pass
// through, 0xa0..0xdf are half-width katakana, 0xfd..0xff Windows extras;
// any other byte leads a pair.
std::string decode_cp932(std::string_view raw, const DecodeErrors errors) {
  std::string out;
  std::size_t i = 0;
  while (i < raw.size()) {
    const auto c = static_cast<unsigned char>(raw[i]);
    if (c <= 0x80) {
      append_utf8(out, c);
      ++i;
      continue;
    }
    if (c >= 0xA0 && c <= 0xDF) {
      append_utf8(out, c == 0xA0 ? 0xF8F0 : 0xFEC0 + c);
      ++i;
      continue;
    }
    if (c >= 0xFD) {
      append_utf8(out, 0xF8F1 - 0xFD + c);
      ++i;
      continue;
    }
    const char32_t code = i + 1 < raw.size()
                              ? decode_pair(c, static_cast<unsigned char>(raw[i + 1]))
                              : replacement;
    if (code == replacement) {
      if (errors == DecodeErrors::strict) {
        throw FormatError("cp932 cannot decode byte at " + std::to_string(i));
      }
      append_utf8(out, replacement);
      ++i;
      continue;
    }
    append_utf8(out, code);
    i += 2;
  }
  return out;
}

std::string decode_latin1(std::string_view raw) {
  std::string out;
  for (const char c : raw) {
    append_utf8(out, static_cast<unsigned char>(c));
  }
  return out;
}

std::string decode_utf16le(std::string_view raw) {
  std::u16string wide;
  for (std::size_t i = 0; i + 1 < raw.size(); i += 2) {
    wide += static_cast<char16_t>(static_cast<unsigned char>(raw[i]) |
                                  static_cast<unsigned char>(raw[i + 1]) << 8);
  }
  return from_utf16(wide);
}

std::string nfkc(std::string_view utf8) {
  const std::u16string wide = to_utf16(utf8);
  if (wide.empty()) {
    return {};
  }
  const auto *source = reinterpret_cast<LPCWSTR>(wide.data());
  int size = NormalizeString(NormalizationKC, source, static_cast<int>(wide.size()), nullptr, 0);
  for (int attempt = 0; attempt < 4 && size > 0; ++attempt) {
    std::vector<wchar_t> buffer(static_cast<std::size_t>(size));
    const int written = NormalizeString(NormalizationKC, source, static_cast<int>(wide.size()),
                                        buffer.data(), size);
    if (written > 0) {
      return from_utf16({reinterpret_cast<const char16_t *>(buffer.data()),
                         static_cast<std::size_t>(written)});
    }
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      break;
    }
    size = -written;
  }
  throw FormatError("NFKC normalisation failed");
}

std::string strip(std::string_view utf8) {
  std::u16string wide = to_utf16(utf8);
  std::size_t begin = 0;
  std::size_t end = wide.size();
  while (begin < end && is_space(wide[begin])) {
    ++begin;
  }
  while (end > begin && is_space(wide[end - 1])) {
    --end;
  }
  return from_utf16(std::u16string_view(wide).substr(begin, end - begin));
}

std::string first_character(std::string_view utf8) {
  if (utf8.empty()) {
    return {};
  }
  const auto lead = static_cast<unsigned char>(utf8[0]);
  const std::size_t length = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
  return std::string(utf8.substr(0, length));
}

std::string plain(std::string_view raw) {
  return strip(nfkc(decode_cp932(raw)));
}

} // namespace phantomcrash::extract
