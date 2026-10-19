#include "png_write.hpp"

#include <libdeflate.h>
#include <zlib.h>

#include <memory>
#include <string_view>

namespace phantomcrash::extract {
namespace {

constexpr int png_compression_level = 6;

void chunk(Bytes &out, std::string_view kind, ByteView payload) {
  put_u32_be(out, static_cast<std::uint32_t>(payload.size()));
  const std::size_t start = out.size();
  out.insert(out.end(), kind.begin(), kind.end());
  out.insert(out.end(), payload.begin(), payload.end());
  const auto crc = crc32(0, out.data() + start, static_cast<uInt>(out.size() - start));
  put_u32_be(out, static_cast<std::uint32_t>(crc));
}

} // namespace

Bytes encode_png(const int width, const int height, ByteView rgba) {
  const auto stride = static_cast<std::size_t>(width) * 4;
  if (rgba.size() != stride * static_cast<std::size_t>(height)) {
    throw FormatError("PNG needs " + std::to_string(stride * static_cast<std::size_t>(height)) +
                      " bytes of RGBA, got " + std::to_string(rgba.size()));
  }
  Bytes raw;
  raw.reserve((stride + 1) * static_cast<std::size_t>(height));
  for (int row = 0; row < height; ++row) {
    raw.push_back(0);
    const auto line = rgba.subspan(static_cast<std::size_t>(row) * stride, stride);
    raw.insert(raw.end(), line.begin(), line.end());
  }
  // libdeflate: the same zlib stream format as zlib's, several times faster
  // (the conversion's main cost is compressing ~1.5 GB of texels).
  thread_local const std::unique_ptr<libdeflate_compressor, decltype(&libdeflate_free_compressor)>
      compressor(libdeflate_alloc_compressor(png_compression_level), &libdeflate_free_compressor);
  if (!compressor) {
    throw FormatError("libdeflate could not allocate a compressor");
  }
  Bytes packed(libdeflate_zlib_compress_bound(compressor.get(), raw.size()));
  const std::size_t packed_size =
      libdeflate_zlib_compress(compressor.get(), raw.data(), raw.size(), packed.data(), packed.size());
  if (packed_size == 0) {
    throw FormatError("libdeflate failed to compress a PNG");
  }
  packed.resize(packed_size);

  Bytes header;
  put_u32_be(header, static_cast<std::uint32_t>(width));
  put_u32_be(header, static_cast<std::uint32_t>(height));
  header.insert(header.end(), {8, 6, 0, 0, 0});
  Bytes out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  chunk(out, "IHDR", header);
  chunk(out, "IDAT", packed);
  chunk(out, "IEND", {});
  return out;
}

void write_png(const std::filesystem::path &path, const int width, const int height,
               ByteView rgba) {
  write_file(path, encode_png(width, height, rgba));
}

} // namespace phantomcrash::extract
