#include "xbox_texture.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace phantomcrash::extract {
namespace {

int bytes_per_texel(int kind) {
  switch (kind) {
  case texture_kind::a8r8g8b8:
  case texture_kind::x8r8g8b8:
  case texture_kind::linear_a8r8g8b8:
    return 4;
  case texture_kind::p8:
    return 1;
  default:
    return 0;
  }
}

int block_bytes(int kind) {
  switch (kind) {
  case texture_kind::dxt1:
    return 8;
  case texture_kind::dxt3:
  case texture_kind::dxt5:
    return 16;
  default:
    return 0;
  }
}

int log2(int value) {
  int bits = 0;
  while ((1 << (bits + 1)) <= value) {
    ++bits;
  }
  return bits;
}

// Swizzled texel index of each row-major texel: address bits interleave x
// and y, starting with x, until the smaller dimension runs out.
std::vector<std::uint32_t> swizzle_order(int width, int height) {
  const int x_bits = log2(width);
  const int y_bits = log2(height);
  std::vector<std::uint32_t> x_part(static_cast<std::size_t>(width));
  std::vector<std::uint32_t> y_part(static_cast<std::size_t>(height));
  int bit = 0;
  for (int used_x = 0, used_y = 0; used_x < x_bits || used_y < y_bits;) {
    if (used_x < x_bits) {
      for (int x = 0; x < width; ++x) {
        if ((x >> used_x) & 1) {
          x_part[static_cast<std::size_t>(x)] |= 1U << bit;
        }
      }
      ++bit;
      ++used_x;
    }
    if (used_y < y_bits) {
      for (int y = 0; y < height; ++y) {
        if ((y >> used_y) & 1) {
          y_part[static_cast<std::size_t>(y)] |= 1U << bit;
        }
      }
      ++bit;
      ++used_y;
    }
  }
  std::vector<std::uint32_t> order;
  order.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (const std::uint32_t y : y_part) {
    for (const std::uint32_t x : x_part) {
      order.push_back(y | x);
    }
  }
  return order;
}

// B8G8R8A8 texels to RGBA, alpha forced opaque for X8R8G8B8.
void bgra_texel(const std::uint8_t *in, std::uint8_t *out, bool opaque) {
  out[0] = in[2];
  out[1] = in[1];
  out[2] = in[0];
  out[3] = opaque ? 0xFF : in[3];
}

using Colour = std::array<int, 4>;

Colour rgb565(unsigned value) {
  const int red = static_cast<int>((value >> 11) & 0x1F);
  const int green = static_cast<int>((value >> 5) & 0x3F);
  const int blue = static_cast<int>(value & 0x1F);
  return {red << 3 | red >> 2, green << 2 | green >> 4, blue << 3 | blue >> 2, 255};
}

std::array<Colour, 4> colour_palette(unsigned c0, unsigned c1, bool allow_transparent) {
  const Colour a = rgb565(c0);
  const Colour b = rgb565(c1);
  std::array<Colour, 4> out{a, b, {}, {}};
  if (c0 > c1 || !allow_transparent) {
    out[2] = {(2 * a[0] + b[0]) / 3, (2 * a[1] + b[1]) / 3, (2 * a[2] + b[2]) / 3, 255};
    out[3] = {(a[0] + 2 * b[0]) / 3, (a[1] + 2 * b[1]) / 3, (a[2] + 2 * b[2]) / 3, 255};
  } else {
    out[2] = {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2, 255};
    out[3] = {0, 0, 0, 0};
  }
  return out;
}

std::array<int, 8> dxt5_alphas(int a0, int a1) {
  std::array<int, 8> out{a0, a1};
  if (a0 > a1) {
    for (int i = 1; i < 7; ++i) {
      out[static_cast<std::size_t>(i + 1)] = ((7 - i) * a0 + i * a1) / 7;
    }
  } else {
    for (int i = 1; i < 5; ++i) {
      out[static_cast<std::size_t>(i + 1)] = ((5 - i) * a0 + i * a1) / 5;
    }
    out[6] = 0;
    out[7] = 255;
  }
  return out;
}

Bytes decode_blocks(int kind, int width, int height, ByteView data) {
  Bytes out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
  const auto bytes = static_cast<std::size_t>(block_bytes(kind));
  const int blocks_x = std::max(1, (width + 3) / 4);
  const int blocks_y = std::max(1, (height + 3) / 4);
  std::size_t offset = 0;
  for (int block_y = 0; block_y < blocks_y; ++block_y) {
    for (int block_x = 0; block_x < blocks_x; ++block_x, offset += bytes) {
      const std::uint8_t *block = data.data() + offset;
      const std::size_t colour = bytes - 8;
      const unsigned c0 = u16(data, offset + colour);
      const unsigned c1 = u16(data, offset + colour + 2);
      const std::uint32_t bits = u32(data, offset + colour + 4);
      const auto colours = colour_palette(c0, c1, kind == texture_kind::dxt1);
      std::array<int, 16> alpha{};
      const bool explicit_alpha = kind != texture_kind::dxt1;
      if (kind == texture_kind::dxt5) {
        const auto alphas = dxt5_alphas(block[0], block[1]);
        std::uint64_t alpha_bits = 0;
        for (int i = 0; i < 6; ++i) {
          alpha_bits |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
        }
        for (int i = 0; i < 16; ++i) {
          alpha[static_cast<std::size_t>(i)] = alphas[(alpha_bits >> (3 * i)) & 7];
        }
      } else if (kind == texture_kind::dxt3) {
        std::uint64_t bits_alpha = 0;
        for (int i = 0; i < 8; ++i) {
          bits_alpha |= static_cast<std::uint64_t>(block[i]) << (8 * i);
        }
        for (int i = 0; i < 16; ++i) {
          alpha[static_cast<std::size_t>(i)] = static_cast<int>((bits_alpha >> (4 * i)) & 0xF) * 17;
        }
      }
      for (int row = 0; row < 4; ++row) {
        const int y = block_y * 4 + row;
        if (y >= height) {
          break;
        }
        for (int column = 0; column < 4; ++column) {
          if (block_x * 4 + column >= width) {
            break;
          }
          const int texel = row * 4 + column;
          const auto &c = colours[(bits >> (2 * texel)) & 3];
          std::uint8_t *at =
              out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(block_x * 4 + column)) *
                               4;
          at[0] = static_cast<std::uint8_t>(c[0]);
          at[1] = static_cast<std::uint8_t>(c[1]);
          at[2] = static_cast<std::uint8_t>(c[2]);
          at[3] = static_cast<std::uint8_t>(explicit_alpha ? alpha[static_cast<std::size_t>(texel)]
                                                           : c[3]);
        }
      }
    }
  }
  return out;
}

} // namespace

