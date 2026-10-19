#include "phantomcrash/frame_stats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace phantomcrash {

namespace {

// Presented frames land on the display's refresh, a little either side of
// the budget; one that missed a refresh takes about twice as long.
constexpr double on_time_slack = 1.5;

} // namespace

double FrameStats::percentile(const double percent) const {
  if (frames_.empty()) {
    return 0.0;
  }
  std::vector<double> sorted = frames_;
  std::ranges::sort(sorted);
  const double rank = std::clamp(percent, 0.0, 100.0) / 100.0 * static_cast<double>(sorted.size());
  const auto index = static_cast<std::size_t>(std::ceil(rank));
  return sorted[std::clamp<std::size_t>(index, 1, sorted.size()) - 1];
}

double FrameStats::within(const double budget_seconds) const {
  if (frames_.empty()) {
    return 0.0;
  }
  const auto met = std::ranges::count_if(frames_, [&](double t) { return t <= budget_seconds; });
  return static_cast<double>(met) / static_cast<double>(frames_.size());
}

std::string FrameStats::report(const double budget_seconds) const {
  const double total = std::accumulate(frames_.begin(), frames_.end(), 0.0);
  const double rate = total > 0.0 ? static_cast<double>(frames_.size()) / total : 0.0;
  char line[256];
  std::snprintf(line, sizeof line,
                "%zu frames, %.1f fps; ms p50 %.2f p99 %.2f p99.9 %.2f max %.2f; "
                "%.2f%% on time (under %.2f ms)",
                frames_.size(), rate, percentile(50.0) * 1e3, percentile(99.0) * 1e3,
                percentile(99.9) * 1e3, percentile(100.0) * 1e3,
                within(budget_seconds * on_time_slack) * 100.0,
                budget_seconds * on_time_slack * 1e3);
  return line;
}

} // namespace phantomcrash
