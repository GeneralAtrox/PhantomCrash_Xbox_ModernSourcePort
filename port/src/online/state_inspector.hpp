#pragma once

#include <string>

namespace phantomcrash {
class Battle;
class RumblingWorld;
class BattleScene;
class RobotMover;
class RobotCombat;
class RobotAnimator;
class SourceLegPlayback;
class SourceRollState;
class PathFollower;
class ShownArmour;
class Armory;
class StageCollision;
class NavGraph;
class StageDoors;
class StageObjects;
class RobotWrecks;
class SupplyDrop;
struct Combatant;
namespace online {
class StateWriter;
class StateSections;
} // namespace online

// Private simulation owners explicitly enumerate their state here. Public
// aggregate records are decomposed into every member by the writer instead.
class StateInspector {
public:
  static void write(online::StateWriter &, const Battle &);
  static void write(online::StateWriter &, const RumblingWorld &);
  static void write(online::StateWriter &, const BattleScene &);
  static void write(online::StateWriter &, const Combatant &);
  static void write(online::StateWriter &, const RobotMover &);
  static void write(online::StateWriter &, const RobotCombat &);
  static void write(online::StateWriter &, const RobotAnimator &);
  static void write(online::StateWriter &, const SourceLegPlayback &);
  static void write(online::StateWriter &, const SourceRollState &);
  static void write(online::StateWriter &, const PathFollower &);
  static void write(online::StateWriter &, const ShownArmour &);
  static void write(online::StateWriter &, const Armory &);
  static void write(online::StateWriter &, const StageCollision &);
  static void write(online::StateWriter &, const NavGraph &);
  static void write(online::StateWriter &, const StageDoors &);
  static void write(online::StateWriter &, const StageObjects &);
  static void write(online::StateWriter &, const RobotWrecks &);
  static void write(online::StateWriter &, const SupplyDrop &);
  // Named parts of the same state, logged when peers' states differ.
  static void sections(online::StateSections &, const Battle &);
  static void sections(online::StateSections &, const RumblingWorld &);
  static void sections(online::StateSections &, const Armory &);
  static void sections(online::StateSections &, const Combatant &, const std::string &label);
  static void sections(online::StateSections &, const RobotMover &, const std::string &label);
  static void sections(online::StateSections &, const RobotCombat &, const std::string &label);
};
} // namespace phantomcrash
