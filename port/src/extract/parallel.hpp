// Runs independent conversion jobs on every core.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace phantomcrash::extract {

// Calls body(i) for every i in [0, count) across worker threads and waits.
// The first exception thrown by a job is rethrown here once all workers stop;
// jobs not yet started are skipped after a failure.
template <typename Body> void parallel_for(const std::size_t count, Body &&body) {
  const std::size_t workers =
      std::min<std::size_t>(count, std::max(1u, std::thread::hardware_concurrency()));
  std::atomic<std::size_t> next{0};
  std::atomic<bool> failed{false};
  std::exception_ptr error;
  std::mutex error_mutex;
  const auto run = [&] {
    for (std::size_t i = next++; i < count && !failed; i = next++) {
      try {
        body(i);
      } catch (...) {
        const std::scoped_lock lock(error_mutex);
        if (!error) {
          error = std::current_exception();
        }
        failed = true;
      }
    }
  };
  std::vector<std::jthread> threads;
  for (std::size_t w = 1; w < workers; ++w) {
    threads.emplace_back(run);
  }
  run();
  threads.clear();
  if (error) {
    std::rethrow_exception(error);
  }
}

} // namespace phantomcrash::extract
