#pragma once
#include "phantomcrash/settings.hpp"
#include <memory>

namespace phantomcrash {
// Modeless controls: the render loop continues while the panel is open.
class VisualsDialog final {
public:
  VisualsDialog(void *owner, Settings &settings);
  ~VisualsDialog();
  void show();
  void refresh();
  [[nodiscard]] bool open() const;
  bool take_interaction();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace phantomcrash
