#pragma once

// The VS battle's rules (0x046f81, run while save +0x11074, the VS flag, is
// set): a robot is out once its armour is gone, it has no weapon left on it
// or it is shot down. With fewer than two standing the round is decided;
// after a wait the round-end menu (RETRY, SELECT AREA, SELECT SV, EXIT)
// takes any player's input, and its choice fades out and tallies the wins
// and losses (save +0x1107c, +0x1108c) unless nobody is standing.

#include "phantomcrash/input.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

struct Combatant;

// The round's timing in the original's frames (60 a second) and its menu's
// sounds (named as data/ui/vs_mode.json's). The defaults are the original's.
struct VsRoundTuning {
  double frame_rate{60.0};
  double settle_frames{120.0};          // decided until the menu opens (0x19f884)
  double fade_frames{30.0};             // the menu's choice fades out (0x17ccfc)
  std::string move_sound{"cursor"};     // sys 3: up/down the menu (0x046f81)
  std::string answer_sound{"adjust"};   // sys 4: YES/NO
  std::string confirm_sound{"confirm"}; // sys 5
  std::string cancel_sound{"adjust"};   // sys 4: cancel moves to EXIT, or to NO
};

enum class VsRoundStage { fight, settle, menu, confirm_exit, fade, done };
enum class VsChoice { retry, select_area, select_sv, exit };
enum class VsOutcome { none, win, lose, draw };

inline constexpr int vs_menu_items = 4; // RETRY, SELECT AREA, SELECT SV, EXIT
inline constexpr int vs_menu_exit = 3;
inline constexpr int vs_answer_yes = 0; // the EXIT question (0x2036e8)
inline constexpr int vs_answer_no = 1;

// Whether robot `robot` is still in the round: armour left (+0x44 >= 1), a
// weapon still on it (0x09d6a3) and not shot down (+0x10 != 2).
[[nodiscard]] bool vs_standing(const Combatant &robot);

// The menu's buttons this tick, any player's.
struct VsMenuInput {
  bool up{}, down{}, left{}, right{}, confirm{}, cancel{};
};
[[nodiscard]] VsMenuInput vs_menu_input(const InputFrame &input);

class VsRound {
public:
  VsRound(VsRoundTuning tuning, int robots);

  // One tick: `standing` is each robot's vs_standing; once out a robot
  // stays out for the round.
  void update(const std::vector<bool> &standing, const VsMenuInput &input, double seconds);

  [[nodiscard]] VsRoundStage stage() const noexcept {
    return stage_;
  }
  [[nodiscard]] bool decided() const noexcept {
    return stage_ != VsRoundStage::fight;
  }
  [[nodiscard]] bool draw() const noexcept; // nobody standing (0x048f0c)
  [[nodiscard]] VsOutcome outcome(int robot) const;
  [[nodiscard]] int cursor() const noexcept {
    return cursor_;
  }
  [[nodiscard]] int answer() const noexcept {
    return answer_;
  }
  // Seconds since the stage began (the result's fade-in, the menu's pulse).
  [[nodiscard]] double stage_seconds() const noexcept {
    return stage_seconds_;
  }
  // 0..1 through the choice's fade-out.
  [[nodiscard]] double fade() const noexcept;
  // The menu's choice, once the round is done.
  [[nodiscard]] std::optional<VsChoice> choice() const;
  // The sounds the menu played this tick.
  [[nodiscard]] std::vector<std::string> take_sounds();
  // Adds the round to the tallies by player port (`ports` per robot), once.
  void tally(const std::vector<int> &ports, std::array<int, controller_ports> &wins,
             std::array<int, controller_ports> &losses);

private:
  void enter(VsRoundStage stage);
  void update_menu(const VsMenuInput &input);
  void update_question(const VsMenuInput &input);

  VsRoundTuning tuning_;
  std::vector<bool> standing_;
  VsRoundStage stage_{VsRoundStage::fight};
  double stage_seconds_{};
  int cursor_{};
  int answer_{vs_answer_no};
  bool tallied_{};
  std::vector<std::string> sounds_;
};

} // namespace phantomcrash