TextureFormat TextureFormat::from_word(const std::uint32_t word) {
  return {static_cast<int>((word >> 8) & 0xFF), 1 << ((word >> 20) & 0xF),
          1 << ((word >> 24) & 0xF), std::max(1, static_cast<int>((word >> 16) & 0xF))};
}

std::string TextureFormat::name() const {
  switch (kind) {
  case texture_kind::a8r8g8b8:
    return "A8R8G8B8";
  case texture_kind::x8r8g8b8:
    return "X8R8G8B8";
  case texture_kind::p8:
    return "P8";
  case texture_kind::dxt1:
    return "DXT1";
  case texture_kind::dxt3:
    return "DXT3";
  case texture_kind::dxt5:
    return "DXT5";
  case texture_kind::linear_a8r8g8b8:
    return "LIN_A8R8G8B8";
  default: {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02x", kind);
    return buffer;
  }
  }
}

std::size_t TextureFormat::level_size(const int level) const {
  const auto w = static_cast<std::size_t>(std::max(1, width >> level));
  const auto h = static_cast<std::size_t>(std::max(1, height >> level));
  if (const int block = block_bytes(kind)) {
    return std::max<std::size_t>(1, (w + 3) / 4) * std::max<std::size_t>(1, (h + 3) / 4) *
           static_cast<std::size_t>(block);
  }
  if (const int texel = bytes_per_texel(kind)) {
    return w * h * static_cast<std::size_t>(texel);
  }
  throw FormatError("Unsupported texture format " + name());
}

std::size_t TextureFormat::total_size() const {
  std::size_t total = 0;
  for (int level = 0; level < levels; ++level) {
    total += level_size(level);
  }
  return total;
}

