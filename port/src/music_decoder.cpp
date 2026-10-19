#include "music_decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}

#include <array>
#include <stdexcept>

namespace phantomcrash {
namespace {

void checked(const int result, const char *operation) {
  if (result >= 0) {
    return;
  }
  std::array<char, AV_ERROR_MAX_STRING_SIZE> message{};
  av_strerror(result, message.data(), message.size());
  throw std::runtime_error(std::string(operation) + ": " + message.data());
}

} // namespace

struct MusicDecoder::State {
  AVFormatContext *format{};
  AVCodecContext *codec{};
  SwrContext *resampler{};
  AVPacket *packet{av_packet_alloc()};
  AVFrame *frame{av_frame_alloc()};
  int stream{-1};
  bool input_end{};
  bool flush_sent{};
  bool drained{};
  bool pending{};
  ~State() {
    swr_free(&resampler);
    avcodec_free_context(&codec);
    avformat_close_input(&format);
    av_packet_free(&packet);
    av_frame_free(&frame);
  }
};

MusicDecoder::MusicDecoder() = default;
MusicDecoder::~MusicDecoder() = default;

void MusicDecoder::close() noexcept {
  state_.reset();
}

void MusicDecoder::open(const std::filesystem::path &path) {
  close();
  auto s = std::make_unique<State>();
  if (!s->packet || !s->frame) {
    throw std::runtime_error("Unable to allocate music decoding state");
  }
  const auto encoded = path.u8string();
  checked(avformat_open_input(&s->format, reinterpret_cast<const char *>(encoded.c_str()), nullptr,
                              nullptr),
          "Opening music");
  checked(avformat_find_stream_info(s->format, nullptr), "Reading music format");
  const AVCodec *decoder{};
  s->stream = av_find_best_stream(s->format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
  checked(s->stream, "Finding music audio");
  s->codec = avcodec_alloc_context3(decoder);
  if (!s->codec) {
    throw std::runtime_error("Unable to allocate music codec");
  }
  checked(avcodec_parameters_to_context(s->codec, s->format->streams[s->stream]->codecpar),
          "Reading music codec");
  checked(avcodec_open2(s->codec, decoder, nullptr), "Opening music codec");
  AVChannelLayout output = AV_CHANNEL_LAYOUT_STEREO;
  checked(swr_alloc_set_opts2(&s->resampler, &output, AV_SAMPLE_FMT_FLT, rate, &s->codec->ch_layout,
                              s->codec->sample_fmt, s->codec->sample_rate, 0, nullptr),
          "Creating music converter");
  checked(swr_init(s->resampler), "Opening music converter");
  state_ = std::move(s);
}

void MusicDecoder::rewind() {
  if (!state_) {
    return;
  }
  auto &s = *state_;
  const auto *stream = s.format->streams[s.stream];
  const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
  checked(av_seek_frame(s.format, s.stream, start, AVSEEK_FLAG_BACKWARD), "Looping music");
  avcodec_flush_buffers(s.codec);
  av_packet_unref(s.packet);
  av_frame_unref(s.frame);
  swr_close(s.resampler);
  checked(swr_init(s.resampler), "Resetting music converter");
  s.input_end = s.flush_sent = s.drained = s.pending = false;
}

std::vector<float> MusicDecoder::read() {
  if (!state_) {
    return {};
  }
  auto &s = *state_;
  while (!s.drained) {
    const int received = avcodec_receive_frame(s.codec, s.frame);
    if (received == 0) {
      const int capacity = swr_get_out_samples(s.resampler, s.frame->nb_samples);
      checked(capacity, "Sizing decoded music");
      std::vector<float> samples(static_cast<std::size_t>(capacity) * channels);
      auto *output = reinterpret_cast<std::uint8_t *>(samples.data());
      const int frames = swr_convert(s.resampler, &output, capacity,
                                     const_cast<const std::uint8_t **>(s.frame->extended_data),
                                     s.frame->nb_samples);
      av_frame_unref(s.frame);
      checked(frames, "Converting music samples");
      samples.resize(static_cast<std::size_t>(frames) * channels);
      if (!samples.empty()) {
        return samples;
      }
      continue;
    }
    if (received == AVERROR_EOF) {
      s.drained = true;
      break;
    }
    if (received != AVERROR(EAGAIN)) {
      checked(received, "Decoding music");
    }
    if (!s.pending && !s.input_end) {
      int result;
      do {
        av_packet_unref(s.packet);
        result = av_read_frame(s.format, s.packet);
      } while (result >= 0 && s.packet->stream_index != s.stream);
      if (result == AVERROR_EOF) {
        s.input_end = true;
      } else {
        checked(result, "Reading music packet");
        s.pending = true;
      }
    }
    if (s.pending) {
      const int sent = avcodec_send_packet(s.codec, s.packet);
      if (sent != AVERROR(EAGAIN)) {
        checked(sent, "Submitting music packet");
        av_packet_unref(s.packet);
        s.pending = false;
      }
    } else if (s.input_end && !s.flush_sent) {
      checked(avcodec_send_packet(s.codec, nullptr), "Finishing music decoder");
      s.flush_sent = true;
    } else {
      throw std::runtime_error("Music decoder made no progress");
    }
  }
  const int capacity = swr_get_out_samples(s.resampler, 0);
  checked(capacity, "Sizing music tail");
  std::vector<float> samples(static_cast<std::size_t>(capacity) * channels);
  auto *output = reinterpret_cast<std::uint8_t *>(samples.data());
  const int frames = swr_convert(s.resampler, &output, capacity, nullptr, 0);
  checked(frames, "Draining music converter");
  samples.resize(static_cast<std::size_t>(frames) * channels);
  return samples;
}

} // namespace phantomcrash
