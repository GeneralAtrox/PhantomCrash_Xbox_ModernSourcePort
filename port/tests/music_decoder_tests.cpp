#include "test_support.hpp"
#include "music_decoder.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <chrono>

using namespace phantomcrash;

namespace {
struct WaveFixture {
  std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("phantomcrash-music-test-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::path path = directory / "stereo.wav";
  WaveFixture(const int rate) {
    std::filesystem::create_directory(directory);
    std::ofstream file(path, std::ios::binary);
    const auto u16 = [&](const int n) {
      file.put(static_cast<char>(n));
      file.put(static_cast<char>(n >> 8));
    };
    const auto u32 = [&](const int n) {
      u16(n & 65535);
      u16(n >> 16);
    };
    file.write("RIFF", 4);
    u32(36 + rate * 4);
    file.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(rate);
    u32(rate * 4);
    u16(4);
    u16(16);
    file.write("data", 4);
    u32(rate * 4);
    for (int i = 0; i < rate; ++i) {
      u16(8192);
      u16(-4096);
    }
  }
  ~WaveFixture() {
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  }
};

std::vector<float> decode_all(MusicDecoder &decoder) {
  std::vector<float> result;
  for (int i = 0; i < 10000; ++i) {
    const auto block = decoder.read();
    if (block.empty()) {
      return result;
    }
    CHECK(block.size() % 2 == 0);
    result.insert(result.end(), block.begin(), block.end());
  }
  throw test::Failure("Decoder did not terminate");
}
} // namespace

TEST_CASE("music reads legacy WAV and preserves stereo samples across a loop") {
  WaveFixture wave(44100);
  MusicDecoder decoder;
  decoder.open(wave.path);
  const auto first = decode_all(decoder);
  REQUIRE(first.size() == 88200);
  CHECK(first[0] == 0.25F);
  CHECK(first[1] == -0.125F);
  CHECK(decoder.read().empty());
  decoder.rewind();
  CHECK(decode_all(decoder) == first);
  decoder.close();
  decoder.rewind();
  CHECK(decoder.read().empty());
}

TEST_CASE("music resamples and drains the final samples") {
  WaveFixture wave(22050);
  MusicDecoder decoder;
  decoder.open(wave.path);
  const auto first = decode_all(decoder);
  REQUIRE(first.size() == 88200);
  CHECK(std::abs(first[200] - 0.25F) < 0.0001F);
  CHECK(std::abs(first[201] + 0.125F) < 0.0001F);
  decoder.rewind();
  CHECK(decode_all(decoder) == first);
}

TEST_CASE("music fails cleanly on a missing or invalid file") {
  WaveFixture wave(44100);
  MusicDecoder decoder;
  CHECK_THROWS(decoder.open(wave.directory / "missing.ogg"));
  std::ofstream(wave.path, std::ios::binary | std::ios::trunc) << "invalid";
  CHECK_THROWS(decoder.open(wave.path));
  CHECK(decoder.read().empty());
}

TEST_CASE("converted ILD Ogg preserves channel order, duration and repeat playback") {
  const char *path = std::getenv("PHANTOMCRASH_TEST_OGG");
  if (path == nullptr) {
    return; // The conversion integration runner supplies its synthetic ILD.
  }
  MusicDecoder decoder;
  decoder.open(path);
  const auto first = decode_all(decoder);
  REQUIRE(first.size() > 100000 && first.size() < 115000);
  double left = 0, right = 0;
  for (std::size_t i = 2000; i + 2000 < first.size(); i += 2) {
    left += first[i];
    right += first[i + 1];
  }
  CHECK(left > 1000);
  CHECK(right < -1000);
  decoder.rewind();
  const auto second = decode_all(decoder);
  CHECK(second == first);
}
