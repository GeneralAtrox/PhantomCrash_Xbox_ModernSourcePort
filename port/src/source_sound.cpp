#include "phantomcrash/source_sound.hpp"
#include "phantomcrash/json_fields.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {
namespace {
double field(const json::JsonValue &v, const char *key) {
  const double n = json::required(v, key).number();
  if (!std::isfinite(n)) {
    throw std::runtime_error("Non-finite source sound field");
  }
  return n;
}
unsigned integer(const json::JsonValue &v, const char *key, unsigned maximum) {
  const double n = field(v, key);
  if (n < 0 || n > maximum || std::floor(n) != n) {
    throw std::runtime_error("Invalid source sound integer");
  }
  return static_cast<unsigned>(n);
}
std::uint64_t frame(double seconds, int rate) {
  return static_cast<std::uint64_t>(std::llround(seconds * rate));
}
} // namespace

std::shared_ptr<const SourceSound> parse_source_sound(const json::JsonValue &v,
                                                      const SourceSoundLoader &load) {
  if (v.string_or("format", "") != "source_sample_sequence" ||
      integer(v, "envelope_rate", 48000) != 48000) {
    throw std::runtime_error("Unsupported source sound format or envelope clock");
  }
  auto result = std::make_shared<SourceSound>();
  result->loop_start = field(v, "loop_start_seconds");
  result->loop_end = field(v, "loop_end_seconds");
  if (!(0 <= result->loop_start && result->loop_start < result->loop_end &&
        result->loop_end <= 120)) {
    throw std::runtime_error("Invalid source sequence loop");
  }
  for (const auto &row : json::required(v, "voices").array()) {
    SourceSoundNote note;
    note.sample = load(json::required(row, "file").string());
    note.start = field(row, "start_seconds");
    note.end = field(row, "end_seconds");
    note.pitch = field(row, "pitch_ratio");
    note.gain = {static_cast<float>(field(row, "gain_left")),
                 static_cast<float>(field(row, "gain_right"))};
    note.adsr1 = integer(row, "adsr1", 65535);
    note.adsr2 = integer(row, "adsr2", 65535);
    note.loop_start = integer(row, "loop_start", static_cast<unsigned>(note.sample.pcm.size()));
    note.loop_end = integer(row, "loop_end", static_cast<unsigned>(note.sample.pcm.size()));
    note.repeat = json::required(row, "repeat_sample").boolean();
    if (note.sample.rate <= 0 || note.sample.pcm.empty() || note.start < 0 ||
        note.end <= note.start || note.end > 120 || note.start >= result->loop_end ||
        note.pitch <= 0 || note.loop_end == 0 ||
        (note.repeat && note.loop_start >= note.loop_end)) {
      throw std::runtime_error("Invalid source sound voice");
    }
    result->notes.push_back(std::move(note));
  }
  if (result->notes.empty()) {
    throw std::runtime_error("Empty source sound sequence");
  }
  return result;
}

void SourceEnvelope::release() {
  if (phase_ != 3) {
    phase_ = 3;
    counter_ = 0;
  }
}
float SourceEnvelope::step() {
  int shift = 0, step = 0;
  bool exponential = true, decrease = true;
  switch (phase_) {
  case 0:
    shift = (adsr1_ >> 10) & 31;
    step = (adsr1_ >> 8) & 3;
    exponential = (adsr1_ & 0x8000) != 0;
    decrease = false;
    break;
  case 1:
    shift = (adsr1_ >> 4) & 15;
    break;
  case 2:
    shift = (adsr2_ >> 8) & 31;
    step = (adsr2_ >> 6) & 3;
    exponential = (adsr2_ & 0x8000) != 0;
    decrease = (adsr2_ & 0x4000) != 0;
    break;
  case 3:
    shift = adsr2_ & 31;
    exponential = (adsr2_ & 32) != 0;
    break;
  }
  int increment = 32768 >> std::max(0, shift - 11);
  int change = (decrease ? -(8 - step) : 7 - step) * (1 << std::max(0, 11 - shift));
  if (exponential && !decrease && level_ > 0x6000) {
    if (shift < 10) {
      change >>= 2;
    } else if (shift >= 11) {
      increment >>= 2;
    } else {
      change >>= 1;
      increment >>= 1;
    }
  } else if (exponential && decrease) {
    change = (change * level_) >> 15;
  }
  const bool frozen =
      ((phase_ == 0 || phase_ == 2) && shift == 31 && step == 3) || (phase_ == 3 && shift == 31);
  counter_ += frozen ? increment : std::max(increment, 1);
  if (counter_ >= 32768) {
    counter_ &= 32767;
    level_ = std::clamp(level_ + change, 0, 32767);
    if (phase_ == 0 && level_ == 32767) {
      phase_ = 1;
      counter_ = 0;
    } else if (phase_ == 1 &&
               level_ <= std::min(32767, static_cast<int>((adsr1_ & 15) + 1) * 2048)) {
      phase_ = 2;
      counter_ = 0;
    }
  }
  return static_cast<float>(level_) / 32767;
}

