// Decodes the game's own Sofdec movies through MovieDecoder. The movies are
// private input (extracted from the user's disc), so the test reports a skip
// when they are missing.

#include "movie_decoder.hpp"
#include "phantomcrash/data_roots.hpp"

extern "C" {
#include <libavutil/frame.h>
}

#include <cstdio>
#include <exception>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

constexpr int skipped = 77;

struct Result {
  int frames{};
  long long audio_frames{};
  double last_seconds{-1.0};
  bool ordered{true};
  bool finished{};
};

// Decodes up to `limit_seconds` of video (everything when negative).
Result decode(const std::filesystem::path &path, const double limit_seconds,
              phantomcrash::MovieDecoder &decoder) {
  decoder.open(path);
  Result result;
  const auto sink = [&](const float *, const int frames, double) { result.audio_frames += frames; };
  for (int round = 0; round < 1'000'000; ++round) {
    decoder.pump(sink);
    while (decoder.has_frame()) {
      const double seconds = decoder.front_seconds();
      const auto &frame = decoder.front();
      if (frame.width != decoder.width() || frame.height != decoder.height() ||
          frame.data[0] == nullptr || frame.data[1] == nullptr || frame.data[2] == nullptr) {
        result.ordered = false;
      }
      result.ordered = result.ordered && seconds > result.last_seconds;
      result.last_seconds = seconds;
      ++result.frames;
      decoder.pop_front();
    }
    if (limit_seconds >= 0.0 && result.last_seconds >= limit_seconds) {
      break;
    }
    if (decoder.input_eof() && decoder.video_drained() && decoder.audio_drained()) {
      result.finished = true;
      break;
    }
  }
  return result;
}

} // namespace

int main() {
  const auto roots = phantomcrash::DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
  const char *movies[] = {"phantagram", "dolby", "opening", "phantomcrash",
                          "opening02",  "cloud", "ending",  "epilogue",
                          "kudryavka",  "staff", "the_end"};
  if (!roots.find_asset("movie/phantagram.sfd")) {
    std::printf("SKIP: movies not extracted (run scripts/extract-game.ps1)\n");
    return skipped;
  }
  int failed = 0;
  phantomcrash::MovieDecoder decoder;
  for (const char *name : movies) {
    const std::string file = std::string("movie/") + name + ".sfd";
    // The intro's short movies are decoded to the end; the rest for 10 s.
    const bool whole = std::string(name) == "phantagram" || std::string(name) == "dolby";
    try {
      const auto path = roots.find_asset(file);
      if (!path) {
        throw std::runtime_error("missing");
      }
      const auto result = decode(*path, whole ? -1.0 : 10.0, decoder);
      const bool ok = decoder.width() == 640 && decoder.height() == 480 && result.frames > 0 &&
                      result.ordered && decoder.has_audio() && result.audio_frames > 0 &&
                      (!whole || result.finished);
      std::printf("%s: %s  %dx%d, %d frames to %.2f s, %.2f s of audio, duration %.2f s\n",
                  ok ? "PASS" : "FAIL", file.c_str(), decoder.width(), decoder.height(),
                  result.frames, result.last_seconds,
                  static_cast<double>(result.audio_frames) /
                      phantomcrash::MovieDecoder::output_sample_rate,
                  decoder.duration_seconds());
      failed += ok ? 0 : 1;
    } catch (const std::exception &error) {
      std::printf("FAIL: %s  %s\n", file.c_str(), error.what());
      ++failed;
    }
    decoder.close();
  }
  std::printf("%zu movies, %d failed\n", std::size(movies), failed);
  return failed == 0 ? 0 : 1;
}
