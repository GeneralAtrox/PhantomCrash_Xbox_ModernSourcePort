#pragma once

#include "phantomcrash/match_result.hpp"

namespace phantomcrash {

// Presentation owns displayed values only. Settlement remains outside this clock.
class ResultPresentation final {
public:
  ResultPresentation(const MatchReceipt &receipt, const json::JsonValue &data);
  void update(double seconds);
  void finish();
  [[nodiscard]] bool complete() const noexcept {
    return complete_;
  }
  [[nodiscard]] const std::array<std::int64_t, 9> &values() const noexcept {
    return values_;
  }
  [[nodiscard]] float row_alpha(int row) const;
  [[nodiscard]] int counting_row() const noexcept {
    return row_;
  }
  [[nodiscard]] bool rental_discount_label() const;

private:
  std::array<std::int64_t, 9> targets_{}, values_{};
  double seconds_{}, row_elapsed_{};
  double intro_seconds_{}, reveal_seconds_{}, count_seconds_{}, rental_seconds_{};
  int row_{1};
  bool complete_{};
};

} // namespace phantomcrash
