#pragma once
#include "phantomcrash/online_records.hpp"

namespace phantomcrash::online {
inline constexpr std::size_t timeline_proposal_header_bytes = 21;
inline constexpr std::size_t max_timeline_setup_bytes =
    max_record_bytes - timeline_proposal_header_bytes;

struct TimelineProposal {
  std::uint64_t timeline{};
  std::uint64_t previous{}; // prior timeline when changing an existing match
  std::uint32_t first_tick{};
  std::uint8_t peers{};            // peers required to initialize, including the host
  std::vector<std::uint8_t> setup; // application-owned initial/checkpoint description
};

// The host orders one transition at a time. Every required peer loads the
// proposal and hashes its initialized simulation before calling ready().
// No simulation may start until the matching commit is received.
class TimelineAgreement final {
public:
  enum class Phase { idle, preparing, committed, failed };
  TimelineAgreement(Room &room, RecordChannel &records) : room_(room), records_(records) {}
  void propose(TimelineProposal proposal, double now);
  bool receive(const Record &record, double now);
  void ready(const Compatibility &state);
  void reject(std::string reason);
  void update(double now);
  [[nodiscard]] Phase phase() const {
    return phase_;
  }
  [[nodiscard]] const TimelineProposal &proposal() const {
    return proposal_;
  }
  [[nodiscard]] const std::string &error() const {
    return error_;
  }

private:
  void accept_ready(std::uint8_t peer, const Compatibility &state);
  void fail(std::string reason);
  void publish(std::uint16_t kind, const std::vector<std::uint8_t> &bytes);
  Room &room_;
  RecordChannel &records_;
  TimelineProposal proposal_;
  Phase phase_{Phase::idle};
  std::array<std::optional<Compatibility>, max_players> ready_;
  std::optional<double> started_;
  std::string error_;
};
} // namespace phantomcrash::online
