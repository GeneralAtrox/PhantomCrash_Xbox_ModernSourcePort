#include "movie_decoder.hpp"

#include "phantomcrash/movie_clock.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <array>
#include <mutex>
#include <stdexcept>

namespace phantomcrash {
namespace {

std::string ffmpeg_error(const int code) {
  std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
  av_strerror(code, buffer.data(), buffer.size());
  return buffer.data();
}

AVCodecContext *open_decoder(AVFormatContext *format, const int stream) {
  const AVCodecParameters *parameters = format->streams[stream]->codecpar;
  const AVCodec *codec = avcodec_find_decoder(parameters->codec_id);
  if (codec == nullptr) {
    throw std::runtime_error("No FFmpeg decoder for movie stream " + std::to_string(stream));
  }
  AVCodecContext *context = avcodec_alloc_context3(codec);
  if (context == nullptr) {
    throw std::runtime_error("Unable to allocate an FFmpeg decoder");
  }
  int result = avcodec_parameters_to_context(context, parameters);
  if (result >= 0) {
    context->thread_count = 0; // automatic, for high-resolution replacement movies
    result = avcodec_open2(context, codec, nullptr);
  }
  if (result < 0) {
    avcodec_free_context(&context);
    throw std::runtime_error("Unable to open FFmpeg decoder: " + ffmpeg_error(result));
  }
  return context;
}

} // namespace

MovieDecoder::MovieDecoder() {
  // The Sofdec streams carry harmless timestamp oddities FFmpeg warns about
  // (see AVFMT_FLAG_IGNDTS below); only errors are worth showing.
  static std::once_flag quiet;
  std::call_once(quiet, [] { av_log_set_level(AV_LOG_ERROR); });
  packet_ = av_packet_alloc();
  decoded_ = av_frame_alloc();
  if (packet_ == nullptr || decoded_ == nullptr) {
    av_frame_free(&decoded_);
    av_packet_free(&packet_);
    throw std::runtime_error("Unable to allocate FFmpeg packet/frame state");
  }
}

MovieDecoder::~MovieDecoder() {
  close();
  av_frame_free(&decoded_);
  av_packet_free(&packet_);
}

void MovieDecoder::open(const std::filesystem::path &path) {
  close();
  try {
    format_ = avformat_alloc_context();
    if (format_ == nullptr) {
      throw std::runtime_error("Unable to allocate the FFmpeg demuxer");
    }
    // Some Sofdec packets carry a DTS one frame after their PTS; presentation
    // timestamps are authoritative (as in the Lodoss port).
    format_->flags |= AVFMT_FLAG_IGNDTS;
    const auto utf8 = path.u8string();
    const std::string name(utf8.begin(), utf8.end());
    int result = avformat_open_input(&format_, name.c_str(), nullptr, nullptr);
    if (result < 0) {
      format_ = nullptr; // freed by avformat_open_input on failure
      throw std::runtime_error("Unable to open movie " + name + ": " + ffmpeg_error(result));
    }
    result = avformat_find_stream_info(format_, nullptr);
    if (result < 0) {
      throw std::runtime_error("Unable to inspect movie " + name + ": " + ffmpeg_error(result));
    }
    video_stream_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_ < 0) {
      throw std::runtime_error("Movie " + name + " has no video stream");
    }
    audio_stream_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, video_stream_, nullptr, 0);
    video_decoder_ = open_decoder(format_, video_stream_);
    width_ = video_decoder_->width;
    height_ = video_decoder_->height;
    if (width_ <= 0 || height_ <= 0) {
      throw std::runtime_error("Movie " + name + " has invalid video dimensions");
    }
    const AVRational rate = av_guess_frame_rate(format_, format_->streams[video_stream_], nullptr);
    frame_duration_seconds_ = rate.num > 0 && rate.den > 0 ? av_q2d(av_inv_q(rate)) : 1.0 / 30.0;
    start_seconds_ = format_->start_time != AV_NOPTS_VALUE
                         ? static_cast<double>(format_->start_time) / AV_TIME_BASE
                         : 0.0;
    duration_seconds_ =
        format_->duration > 0 ? static_cast<double>(format_->duration) / AV_TIME_BASE : 0.0;

