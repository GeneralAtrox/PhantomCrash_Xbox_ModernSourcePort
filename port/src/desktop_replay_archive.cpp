#include "desktop_replay_archive.hpp"
#include <algorithm>
#include <fcntl.h>
#include <io.h>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <cstring>
#include <zlib.h>

namespace phantomcrash {
namespace {
// Frames the writer may fall behind by before a match waits for it, bounding
// the memory held (a busy frame serializes to about 400 KB).
constexpr std::size_t max_queued_frames = 60;

class Stream {
public:
  Stream(std::vector<unsigned char> &buffer, bool reading) : buffer_(buffer), reading_(reading) {}
  template <class... T> void fields(T &...values) {
    (value(values), ...);
  }
  template <class T> void value(T &v) {
    static_assert(std::is_trivially_copyable_v<T>);
    bytes(&v, sizeof(v));
  }
  void value(std::string &v) {
    std::size_t size = v.size();
    value(size);
    if (size > 1024 * 1024) {
      throw std::runtime_error("Invalid replay string");
    }
    if (reading_) {
      v.resize(size);
    }
    bytes(v.data(), size);
  }
  template <class T> void value(std::vector<T> &v) {
    std::size_t size = v.size();
    value(size);
    if (size > 1024 * 1024) {
      throw std::runtime_error("Invalid replay array");
    }
    if (reading_) {
      v.resize(size);
    }
    if constexpr (std::is_trivially_copyable_v<T>) {
      bytes(v.data(), size * sizeof(T));
    } else {
      for (auto &item : v) {
        value(item);
      }
    }
  }
  template <class T> void value(std::optional<T> &v) {
    bool present = v.has_value();
    value(present);
    if (reading_) {
      if (present) {
        v.emplace();
      } else {
        v.reset();
      }
    }
    if (present) {
      value(*v);
    }
  }
  void value(SkyMotion &v) {
    fields(v.node, v.rotation_per_second, v.uv_per_second);
  }
  void value(WorldItem &v) {
    fields(v.robot, v.model, v.world, v.sky_nodes, v.shadow_nodes, v.two_sided_materials,
           v.lit_hidden_materials, v.sky_motion, v.sky_seconds, v.monitor_nodes, v.node_worlds,
           v.paint, v.part_paint, v.alpha, v.flash, v.node, v.colour, v.uv_offset, v.scale_x,
           v.additive, v.optical, v.casts_shadow, v.ground_shadow, v.double_sided, v.fogged,
           v.rainbow, v.paint_any, v.label_colour, v.sheen);
  }
  void value(WorldFrame &v) {
    fields(v.width, v.height, v.view, v.projection, v.eye, v.lighting, v.items, v.overlay,
           v.sprites, v.lights, v.air, v.shadow_length, v.transparent);
  }
  void value(StageLighting &v) {
    fields(v.ambient, v.sun_direction, v.sun_colour, v.fog, v.fog_colour, v.fog_start, v.fog_end,
           v.clear_colour, v.source_lights, v.shadow_colour, v.screen);
  }
  void value(ReplaySubject &v) {
    fields(v.camera, v.cockpit, v.name, v.alive, v.in_play);
  }
  void value(SoundCue &v) {
    fields(v.action, v.name, v.mix, v.loop, v.key, v.position, v.listener, v.falloff, v.bus);
  }
  void value(ReplayFrame &v) {
    fields(v.seconds, v.player, v.monitor, v.hidden_player, v.subjects, v.overlay, v.sounds,
           v.music);
  }

private:
  void bytes(void *data, std::size_t size) {
    if (!size) {
      return;
    }
    if (reading_) {
      if (cursor_ + size > buffer_.size()) {
        throw std::runtime_error("Incomplete replay frame");
      }
      std::memcpy(data, buffer_.data() + cursor_, size);
      cursor_ += size;
    } else {
      const auto *first = static_cast<const unsigned char *>(data);
      buffer_.insert(buffer_.end(), first, first + size);
    }
  }
  std::vector<unsigned char> &buffer_;
  std::size_t cursor_{};
  bool reading_;
};
} // namespace

ReplayArchive::ReplayArchive() {
  wchar_t directory[MAX_PATH], path[MAX_PATH];
  if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"pcr", 0, path)) {
    throw std::runtime_error("Cannot create replay storage");
  }
  HANDLE handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    DeleteFileW(path);
    throw std::runtime_error("Cannot open replay storage");
  }
  const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_BINARY);
  if (descriptor < 0) {
    CloseHandle(handle);
    throw std::runtime_error("Replay file handle failed");
  }
  file_ = _fdopen(descriptor, "w+b");
  if (!file_) {
    _close(descriptor);
    throw std::runtime_error("Replay file stream failed");
  }
  writer_ = std::thread([this] { write_frames(); });
}
ReplayArchive::~ReplayArchive() {
  {
    const std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  changed_.notify_all();
  writer_.join();
  std::fclose(file_);
}

void ReplayArchive::write_frames() {
  for (;;) {
    std::vector<unsigned char> buffer;
    {
      std::unique_lock lock(mutex_);
      changed_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) {
        return;
      }
      buffer = std::move(queue_.front());
      queue_.pop_front();
    }
    const uLong size = static_cast<uLong>(buffer.size());
    uLongf packed_size = compressBound(size);
    std::vector<unsigned char> packed(packed_size);
    bool ok = compress2(packed.data(), &packed_size, buffer.data(), size, Z_BEST_SPEED) == Z_OK;
    std::int64_t offset = 0;
    if (ok) {
      const std::lock_guard file(file_mutex_);
      ok = _fseeki64(file_, 0, SEEK_END) == 0 && (offset = _ftelli64(file_)) >= 0 &&
           std::fwrite(&size, sizeof(size), 1, file_) == 1 &&
           std::fwrite(&packed_size, sizeof(packed_size), 1, file_) == 1 &&
           std::fwrite(packed.data(), 1, packed_size, file_) == packed_size;
    }
    {
      const std::lock_guard lock(mutex_);
      if (ok) {
        offsets_.push_back(offset);
      } else {
        failed_ = true;
      }
    }
    changed_.notify_all();
    if (!ok) {
      return;
    }
  }
}

