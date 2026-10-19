// 8-bit RGBA PNG files, encoded as the Python tools encode them (filter 0 on
// every row, zlib level 6).
#pragma once

#include "binary.hpp"

#include <filesystem>

namespace phantomcrash::extract {

[[nodiscard]] Bytes encode_png(int width, int height, ByteView rgba);
void write_png(const std::filesystem::path &path, int width, int height, ByteView rgba);

} // namespace phantomcrash::extract