    if (audio_stream_ >= 0) {
      audio_decoder_ = open_decoder(format_, audio_stream_);
      AVChannelLayout input_layout{};
      if (audio_decoder_->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
        av_channel_layout_default(&input_layout, audio_decoder_->ch_layout.nb_channels);
      } else {
        av_channel_layout_copy(&input_layout, &audio_decoder_->ch_layout);
      }
      AVChannelLayout output_layout{};
      av_channel_layout_default(&output_layout, output_channels);
      result = swr_alloc_set_opts2(&resampler_, &output_layout, AV_SAMPLE_FMT_FLT,
                                   output_sample_rate, &input_layout, audio_decoder_->sample_fmt,
                                   audio_decoder_->sample_rate, 0, nullptr);
      av_channel_layout_uninit(&input_layout);
      av_channel_layout_uninit(&output_layout);
      if (result < 0 || resampler_ == nullptr || swr_init(resampler_) < 0) {
        throw std::runtime_error("Unable to create the movie audio converter");
      }
    }
  } catch (...) {
    close();
    throw;
  }
}

void MovieDecoder::close() noexcept {
  while (!frames_.empty()) {
    pop_front();
  }
  swr_free(&resampler_);
  sws_freeContext(converter_);
  converter_ = nullptr;
  avcodec_free_context(&audio_decoder_);
  avcodec_free_context(&video_decoder_);
  if (format_ != nullptr) {
    avformat_close_input(&format_);
  }
  if (packet_ != nullptr) {
    av_packet_unref(packet_);
  }
  if (decoded_ != nullptr) {
    av_frame_unref(decoded_);
  }
  video_stream_ = audio_stream_ = -1;
  width_ = height_ = 0;
  start_seconds_ = duration_seconds_ = frame_duration_seconds_ = 0.0;
  last_video_seconds_ = -1.0;
  last_audio_end_seconds_ = 0.0;
  input_eof_ = packet_pending_ = video_flush_sent_ = audio_flush_sent_ = false;
  video_drained_ = audio_drained_ = false;
}

void MovieDecoder::pop_front() noexcept {
  av_frame_free(&frames_.front().frame);
  frames_.pop_front();
}

double MovieDecoder::seconds(const AVFrame &frame, const int stream) const {
  if (frame.best_effort_timestamp == AV_NOPTS_VALUE) {
    return -1.0;
  }
  return static_cast<double>(frame.best_effort_timestamp) *
             av_q2d(format_->streams[stream]->time_base) -
         start_seconds_;
}

