#pragma once

#include <filesystem>
#include <memory>
#include <vector>

namespace phantomcrash {

// Incremental audio decoding, shared by music playback and its tests. Output
// is interleaved stereo float PCM at 44100 Hz, independent of source format.
class MusicDecoder final {
public:
  MusicDecoder();
  ~MusicDecoder();
  MusicDecoder(const MusicDecoder &) = delete;
  MusicDecoder &operator=(const MusicDecoder &) = delete;
  void open(const std::filesystem::path &path);
  void close() noexcept;
  void rewind();
  // An empty block means EOF. Throws on a damaged stream or decoding failure.
  std::vector<float> read();
  static constexpr int rate = 44100;
  static constexpr int channels = 2;

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace phantomcrash
