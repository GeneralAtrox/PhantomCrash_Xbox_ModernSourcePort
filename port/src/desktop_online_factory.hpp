#pragma once
#include "desktop_online_assets.hpp"
#include "desktop_training.hpp"
#include "phantomcrash/online_battle_setup.hpp"
#include "phantomcrash/rumbling_battle.hpp"

namespace phantomcrash {
class DesktopHost;
void check_online_factory(DesktopHost &host, const DataRoots &roots);
void check_online_game(DesktopHost &host, const DataRoots &roots, const online::BattleSetup &setup);
void check_online_recovery(DesktopHost &host, const DataRoots &roots,
                           const online::BattleSetup &setup);

// Session-owned tables, CPU/GPU assets and UI resources. A match retains these
// independently of the campaign/menu runner that requested it.
struct OnlineBattleResources {
  OnlineBattleResources(SDL_Renderer *renderer, DataRoots roots);
  DataRoots roots;
  TextureCache textures;
  WorldRenderer world;
  RobotLibrary robots;
  std::optional<PartTables> parts;
  std::optional<GarageTables> garage;
  ContestTables contests;
  PartStatsTable stats;
  RumblingRules rules;
  TrainingScript training;
  TestDriveScript drive;
  RumblingBattle rumbling;
  RumblingLive live;
  std::map<std::string, StageDefinition> stages;
  std::array<RumblingStage, roster_areas> gameplay_stages;
  BattleHudStyle hud;
  GlyphFont font;
  TalkTables talk;
  EventTuning talk_tuning;
  DesktopMatchAssets assets;
};

struct OnlineBattleDefinition {
  std::shared_ptr<OnlineBattleResources> resources;
  online::BattleSetup setup;
  RumblingRules rules;
  std::array<TrainingScript, online::max_players> scripts;
};

std::shared_ptr<CoopMatch> build_online_battle(std::shared_ptr<OnlineBattleResources> resources,
                                               const online::BattleSetup &setup);
void join_online_battle(CoopMatch &match, int slot, const online::BattlePlayer &player);
void finish_online_battle(CoopMatch &match, int slot);
std::unique_ptr<TrainingScreen> make_online_battle_view(std::shared_ptr<CoopMatch> match, int slot,
                                                        const GameOptions &options,
                                                        BattleSounds::PlaybackQuery playing = {},
                                                        float victory_hold_seconds = 0,
                                                        std::function<void()> exit = {});
// Configure the actor with the same stat/weapon/movement functions as the
// offline training screen, before any local presentation exists.
void prepare_online_player(CoopMatch &match, int slot, const online::BattlePlayer &player,
                           const TrainingScript &script, OnlineBattleResources &resources);
} // namespace phantomcrash
