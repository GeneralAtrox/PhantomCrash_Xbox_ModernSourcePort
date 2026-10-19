// Converts the user's ILD stream to Ogg Vorbis. Container offsets are verified
// against vgmstream's ILD reader; decoded samples use the standard PS ADPCM
// predictors. This tool is used by convert-slai-music, never by the game.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

void check(const int result) {
  if (result < 0) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> error{};
    av_strerror(result, error.data(), error.size());
    throw std::runtime_error(error.data());
  }
}

std::uint32_t word(const std::vector<std::uint8_t> &bytes, const std::size_t at) {
  return bytes.at(at) | (bytes.at(at + 1) << 8U) | (bytes.at(at + 2) << 16U) |
         (static_cast<std::uint32_t>(bytes.at(at + 3)) << 24U);
}

struct Audio {
  int rate{};
  std::array<std::vector<float>, 2> samples;
};

Audio decode(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  const auto size = file.tellg();
  if (!file || size < 84 || size > 128 * 1024 * 1024) {
    throw std::runtime_error("Invalid ILD length");
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char *>(bytes.data()), size)) {
    throw std::runtime_error("Unable to read ILD");
  }
  if (word(bytes, 0) != 0x00444c49 || word(bytes, 4) != 2) {
    throw std::runtime_error("Expected a stereo ILD");
  }
  const std::size_t start = word(bytes, 8);
  const std::size_t size_bytes = word(bytes, 12);
  const std::size_t interleave = word(bytes, 0x20);
  if (start < 84 || interleave != 16 || start > bytes.size() || size_bytes > bytes.size() - start ||
      size_bytes % 32 != 0 || word(bytes, 0x48) != word(bytes, 0x28)) {
    throw std::runtime_error("Unsupported ILD layout");
  }
  Audio audio;
  audio.rate = static_cast<int>(word(bytes, 0x28));
  if (audio.rate < 8000 || audio.rate > 192000) {
    throw std::runtime_error("Invalid ILD sample rate");
  }
  const std::size_t frames = size_bytes / 32 * 28;
  for (auto &samples : audio.samples) {
    samples.reserve(frames);
  }
  constexpr std::array<std::array<int, 2>, 5> predictors{
      {{{0, 0}}, {{60, 0}}, {{115, -52}}, {{98, -55}}, {{122, -60}}}};
  std::array<std::array<int, 2>, 2> history{};
  for (std::size_t at = start; at < start + size_bytes; at += 32) {
    for (std::size_t channel = 0; channel < 2; ++channel) {
      const std::size_t block = at + channel * 16;
      const auto filter = bytes[block] >> 4U;
      const auto shift = bytes[block] & 15U;
      if (filter >= predictors.size() || shift > 12 || bytes[block + 1] > 7) {
        throw std::runtime_error("Invalid PS ADPCM block");
      }
      auto &h = history[channel];
      for (int sample = 0; sample < 28; ++sample) {
        const int packed = bytes[block + 2 + sample / 2];
        const int nibble = (packed >> (sample % 2 * 4)) & 15;
        const int signed_nibble = nibble >= 8 ? nibble - 16 : nibble;
        int value = (signed_nibble * 4096) >> shift;
        value += (h[0] * predictors[filter][0] + h[1] * predictors[filter][1] + 32) >> 6;
        if (bytes[block + 1] == 7) {
          value = 0;
        }
        value = std::clamp(value, -32768, 32767);
        h[1] = h[0];
        h[0] = value;
        audio.samples[channel].push_back(static_cast<float>(value) / 32768.0F);
      }
    }
  }
  return audio;
}

struct Writer {
  AVFormatContext *format{};
  AVCodecContext *codec{};
  AVFrame *frame{av_frame_alloc()};
  AVPacket *packet{av_packet_alloc()};
  ~Writer() {
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec);
    if (format) {
      if (format->pb) {
        avio_closep(&format->pb);
      }
      avformat_free_context(format);
    }
  }
  void packets() {
    while (true) {
      const int result = avcodec_receive_packet(codec, packet);
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
        return;
      }
      check(result);
      av_packet_rescale_ts(packet, codec->time_base, format->streams[0]->time_base);
      packet->stream_index = 0;
      check(av_interleaved_write_frame(format, packet));
      av_packet_unref(packet);
    }
  }
};

void encode(const Audio &audio, const std::filesystem::path &path) {
  Writer w;
  const auto utf8 = path.u8string();
  const auto *name = reinterpret_cast<const char *>(utf8.c_str());
  check(avformat_alloc_output_context2(&w.format, nullptr, "ogg", name));
  const auto *encoder = avcodec_find_encoder(AV_CODEC_ID_VORBIS);
  if (!encoder || !w.frame || !w.packet || !w.format) {
    throw std::runtime_error("Vorbis encoder is unavailable");
  }
  w.codec = avcodec_alloc_context3(encoder);
  if (!w.codec) {
    throw std::runtime_error("Unable to allocate Vorbis encoder");
  }
  av_channel_layout_default(&w.codec->ch_layout, 2);
  w.codec->sample_rate = audio.rate;
  w.codec->sample_fmt = AV_SAMPLE_FMT_FLTP;
  w.codec->time_base = {1, audio.rate};
  w.codec->bit_rate = 192000;
  w.codec->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
  if (w.format->oformat->flags & AVFMT_GLOBALHEADER) {
    w.codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  check(avcodec_open2(w.codec, encoder, nullptr));
  auto *stream = avformat_new_stream(w.format, nullptr);
  if (!stream) {
    throw std::runtime_error("Unable to allocate Ogg stream");
  }
  stream->time_base = w.codec->time_base;
  check(avcodec_parameters_from_context(stream->codecpar, w.codec));
  check(avio_open(&w.format->pb, name, AVIO_FLAG_WRITE));
  check(avformat_write_header(w.format, nullptr));
  w.frame->format = w.codec->sample_fmt;
  w.frame->sample_rate = audio.rate;
  check(av_channel_layout_copy(&w.frame->ch_layout, &w.codec->ch_layout));
  w.frame->nb_samples = w.codec->frame_size;
  if (w.frame->nb_samples <= 0) {
    throw std::runtime_error("Invalid encoder frame size");
  }
  check(av_frame_get_buffer(w.frame, 0));
  const auto count = audio.samples[0].size();
  for (std::size_t at = 0; at < count; at += static_cast<std::size_t>(w.codec->frame_size)) {
    check(av_frame_make_writable(w.frame));
    w.frame->nb_samples =
        static_cast<int>(std::min(count - at, static_cast<std::size_t>(w.codec->frame_size)));
    for (std::size_t channel = 0; channel < 2; ++channel) {
      std::copy_n(audio.samples[channel].data() + at, w.frame->nb_samples,
                  reinterpret_cast<float *>(w.frame->data[channel]));
    }
    w.frame->pts = static_cast<std::int64_t>(at);
    check(avcodec_send_frame(w.codec, w.frame));
    w.packets();
  }
  check(avcodec_send_frame(w.codec, nullptr));
  w.packets();
  check(av_write_trailer(w.format));
}

} // namespace

int main(const int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Usage: phantomcrash_slai_audio input.ild output.ogg\n";
    return 2;
  }
  const auto output = std::filesystem::u8path(argv[2]);
  try {
    const auto audio = decode(std::filesystem::u8path(argv[1]));
    encode(audio, output);
    std::cout << audio.samples[0].size() << " frames at " << audio.rate << " Hz\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    std::error_code ignored;
    std::filesystem::remove(output, ignored);
    return 1;
  }
}
