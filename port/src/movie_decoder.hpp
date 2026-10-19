#pragma once

#include <deque>
#include <filesystem>
#include <functional>
#include <limits>
#include <cstdint>
#include <string>
#include <vector>

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;
struct SwsContext;

namespace phantomcrash {

// FFmpeg demuxing and decoding for one movie (after the Lodoss movie player,
// without its frame-exact replay). Reads the original Sofdec .sfd files
// (MPEG-1 video + ADX audio in MPEG-PS) and anything else FFmpeg opens, so
// mods can use .mp4 or .webm. No SDL: video frames stay YUV 4:2:0 for the
// GPU, and audio comes out as interleaved 48 kHz stereo float.
class MovieDecoder final {
public:
  static constexpr int output_sample_rate = 48'000;
  static constexpr int output_channels = 2;

  // Receives converted audio: `frames` stereo sample frames starting at
  // media time `seconds`.
  using AudioSink = std::function<void(const float *samples, int frames, double seconds)>;

  MovieDecoder();
  ~MovieDecoder();
  MovieDecoder(const MovieDecoder &) = delete;
  MovieDecoder &operator=(const MovieDecoder &) = delete;

  // Throws std::runtime_error when the file cannot be opened or has no video.
  void open(const std::filesystem::path &path);
  void close() noexcept;

  // Reads and decodes until the video queue is full or the input is used up,
  // passing decoded audio to `sink` on the way.
  void pump(const AudioSink &sink);

  [[nodiscard]] bool is_open() const noexcept {
    return format_ != nullptr;
  }
  [[nodiscard]] bool has_audio() const noexcept {
    return audio_decoder_ != nullptr;
  }
  [[nodiscard]] int width() const noexcept {
    return width_;
  }
  [[nodiscard]] int height() const noexcept {
    return height_;
  }
  // From the container; 0 when it does not say.
  [[nodiscard]] double duration_seconds() const noexcept {
    return duration_seconds_;
  }
  [[nodiscard]] double frame_duration_seconds() const noexcept {
    return frame_duration_seconds_;
  }
  [[nodiscard]] bool input_eof() const noexcept {
    return input_eof_;
  }
  [[nodiscard]] bool video_drained() const noexcept {
    return video_drained_;
  }
  // True for movies without audio, too.
  [[nodiscard]] bool audio_drained() const noexcept {
    return audio_decoder_ == nullptr || audio_drained_;
  }

  // Decoded frames in presentation order, YUV 4:2:0 planar.
  [[nodiscard]] bool has_frame() const noexcept {
    return !frames_.empty();
  }
  [[nodiscard]] double front_seconds() const noexcept {
    return frames_.front().seconds;
  }
  [[nodiscard]] const AVFrame &front() const noexcept {
    return *frames_.front().frame;
  }
  // Time of the frame after the front one; infinity when none is queued.
  [[nodiscard]] double next_seconds() const noexcept {
    return frames_.size() > 1 ? frames_[1].seconds : std::numeric_limits<double>::infinity();
  }
  void pop_front() noexcept;

private:
  struct Frame {
    double seconds{};
    AVFrame *frame{};
  };
  static constexpr std::size_t frame_capacity = 8;

  [[nodiscard]] double seconds(const AVFrame &frame, int stream) const;
  void receive_video();
  void receive_audio(const AudioSink &sink);
  void queue_audio(const AudioSink &sink, const std::uint8_t **input, int input_frames,
                   double seconds);
  void submit_packet();
  void send_flush();

  AVFormatContext *format_{};
  AVCodecContext *video_decoder_{};
  AVCodecContext *audio_decoder_{};
  AVPacket *packet_{};
  AVFrame *decoded_{};
  SwsContext *converter_{};
  SwrContext *resampler_{};
  std::deque<Frame> frames_;
  std::vector<float> audio_buffer_;
  int video_stream_{-1};
  int audio_stream_{-1};
  int width_{};
  int height_{};
  double start_seconds_{};
  double duration_seconds_{};
  double frame_duration_seconds_{};
  double last_video_seconds_{-1.0};
  double last_audio_end_seconds_{};
  bool input_eof_{};
  bool packet_pending_{};
  bool video_flush_sent_{};
  bool audio_flush_sent_{};
  bool video_drained_{};
  bool audio_drained_{};
};

} // namespace phantomcrash