Bytes decode_rgba(const TextureFormat &format, ByteView data, std::optional<ByteView> palette) {
  const std::size_t size = format.level_size(0);
  if (data.size() < size) {
    throw FormatError(format.name() + " " + std::to_string(format.width) + "x" +
                      std::to_string(format.height) + " needs " + std::to_string(size) +
                      " bytes, got " + std::to_string(data.size()));
  }
  data = data.first(size);
  const auto count = static_cast<std::size_t>(format.width) * static_cast<std::size_t>(format.height);
  if (block_bytes(format.kind) != 0) {
    return decode_blocks(format.kind, format.width, format.height, data);
  }
  Bytes out(count * 4);
  switch (format.kind) {
  case texture_kind::a8r8g8b8:
  case texture_kind::x8r8g8b8: {
    const auto order = swizzle_order(format.width, format.height);
    const bool opaque = format.kind == texture_kind::x8r8g8b8;
    for (std::size_t i = 0; i < count; ++i) {
      bgra_texel(data.data() + std::size_t{order[i]} * 4, out.data() + i * 4, opaque);
    }
    return out;
  }
  case texture_kind::linear_a8r8g8b8:
    for (std::size_t i = 0; i < count; ++i) {
      bgra_texel(data.data() + i * 4, out.data() + i * 4, false);
    }
    return out;
  case texture_kind::p8: {
    if (!palette || palette->size() < 1024) {
      throw FormatError("P8 texture without a 256-entry palette");
    }
    const auto order = swizzle_order(format.width, format.height);
    for (std::size_t i = 0; i < count; ++i) {
      bgra_texel(palette->data() + std::size_t{data[order[i]]} * 4, out.data() + i * 4, false);
    }
    return out;
  }
  default:
    throw FormatError("Unsupported texture format " + format.name());
  }
}

Bytes PackTexture::rgba(ByteView pack) const {
  std::optional<ByteView> palette;
  if (palette_offset != 0) {
    const std::size_t start = palette_offset + 0x40;
    palette = pack.subspan(std::min(start, pack.size()),
                           std::min<std::size_t>(1024, pack.size() - std::min(start, pack.size())));
  }
  const std::size_t end = std::min(pack.size(), data_offset + format.level_size(0));
  return decode_rgba(format, pack.subspan(data_offset, end - data_offset), palette);
}

std::vector<PackTexture> parse_texture_pack(ByteView data) {
  if (data.size() < 8) {
    throw FormatError("File too short for a texture pack header");
  }
  const std::uint32_t magic = u32(data, 0);
  const std::uint32_t count = u32(data, 4);
  if (magic != 0x80000101 && magic != 0x80000102) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", magic);
    throw FormatError(std::string("Unknown texture pack magic ") + buffer);
  }
  if (8 + std::size_t{count} * 8 > data.size()) {
    throw FormatError("Offset table for " + std::to_string(count) +
                      " textures lies outside the file");
  }
  std::vector<std::uint32_t> offsets(count);
  std::int64_t smallest = 8;
  for (std::uint32_t i = 0; i < count; ++i) {
    offsets[i] = u32(data, 8 + i * 4);
    smallest = i == 0 ? offsets[i] : std::min<std::int64_t>(smallest, offsets[i]);
  }
  // Most packs follow the offsets with one palette offset per texture; a few
  // place the first descriptor immediately after the offsets instead.
  const std::int64_t gap = smallest - 8 - std::int64_t{count} * 4;
  const std::int64_t gap_words = gap >= 0 ? gap / 4 : -((-gap + 3) / 4);
  std::vector<PackTexture> out;
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint32_t palette = gap_words >= count ? u32(data, 8 + count * 4 + i * 4) : 0;
    const std::uint32_t offset = offsets[i];
    if (std::size_t{offset} + 20 > data.size()) {
      throw FormatError("Descriptor " + std::to_string(i) + " lies outside the file");
    }
    const auto format = TextureFormat::from_word(u32(data, offset + 12));
    const std::uint32_t data_offset = u32(data, offset + 4);
    if (data_offset + format.level_size(0) > data.size()) {
      throw FormatError("Texture " + std::to_string(i) + " data lies outside the file");
    }
    if (palette != 0 && std::size_t{palette} + 0x40 + 1024 > data.size()) {
      throw FormatError("Palette " + std::to_string(i) + " lies outside the file");
    }
    out.push_back({static_cast<int>(i), format, data_offset, palette});
  }
  return out;
}

} // namespace phantomcrash::extract
