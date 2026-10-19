#pragma once
#include "state_inspector.hpp"
#include "state_aggregate.hpp"
#include "../extract/sha256.hpp"
#include "phantomcrash/online_state.hpp"
#include <bit>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace phantomcrash {
struct MotionBank;
} // namespace phantomcrash

namespace phantomcrash::online {
// Const pointees whose digests AssetDigests may keep: loaded once and never
// changed or freed while a match runs (RobotLibrary keeps every bank). Weapon
// tunings are not: battles own and copy theirs.
template <class T> inline constexpr bool kept_asset = false;
template <> inline constexpr bool kept_asset<MotionBank> = true;

class StateWriter {
  struct PointerKey {
    std::type_index type;
    const void *address;
    bool operator<(const PointerKey &other) const {
      if (type != other.type) {
        return type < other.type;
      }
      return std::less<const void *>{}(address, other.address);
    }
  };
  struct Context {
    std::map<PointerKey, StateDigest> pointers;
    std::set<PointerKey> active;
    std::map<PointerKey, std::uint64_t> mutable_ids;
    AssetDigests *assets{};
  };

public:
  explicit StateWriter(AssetDigests *assets = nullptr) : context_(std::make_shared<Context>()) {
    context_->assets = assets;
  }
  template <class... T> void operator()(const T &...values) {
    (write(values), ...);
  }
  StateDigest finish() {
    flush();
    const auto hex = hash_.hex();
    StateDigest result;
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < result.size(); ++i) {
      result[i] = static_cast<std::uint8_t>(nibble(hex[i * 2]) * 16 + nibble(hex[i * 2 + 1]));
    }
    return result;
  }

private:
  explicit StateWriter(std::shared_ptr<Context> context) : context_(std::move(context)) {}
  void flush() {
    hash_.update(std::span(buffer_).first(used_));
    used_ = 0;
  }
  void byte(std::uint8_t value) {
    if (used_ == buffer_.size()) {
      flush();
    }
    buffer_[used_++] = value;
  }
  template <class T> void write(const T &value) {
    if constexpr (std::is_same_v<T, bool>) {
      byte(value ? 1 : 0);
    } else if constexpr (std::is_integral_v<T>) {
      auto bits = static_cast<std::make_unsigned_t<T>>(value);
      for (std::size_t i = 0; i < sizeof(T); ++i) {
        byte(static_cast<std::uint8_t>(bits));
        bits >>= 8;
      }
    } else if constexpr (std::is_enum_v<T>) {
      write(static_cast<std::underlying_type_t<T>>(value));
    } else if constexpr (std::is_same_v<T, float>) {
      write(std::bit_cast<std::uint32_t>(value));
    } else if constexpr (std::is_same_v<T, double>) {
      write(std::bit_cast<std::uint64_t>(value));
    } else if constexpr (requires { StateInspector::write(*this, value); }) {
      StateInspector::write(*this, value);
    } else {
      static_assert(std::is_aggregate_v<T>, "State inspection needs an explicit owner visitor");
      visit_state_aggregate(value, [this](const auto &...members) { (*this)(members...); });
    }
  }
  template <class T> void write(T *value) {
    write(value != nullptr);
    if (!value) {
      return;
    }
    // Address/type are only local lookup keys. Mutable ownership uses traversal
    // ordinals: two actors sharing one pose must differ from two independent
    // poses, even when their current values happen to match. Immutable asset
    // sharing is irrelevant, so const pointees are identified by contents only.
    const PointerKey key{typeid(std::remove_cv_t<T>), value};
    if constexpr (!std::is_const_v<T>) {
      const auto [identity, inserted] =
          context_->mutable_ids.emplace(key, context_->mutable_ids.size());
      write(identity->second);
    }
    if constexpr (std::is_const_v<T> && kept_asset<std::remove_cv_t<T>>) {
      if (context_->assets) {
        const std::pair<std::type_index, const void *> asset{key.type, key.address};
        auto known = context_->assets->known.find(asset);
        if (known == context_->assets->known.end()) {
          StateWriter child(context_);
          child(*value);
          known = context_->assets->known.emplace(asset, child.finish()).first;
        }
        write(known->second);
        return;
      }
    }
    auto found = context_->pointers.find(key);
    if (found == context_->pointers.end()) {
      if (!context_->active.insert(key).second) {
        throw std::runtime_error("Cycle in simulation-state inspection");
      }
      StateWriter child(context_);
      child(*value);
      const auto digest = child.finish();
      context_->active.erase(key);
      found = context_->pointers.emplace(key, digest).first;
    }
    write(found->second);
  }
  template <class T> void write(const std::shared_ptr<T> &value) {
    write(value.get());
  }
  template <class T> void write(const std::unique_ptr<T> &value) {
    write(value.get());
  }
  template <class T> void write(const std::optional<T> &value) {
    write(value.has_value());
    if (value) {
      write(*value);
    }
  }
  template <class A, class B> void write(const std::pair<A, B> &value) {
    (*this)(value.first, value.second);
  }
  template <class T, std::size_t N> void write(const std::array<T, N> &value) {
    for (const auto &element : value) {
      write(element);
    }
  }
  template <class T, std::size_t N> void write(const T (&value)[N]) {
    for (const auto &element : value) {
      write(element);
    }
  }
  template <class T, class A> void write(const std::vector<T, A> &value) {
    write(static_cast<std::uint64_t>(value.size()));
    for (const auto &element : value) {
      if constexpr (std::is_same_v<T, bool>) {
        write(bool(element));
      } else {
        write(element);
      }
    }
  }
  template <class K, class V, class C, class A> void write(const std::map<K, V, C, A> &value) {
    write(static_cast<std::uint64_t>(value.size()));
    for (const auto &element : value) {
      write(element);
    }
  }
  void write(const std::string &value) {
    write(static_cast<std::uint64_t>(value.size()));
    for (unsigned char c : value) {
      byte(c);
    }
  }
  template <class UInt, UInt A, UInt C, UInt M>
  void write(const std::linear_congruential_engine<UInt, A, C, M> &value) {
    // minstd_rand's one-step transition is bijective over its valid states.
    static_assert(A == 48271 && C == 0 && M == 2147483647);
    auto copy = value;
    write(static_cast<std::uint64_t>(copy()));
  }
  std::shared_ptr<Context> context_;
  extract::Sha256 hash_;
  std::array<std::uint8_t, 4096> buffer_{};
  std::size_t used_{};
};

// The contents digest of a value that never changes once built, such as a
// stage's collision or navigation. Hashing such large values again for every
// periodic digest cost a frame's worth of time, so each is remembered for as
// long as the value lives. Only for values no one can change after sharing.
template <class T, class Write>
StateDigest remembered_digest(const std::shared_ptr<const T> &value, const Write &write) {
  struct Entry {
    std::weak_ptr<const T> owner;
    StateDigest digest;
  };
  static std::mutex mutex;
  static std::unordered_map<const T *, Entry> known;
  const std::scoped_lock lock(mutex);
  if (const auto found = known.find(value.get());
      found != known.end() && found->second.owner.lock() == value) {
    return found->second.digest;
  }
  StateWriter writer;
  write(writer, *value);
  if (known.size() >= 256) {
    std::erase_if(known, [](const auto &entry) { return entry.second.owner.expired(); });
  }
  return known.insert_or_assign(value.get(), Entry{value, writer.finish()}).first->second.digest;
}
} // namespace phantomcrash::online
