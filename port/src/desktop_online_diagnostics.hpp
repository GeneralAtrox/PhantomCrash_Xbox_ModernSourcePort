#pragma once

namespace phantomcrash {

// Opt-in timing log for online matches. The launcher's "Online diagnostics"
// setting passes PHANGRADE_ONLINE_DIAGNOSTICS=1 to the game; starting the game
// by hand with the same variable does the same. While online, each second of
// frames writes one summary line to the SDL log.
class DesktopOnlineDiagnostics final {
public:
  DesktopOnlineDiagnostics();

  // One presented frame: its real duration, the logic ticks it ran, and whether
  // it belongs to an online match.
  void frame(double seconds, int logic_ticks, bool online);

private:
  struct Window {
    int frames{};
    int logic_ticks{};
    int max_ticks{};
    int catch_up_frames{};
    double seconds{};
    double worst_seconds{};
  };

  void report();

  bool enabled_{};
  Window window_{};
};

} // namespace phantomcrash
