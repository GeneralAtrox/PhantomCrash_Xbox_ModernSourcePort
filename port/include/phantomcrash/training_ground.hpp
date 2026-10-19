#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/battle_monitor.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/post_process.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/robot_load.hpp"
#include "phantomcrash/training_battle.hpp"
#include "phantomcrash/source_lighting.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

// How a stage is lit and fogged. The original sets these per stage in code
// (the per-stage function table at 0x22e928); the values are port choices
// until they are recovered.
struct StageLighting {
  Vec3 ambient{0.45F, 0.45F, 0.45F};
  Vec3 sun_direction{-0.4F, -0.8F, 0.45F}; // the way the light travels
  Vec3 sun_colour{0.75F, 0.75F, 0.72F};
  bool fog{};
  Vec3 fog_colour{0.7F, 0.8F, 0.9F};
  float fog_start{300.0F};
  float fog_end{2500.0F};
  Vec3 clear_colour{0.0F, 0.0F, 0.0F};
  std::vector<SourceLightVolume> source_lights;
  // What the robots' shadow volumes lay over the stage (0x0b6b80, the
  // colour table at 0x2014e8 per stage): 0x70030303 on most stages.
  std::array<float, 4> shadow_colour{3 / 255.0F, 3 / 255.0F, 3 / 255.0F, 0x70 / 255.0F};
  ScreenPass screen;
};

// Sky object animation tags at 0x2212ec, applied by 0x06ea05. Rates are
// per second, converted from the original battle's 30 Hz updates.
struct SkyMotion {
  std::string node;
  float rotation_per_second{};
  std::array<float, 2> uv_per_second{};
};

// One battle stage of the stage table at 0x2212ac (data/world/stages.json).
struct StageDefinition {
  std::string name;
  std::string model;                     // asset path of its glTF, empty until converted
  std::string collision;                 // asset path of its converted collision, or empty
  std::string collision_fixes;           // data document of port collision fixes, or empty
  std::string enemy_path;                // asset path of its drones' path (enepath/), or empty
  std::vector<std::string> sky_nodes;    // drawn around the camera, behind everything
  std::vector<std::string> shadow_nodes; // authored layer-7 stencil volumes
  std::vector<std::string> two_sided_textures; // stage material overrides, by base texture asset
  std::vector<SkyMotion> sky_motion;
  std::vector<std::string> monitor_nodes; // live arena TV surfaces, never the shared sky texture
  BattleMonitorSettings monitor;
  StageLighting lighting;
  int scene_stage{-1};       // its number in the original's tables (0x2212ac), -1 for the script's
  std::string door_model;    // the effect model of its gate doors, empty for the script's
  std::string props_model;   // placed city scenery, separate from the moving gate leaves
  std::optional<Vec3> spawn; // imported arenas' adapter-selected standing point
  std::vector<Vec3> player_spawns;
  std::optional<SupplyPickupLook> supply_pickups;
  std::optional<std::vector<SupplyRoute>> supply_routes;
};

[[nodiscard]] std::map<std::string, StageDefinition> parse_stages(const json::JsonValue &document);

// The scenes' gates, doors and cameras for the stage the battle is on.
void apply_stage(SceneTuning &scenes, const StageDefinition &stage);

// The tutorial battle (scene 42), opened by one of Pepper's lessons. The
// guide's lesson (0x2928c) picks the save's mode at +0x10f10: FreePlay 3,
// basic controls 0, attack methods 1, Optic Camouflage 2.
struct TrainingLesson {
  std::string title;
  int mode{};
  // Split screen (split_screen.hpp): the screens, each following robots
  // 0, 1, ... A port test of the versus mode's split HUD; 1 in the
  // original's lessons.
  int screens{1};
};

struct TrainingScript {
  std::string stage;
  std::string music;
  // FreePlay battles play the player's music program; a ranker's contest
  // plays its theme (`music`) instead (0x09e24c).
  bool music_program{true};
  std::string return_to;
  std::map<std::string, TrainingLesson> lessons; // screen name -> lesson
  std::map<std::string, RobotParts> robots;      // "player", "drone", ...
  TrainingBattle battle;
};

[[nodiscard]] TrainingScript parse_training(const json::JsonValue &document);

// What the training robot `name` carries: its body and legs (script.robots)
// at the robot's levels, its chip and the parts its weapons name (battle.settings).
[[nodiscard]] RobotLoadout training_loadout(const TrainingScript &script, const std::string &name,
                                            const TrainingRobot &robot);

} // namespace phantomcrash
