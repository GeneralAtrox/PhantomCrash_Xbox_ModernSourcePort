#include "phantomcrash/result_presentation.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

ResultPresentation::ResultPresentation(const MatchReceipt &receipt, const json::JsonValue &data)
    : targets_{receipt.starting_money, receipt.prize,        receipt.repairs[0],
               receipt.repairs[1],     receipt.repairs[2],   receipt.repairs[3],
               receipt.repairs[4],     receipt.repair_total, receipt.total_money} {
  intro_seconds_ = std::max(0.0, data.number_or("intro_seconds", 0.5));
  reveal_seconds_ = std::max(0.0, data.number_or("row_reveal_seconds", 1.0 / 6.0));
  count_seconds_ = std::max(0.0, data.number_or("row_count_seconds", 0.5));
  rental_seconds_ = std::max(0.01, data.number_or("rental_label_seconds", 1.0));
  values_[0] = values_[8] = receipt.starting_money;
}

void ResultPresentation::update(const double seconds) {
  if (complete_) {
    seconds_ += std::max(0.0, seconds);
    return;
  }
  // The original reveals the receipt first, then counts prize, each repair,
  // repair total and wallet in sequence (0x095908, 0x09699b). Zero rows skip.
  const double count_start = intro_seconds_ + 11.0 * reveal_seconds_;
  const double before = std::max(0.0, seconds_ - count_start);
  seconds_ += std::max(0.0, seconds);
  double remaining = std::max(0.0, seconds_ - count_start) - before;
  if (seconds_ < count_start) {
    return;
  }
  while (row_ < static_cast<int>(values_.size())) {
    const std::int64_t start = row_ == 8 ? targets_[0] : 0;
    if (targets_[row_] == start || count_seconds_ == 0) {
      values_[row_] = targets_[row_];
      ++row_;
      row_elapsed_ = 0;
      continue;
    }
    const double step = std::min(remaining, count_seconds_ - row_elapsed_);
    row_elapsed_ += step;
    remaining -= step;
    const double share = std::clamp(row_elapsed_ / count_seconds_, 0.0, 1.0);
    values_[row_] = start + static_cast<std::int64_t>((targets_[row_] - start) * share);
    if (row_elapsed_ < count_seconds_) {
      break;
    }
    values_[row_] = targets_[row_];
    ++row_;
    row_elapsed_ = 0;
  }
  if (row_ == static_cast<int>(values_.size())) {
    finish();
  }
}

void ResultPresentation::finish() {
  values_ = targets_;
  row_ = static_cast<int>(values_.size());
  complete_ = true;
}

float ResultPresentation::row_alpha(const int row) const {
  if (complete_) {
    return 1.0F;
  }
  const double since = seconds_ - intro_seconds_ - std::max(row, 0) * reveal_seconds_;
  if (reveal_seconds_ == 0) {
    return since >= 0 ? 1.0F : 0.0F;
  }
  return static_cast<float>(std::clamp(since / reveal_seconds_, 0.0, 1.0));
}

bool ResultPresentation::rental_discount_label() const {
  return std::fmod(seconds_, 2.0 * rental_seconds_) >= rental_seconds_;
}

} // namespace phantomcrash