void MovieDecoder::receive_video() {
  while (!video_drained_ && frames_.size() < frame_capacity) {
    const int result = avcodec_receive_frame(video_decoder_, decoded_);
    if (result == AVERROR(EAGAIN)) {
      return;
    }
    if (result == AVERROR_EOF) {
      video_drained_ = true;
      return;
    }
    if (result < 0) {
      throw std::runtime_error("Unable to decode movie video: " + ffmpeg_error(result));
    }
    const double stamp = seconds(*decoded_, video_stream_);
    last_video_seconds_ = movie::decoded_video_frame_seconds(
        stamp >= -0.5, stamp, last_video_seconds_, frame_duration_seconds_, 0.0);
    AVFrame *kept = av_frame_alloc();
    if (kept == nullptr) {
      throw std::runtime_error("Unable to allocate a movie frame");
    }
    if (decoded_->format == AV_PIX_FMT_YUV420P) {
      av_frame_move_ref(kept, decoded_);
    } else {
      // Other layouts (replacement movies) are converted to 4:2:0 once here.
      converter_ = sws_getCachedContext(
          converter_, width_, height_, static_cast<AVPixelFormat>(decoded_->format), width_,
          height_, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
      kept->format = AV_PIX_FMT_YUV420P;
      kept->width = width_;
      kept->height = height_;
      if (converter_ == nullptr || av_frame_get_buffer(kept, 0) < 0) {
        av_frame_free(&kept);
        throw std::runtime_error("Unable to convert a movie frame");
      }
      sws_scale(converter_, decoded_->data, decoded_->linesize, 0, height_, kept->data,
                kept->linesize);
      av_frame_unref(decoded_);
    }
    frames_.push_back({last_video_seconds_, kept});
  }
}

void MovieDecoder::queue_audio(const AudioSink &sink, const std::uint8_t **input,
                               const int input_frames, const double start) {
  double at = start;
  while (true) {
    const int capacity = swr_get_out_samples(resampler_, input_frames);
    if (capacity <= 0) {
      return;
    }
    audio_buffer_.resize(static_cast<std::size_t>(capacity) * output_channels);
    auto *output = reinterpret_cast<std::uint8_t *>(audio_buffer_.data());
    const int converted = swr_convert(resampler_, &output, capacity, input, input_frames);
    if (converted < 0) {
      throw std::runtime_error("Unable to convert movie audio: " + ffmpeg_error(converted));
    }
    if (converted > 0) {
      sink(audio_buffer_.data(), converted, at);
      at += static_cast<double>(converted) / output_sample_rate;
      last_audio_end_seconds_ = at;
    }
    // With input, one call converts it all; draining repeats until empty.
    if (input != nullptr || converted == 0) {
      return;
    }
  }
}

void MovieDecoder::receive_audio(const AudioSink &sink) {
  while (audio_decoder_ != nullptr && !audio_drained_) {
    const int result = avcodec_receive_frame(audio_decoder_, decoded_);
    if (result == AVERROR(EAGAIN)) {
      return;
    }
    if (result == AVERROR_EOF) {
      queue_audio(sink, nullptr, 0, last_audio_end_seconds_);
      audio_drained_ = true;
      return;
    }
    if (result < 0) {
      throw std::runtime_error("Unable to decode movie audio: " + ffmpeg_error(result));
    }
    const double stamp = seconds(*decoded_, audio_stream_);
    queue_audio(sink, const_cast<const std::uint8_t **>(decoded_->extended_data),
                decoded_->nb_samples, stamp >= -0.5 ? stamp : last_audio_end_seconds_);
    av_frame_unref(decoded_);
  }
}

void MovieDecoder::submit_packet() {
  AVCodecContext *decoder = packet_->stream_index == video_stream_   ? video_decoder_
                            : packet_->stream_index == audio_stream_ ? audio_decoder_
                                                                     : nullptr;
  const int result = decoder != nullptr ? avcodec_send_packet(decoder, packet_) : 0;
  if (result == AVERROR(EAGAIN)) {
    return; // the decoder has output waiting; retried after the next receive
  }
  av_packet_unref(packet_);
  packet_pending_ = false;
  // A damaged packet costs a frame or a few samples, not the whole movie.
  if (result < 0 && result != AVERROR_INVALIDDATA) {
    throw std::runtime_error("Unable to submit a movie packet: " + ffmpeg_error(result));
  }
}

void MovieDecoder::send_flush() {
  const auto flush = [](AVCodecContext *decoder, bool &sent) {
    if (decoder == nullptr || sent) {
      return;
    }
    const int result = avcodec_send_packet(decoder, nullptr);
    if (result == 0 || result == AVERROR_EOF) {
      sent = true;
    } else if (result != AVERROR(EAGAIN)) {
      throw std::runtime_error("Unable to flush a movie decoder: " + ffmpeg_error(result));
    }
  };
  flush(video_decoder_, video_flush_sent_);
  flush(audio_decoder_, audio_flush_sent_);
}

void MovieDecoder::pump(const AudioSink &sink) {
  if (!is_open()) {
    return;
  }
  for (int work = 0; work < 4096; ++work) {
    receive_video();
    receive_audio(sink);
    if (frames_.size() >= frame_capacity) {
      return;
    }
    if (packet_pending_) {
      submit_packet();
      continue;
    }
    if (input_eof_) {
      send_flush();
      receive_video();
      receive_audio(sink);
      return;
    }
    const int result = av_read_frame(format_, packet_);
    if (result == AVERROR_EOF) {
      input_eof_ = true;
    } else if (result < 0) {
      throw std::runtime_error("Unable to read a movie packet: " + ffmpeg_error(result));
    } else {
      packet_pending_ = true;
    }
  }
}

} // namespace phantomcrash