void ReplayArchive::append(const ReplayFrame &frame) {
  if (times_.empty()) {
    origin_ = frame.seconds;
  }
  if (!accepts(frame.seconds)) {
    return;
  }
  // Stream's writer does not mutate its input; sharing the serializer ensures
  // every field written is consumed by playback, including monitor and overlays.
  std::vector<unsigned char> buffer;
  Stream stream(buffer, false);
  stream.value(const_cast<ReplayFrame &>(frame));
  {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return failed_ || queue_.size() < max_queued_frames; });
    if (failed_) {
      throw std::runtime_error("Replay storage failed");
    }
    queue_.push_back(std::move(buffer));
  }
  changed_.notify_all();
  times_.push_back(frame.seconds - origin_);
}
ReplayFrame ReplayArchive::read(const std::size_t index) {
  if (index >= size()) {
    throw std::runtime_error("Replay frame unavailable");
  }
  std::int64_t offset = 0;
  {
    // A frame just appended may still be with the writer.
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [&] { return failed_ || offsets_.size() > index; });
    if (offsets_.size() <= index) {
      throw std::runtime_error("Replay frame unavailable");
    }
    offset = offsets_[index];
  }
  std::unique_lock file(file_mutex_);
  if (_fseeki64(file_, offset, SEEK_SET) != 0) {
    throw std::runtime_error("Replay frame unavailable");
  }
  ReplayFrame frame;
  uLong size{};
  uLongf packed_size{};
  if (std::fread(&size, sizeof(size), 1, file_) != 1 ||
      std::fread(&packed_size, sizeof(packed_size), 1, file_) != 1 || size > 128 * 1024 * 1024 ||
      packed_size > 128 * 1024 * 1024) {
    throw std::runtime_error("Invalid replay frame size");
  }
  std::vector<unsigned char> buffer(size), packed(packed_size);
  uLongf actual_size = size;
  if (std::fread(packed.data(), 1, packed_size, file_) != packed_size ||
      uncompress(buffer.data(), &actual_size, packed.data(), packed_size) != Z_OK ||
      actual_size != size) {
    throw std::runtime_error("Invalid compressed replay frame");
  }
  file.unlock();
  Stream stream(buffer, true);
  stream.value(frame);
  frame.seconds = times_[index];
  return frame;
}
std::size_t ReplayArchive::at(const double seconds) const {
  const auto it = std::upper_bound(times_.begin(), times_.end(), seconds + 1e-9);
  return it == times_.begin() ? 0 : static_cast<std::size_t>(it - times_.begin() - 1);
}
void ReplayArchive::hear(const std::vector<SoundCue> &sounds) {
  sounds_.insert(sounds_.end(), sounds.begin(), sounds.end());
}
std::vector<SoundCue> ReplayArchive::take_sounds() {
  return std::exchange(sounds_, {});
}
} // namespace phantomcrash