SourceSoundPlayback::SourceSoundPlayback(std::shared_ptr<const SourceSound> source, int rate,
                                         bool repeat_sequence)
    : source_(std::move(source)), rate_(rate), repeat_(repeat_sequence) {
  if (!source_ || rate_ <= 0) {
    throw std::runtime_error("Invalid source sound playback");
  }
  loop_start_ = frame(source_->loop_start, rate_);
  loop_end_ = frame(source_->loop_end, rate_);
  if (loop_end_ <= loop_start_) {
    throw std::runtime_error("Source sequence loop is shorter than one output frame");
  }
  for (const auto &note : source_->notes) {
    schedule_.emplace_back(frame(note.start, rate_), &note);
  }
  std::stable_sort(schedule_.begin(), schedule_.end(),
                   [](const auto &a, const auto &b) { return a.first < b.first; });
}
void SourceSoundPlayback::release() {
  released_ = true;
  for (auto &voice : voices_) {
    voice.envelope.release();
  }
}
std::array<float, 2> SourceSoundPlayback::sample() {
  if (!released_) {
    while (next_note_ < schedule_.size() && schedule_[next_note_].first == frame_) {
      const auto &note = *schedule_[next_note_++].second;
      voices_.push_back({&note, SourceEnvelope(note.adsr1, note.adsr2), 0, 0,
                         frame(note.end, rate_) - frame(note.start, rate_)});
    }
  }
  std::array<float, 2> out{};
  for (auto &voice : voices_) {
    const auto &note = *voice.source;
    if (voice.age >= voice.duration) {
      voice.envelope.release();
    }
    voice.envelope_clock += 48000;
    while (voice.envelope_clock >= rate_) {
      voice.level = voice.envelope.step();
      voice.envelope_clock -= rate_;
    }
    if (voice.position >= note.loop_end) {
      if (note.repeat) {
        voice.position = note.loop_start + std::fmod(voice.position - note.loop_start,
                                                     note.loop_end - note.loop_start);
      } else {
        continue;
      }
    }
    const auto at = static_cast<std::size_t>(voice.position);
    const auto next = at + 1 < note.loop_end ? at + 1 : (note.repeat ? note.loop_start : at);
    const float value = std::lerp(note.sample.pcm[at], note.sample.pcm[next],
                                  static_cast<float>(voice.position - at)) *
                        voice.level;
    out[0] += value * note.gain[0];
    out[1] += value * note.gain[1];
    voice.position += static_cast<double>(note.sample.rate) / rate_ * note.pitch;
    ++voice.age;
  }
  std::erase_if(voices_, [](const auto &v) {
    return v.envelope.finished() || (!v.source->repeat && v.position >= v.source->loop_end);
  });
  if (!released_ && ++frame_ >= loop_end_) {
    if (repeat_) {
      frame_ = loop_start_;
      next_note_ = static_cast<std::size_t>(
          std::lower_bound(schedule_.begin(), schedule_.end(), loop_start_,
                           [](const auto &event, std::uint64_t at) { return event.first < at; }) -
          schedule_.begin());
    } else {
      released_ = true; // scheduled note-offs may extend beyond the sequence boundary
    }
  }
  return out;
}
} // namespace phantomcrash
