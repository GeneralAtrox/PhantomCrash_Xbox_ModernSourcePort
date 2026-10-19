// Original Xbox (NV2A) textures to row-major RGBA8 (xbox_texture.py) and the
// game's texture packs (texture_pack.py).
#pragma once

#include "binary.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash::extract {

namespace texture_kind {
inline constexpr int a8r8g8b8 = 0x06;
inline constexpr int x8r8g8b8 = 0x07;
inline constexpr int p8 = 0x0B;
inline constexpr int dxt1 = 0x0C;
inline constexpr int dxt3 = 0x0E;
inline constexpr int dxt5 = 0x0F;
inline constexpr int linear_a8r8g8b8 = 0x12;
} // namespace texture_kind

struct TextureFormat {
  int kind{};
  int width{}, height{}, levels{};

  // A D3D Format word: log2 sizes in bits 20-27, levels in 16-19.
  [[nodiscard]] static TextureFormat from_word(std::uint32_t word);
  [[nodiscard]] std::string name() const;
  [[nodiscard]] std::size_t level_size(int level = 0) const;
  [[nodiscard]] std::size_t total_size() const;
};

// Mip level 0 of `data` as RGBA8, rows top to bottom. P8 needs a 256-entry
// B8G8R8A8 palette.
[[nodiscard]] Bytes decode_rgba(const TextureFormat &format, ByteView data,
                                std::optional<ByteView> palette = std::nullopt);

struct PackTexture {
  int index{};
  TextureFormat format;
  std::uint32_t data_offset{};
  std::uint32_t palette_offset{};

  [[nodiscard]] Bytes rgba(ByteView pack) const;
};

// A texture pack (Media\scroll\*_tex.bin, Media\texture, binlink slices).
[[nodiscard]] std::vector<PackTexture> parse_texture_pack(ByteView data);

} // namespace phantomcrash::extract
