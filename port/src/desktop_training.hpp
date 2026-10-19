#pragma once

#include "desktop_battle_chatter.hpp"
#include "desktop_battle_hud.hpp"
#include "desktop_battle_scene.hpp"
#include "desktop_battle_view.hpp"
#include "desktop_cockpit.hpp"
#include "desktop_effects.hpp"
#include "desktop_mech_vfx.hpp"
#include "desktop_point_lights.hpp"
#include "desktop_shots.hpp"
#include "desktop_screen.hpp"
#include "desktop_supply.hpp"
#include "desktop_monitor_ship.hpp"
#include "desktop_pause_menu.hpp"
#include "desktop_textures.hpp"
#include "desktop_replay_archive.hpp"
#include "desktop_result_backdrop.hpp"
#include "desktop_world_renderer.hpp"
#include "desktop_online_input.hpp"
#include "phantomcrash/shop_dialog.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/match_result.hpp"
#include "phantomcrash/battle_sounds.hpp"
#include "phantomcrash/battle_camera.hpp"
#include "phantomcrash/chase_camera.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/event_player.hpp"
#include "phantomcrash/finale.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/robot_library.hpp"
#include "phantomcrash/rumbling_world.hpp"
#include "phantomcrash/split_screen.hpp"
#include "phantomcrash/training_ground.hpp"
#include "phantomcrash/tutorial.hpp"
#include "phantomcrash/vs_battle.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {
struct CoopMatch;
struct TrainingFinale;
struct TrainingVs;
namespace online {
class Presentation;
}

// Everything the tutorial battle reads that outlives the screen.
struct TrainingContext {
  const DataRoots &roots;
  WorldRenderer *world; // null or unavailable: nothing 3D is drawn
  RobotLibrary *robots; // null or unavailable: no battle
  const TrainingScript &script;
  const StageDefinition *stage;
  const TalkTables &talk;
  EventTuning tuning;
  std::string player_name;
  bool standard_controls{true}; // BUTTON CONFIG's CONTROL TYPE is Standard
  int day{};                    // the quest's day and money, for the HUD
  std::int64_t money{};
  // The live RUMBLING's battle the player joins (rumbling_world.hpp), its
  // opponents already out, and the world they come from; null for a
  // battle of the screen's own.
  std::shared_ptr<Battle> live_battle{};
  const RumblingWorld *live_world{};
  int live_area{-1};
  bool match_results{};
  std::shared_ptr<CoopMatch> coop_match;
  int coop_slot{-1};
  // The VS battle's rules and the session its round tallies into; null
  // elsewhere (the script's riders are the players' robots).
  const VsBattle *vs{};
  VsSession *vs_session{};
  // The finale's epilogue (finale.hpp), and the share of its armour and
  // the weapons the robot won the contest with; null elsewhere.
  const FinaleTables *finale{};
  const FinaleTuning *finale_tuning{};
  float finale_armour{1.0F};
  // A won match stands this long before it ends: the First Ranker's first
  // clear (story/progression.json clear_hold_frames).
  float victory_hold_seconds{};
  // The RUMBLING contest's ranker, if it is a ranker's, and its area: a
  // boss shot down then holds the battle in its defeat (0x089861).
  RankerContest ranker{RankerContest::none};
  int ranker_area{-1};
  std::array<bool, weapon_slots> finale_lost{};
  bool beginner{}; // BUTTON CONFIG's BEGINNER MODE
  ControlOptions controls{}; // its assignments and VIEW CHANGE TYPE
  BattleSounds::PlaybackQuery sound_playing;
  std::function<void()> online_exit;
  std::optional<std::uint32_t>
      scene_seed; // agreed online entrance seed; offline keeps its clock seed
};

// The tutorial battle (scene 42) on the training ground, opened by one of
// Pepper's lessons: the player's robot against the drones walking the
// stage's enemy path, with Pepper's messages leading through the lesson's
// steps (robot states 0x18-0x2e) and back to her when the course ends.
// FreePlay has no course: the robot walks out of a gate and the battle
// runs until it walks back into one, or chooses EXIT in the pause overlay.
class TrainingScreen final : public Screen, public BackdropBattle {
public:
  [[nodiscard]] bool mouse_aim() const override {
    return !paused_;
  }
  [[nodiscard]] bool gameplay_paused() const override {
    return paused_ && !coop_match_;
  }
  TrainingScreen(TextureCache &textures, const GlyphFont &font, const TrainingContext &context,
                 const TrainingLesson &lesson);
  ~TrainingScreen() override;

  void tick(const InputFrame &input, double tick_seconds) override;
  // Register the existing entrance scene before creating the agreed timeline.
  void prepare_online();
  void draw(const Viewport &viewport) override;
  // Battle music ends when entering the exit elevator or leaving the screen;
  // a match keeps it through the exit until RESULT takes over.
  [[nodiscard]] std::string music() const override;
  [[nodiscard]] bool uses_music_program() const override;
  [[nodiscard]] double music_program_gap() const override {
    return script_.battle.scenes.regular_battle ? script_.battle.scenes.music_gap_seconds : 0.0;
  }
  void set_music(const std::string &file) override {
    heard_music_ = file;
    if (file.empty() && !finale_) {
      cue_music_.clear(); // the jingle is over (or would not play)
    }
  }
  [[nodiscard]] std::vector<std::string> take_sounds() override;
  // The battle's sounds, placed as the camera hears them
  // (desktop_training_sound.cpp).
  [[nodiscard]] std::vector<SoundCue> take_sound_cues() override;
  // The player's robot is port 1's (the first robot, Combatant::piloted).
  [[nodiscard]] std::vector<DamageRumble> take_rumble() override {
    return std::exchange(rumble_, {});
  }
  [[nodiscard]] std::vector<std::string> sound_preloads() const override;
  [[nodiscard]] bool done() const override {
    return done_;
  }
  [[nodiscard]] std::string next() const override {
    return finale_ ? finale_next() : match_results_ ? "match_result" : vs_next();
  }
  // Kind, stage, clock, camera and robots (desktop_training_dump.cpp).
  [[nodiscard]] json::JsonValue scene_details() const override;
  [[nodiscard]] std::optional<SceneProgress> scene_progress() const override;
  void set_cheats(const CheatOptions &cheats) override {
    if (battle_ && !online()) {
      battle_->set_cheats({cheats.never_die, cheats.infinite_ammo});
    }
  }
  [[nodiscard]] MatchSummary match_summary() const;
  std::int64_t settle_coop(QuestSave &save);
  [[nodiscard]] bool coop_contest_won() const;
  [[nodiscard]] WorldFrame result_background();
  [[nodiscard]] WorldRenderer *result_renderer() const {
    return world_;
  }
  [[nodiscard]] std::shared_ptr<void> result_resources() const;
  std::shared_ptr<ReplayArchive> replay() const {
    return replay_;
  }
  [[nodiscard]] std::int64_t extra_prize() const {
    return extra_prize_;
  }
  // The live RUMBLING area whose contest robots can stand up behind the
  // receipt, or -1 (desktop_training_backdrop.cpp).
  [[nodiscard]] int backdrop_area() const;
  // That area's contest robots stood up afresh: from the co-op match's
  // world and save, or else from `world` (the player's) with `quest`.
  [[nodiscard]] std::shared_ptr<Battle> stand_up_backdrop(RumblingWorld *world,
                                                          const QuestSave &quest) const;
  // Turns this screen into the receipt's backdrop: `fresh`
  // (RumblingWorld::stand_up) runs on this stage, drawn without the HUD
  // and unheard. `looks` are its robots' (stand_up_contest), otherwise the
  // live area's are. False when it cannot.
  bool start_backdrop(std::shared_ptr<Battle> fresh, const std::vector<RobotParts> &looks = {});
  void tick_backdrop(float seconds) override;
  [[nodiscard]] std::vector<ReplaySubject> backdrop_subjects() const override;
  [[nodiscard]] const StageCollision &backdrop_collision() const override;
  [[nodiscard]] ChaseCameraSettings backdrop_chase() const override;
  [[nodiscard]] CameraViewSettings backdrop_views() const override;
  [[nodiscard]] std::vector<Vec3> backdrop_fixed_points() const override;
  [[nodiscard]] WorldFrame backdrop_frame(const ChaseCamera &camera, int width,
                                          int height) override;

private:
  [[nodiscard]] bool battle_music_ended() const;
  void tick_music_start(double seconds);
  bool online() const;
  void capture_online(const RobotControl &control, const FireControl &fire);
  bool online_result_ready() const;
  void present_online();
  void tick_effects(double seconds);
  // The live checklist's marked tags for the name tags' skulls, now and then.
  void refresh_marked_tags(float seconds);
  std::shared_ptr<online::Presentation> online_presentation_;
  std::function<void()> online_exit_;
  bool online_exit_requested_{}, online_settled_{};
  online::PendingInput pending_online_input_;
  std::optional<TargetView> local_target_view_;
  std::shared_ptr<CoopMatch> coop_match_;
  int coop_slot_{-1};
  int player_index_{};
  int coop_revision_{-1};
  std::vector<RobotParts> coop_looks_;
  void sync_coop_views();
  // VS (desktop_training_vs.cpp): the riders, their controls, the round,
  // and what it draws over the screens.
  std::unique_ptr<TrainingVs> vs_;
  void start_vs(const TrainingContext &context);
  void seat_riders(const std::optional<PartTables> &parts,
                   const std::optional<GarageTables> &garage);
  void place_riders(const DataRoots &roots);
  void vs_controls(const InputFrame &input, bool locked);
  void tick_vs(const InputFrame &input, double seconds);
  [[nodiscard]] std::string vs_next() const;
  void draw_vs_result(const Viewport &screen, std::size_t index);
  void draw_vs_covers(const Viewport &viewport);
  void draw_vs(const Viewport &viewport);
  // The finale (desktop_training_finale.cpp): its phases walk and film the
  // player's robot, with their lines, banners and fades.
  std::unique_ptr<TrainingFinale> finale_;
  void start_finale(const TrainingContext &context);
  void tick_finale(double seconds);
  void tick_finale_lights(double seconds);
  void add_finale_items(std::vector<WorldItem> &items);
  [[nodiscard]] std::optional<CameraShot> finale_camera() const;
  [[nodiscard]] std::string finale_music() const;
  [[nodiscard]] std::string finale_next() const;
  [[nodiscard]] bool finale_done() const;
  void draw_finale(const Viewport &viewport);
  void load_pause(const DataRoots &roots);
  bool tick_pause(const InputFrame &input);
  void draw_pause(const Viewport &viewport);
  bool paused_{};
  PauseMenu pause_menu_;
  void start_recording(const TrainingContext &context);
  void begin_recording();
  void record_world(const WorldFrame &frame, const std::optional<WorldFrame> &monitor);
  void finish_recording();
  [[nodiscard]] ReplaySubject replay_subject(std::size_t index) const;
  void update_result(double seconds);
  void load_stage_art(const StageDefinition *stage, const DataRoots &roots);
  void build_battle(const TrainingContext &context);
  // The live RUMBLING (desktop_training_live.cpp): the player's robot
  // takes robot 0's place, and each opponent brought in since is drawn as
  // itself.
  void seat_player(const BattleSettings &settings, float hp);
  void add_live_views();
  void follow_live();
  void load_messages(const DataRoots &roots, bool standard_controls);
  void update_message(const InputFrame &input, double tick_seconds);
  void tick_battle(const InputFrame &input, float seconds);
  void load_sounds(const DataRoots &roots, BattleSounds::PlaybackQuery playing);
  // The robots', doors' and pickups' sounds of the tick just run.
  void gather_sounds(float seconds);
  void queue_dialogue(std::vector<std::string> sounds);
  // The view buttons, then the chase camera and the view on top of it
  // (desktop_training_camera.cpp).
  void tick_camera(const InputFrame &input, float seconds);
  void update_target_view();
  // What the cameras follow of robot `index`.
  [[nodiscard]] CameraSubject camera_subject(std::size_t index) const;
  // Whether the camera sits in the player's robot, and whether (and how
  // far slid out of view) the cockpit is up.
  [[nodiscard]] bool in_cockpit() const;
  [[nodiscard]] bool cockpit_up() const;
  [[nodiscard]] CockpitInput cockpit_input() const;
  // desktop_training_defeat.cpp
  void observe_defeat();
  [[nodiscard]] std::optional<CameraShot> defeat_shot(float seconds, const SightTest &sees);
  [[nodiscard]] bool in_scene() const {
    return finale_ || (scene_ && scene_->scene().scripted());
  }
  [[nodiscard]] BattleHudInfo hud_info() const;
  // This match's earnings so far, in stored units (the HUD's MONEY).
  [[nodiscard]] std::int64_t match_prize() const;
  // Split screen (desktop_training_split.cpp): the further screens'
  // cameras and HUDs, their ticks, and one screen's world and HUD.
  void build_split(const DataRoots &roots);
  void tick_split(float seconds);
  void draw_screen(const Viewport &screen, std::size_t index);
  [[nodiscard]] WorldFrame battle_frame(const ChaseCamera &camera, int width, int height,
                                        int hidden_robot);
  void draw_message(const Viewport &viewport);
  void draw_notice(const std::vector<int> &run, const Viewport &viewport);

  TextureCache &textures_;
  const GlyphFont &font_;
  WorldRenderer *world_;
  const TrainingScript &script_;
  const TalkTables &talk_;
  EventTuning tuning_;
  StageLighting lighting_;
  std::vector<WorldLight> local_lights_;
  VolumetricAir air_; // the stage's air for the lights to show in
  WorldItem stage_;
  std::unique_ptr<BattleMonitor> monitor_;
  std::unique_ptr<MonitorShip> monitor_ship_;
  WorldItem props_;
  int object_model_{-1};
  int mode_{};
  bool free_play_{};
  LessonMessages messages_;
  Tutorial tutorial_;
  ChaseCamera camera_;
  float target_aspect_{4.0F / 3.0F}; // updated from the player's actual viewport
  std::array<int, weapon_slots> target_candidates_{-1, -1, -1, -1}; // transition logging only
  CameraViews views_;
  std::shared_ptr<Battle> battle_; // the live RUMBLING's is shared with its world
  const RumblingWorld *live_world_{};
  int live_area_{-1};
  float marked_refresh_{}; // seconds until the marked tags are read again
  int live_changes_{};
  std::vector<int> live_entrants_; // drawn as, by battle robot
  std::unique_ptr<BattleView> view_;
  std::unique_ptr<BattleHud> hud_;
  // Screens after the first, each following its own robot; empty with one
  // screen.
  struct SplitView {
    int robot{};
    ChaseCamera camera;
    std::unique_ptr<BattleHud> hud;
    bool placed{}; // the camera has been put behind its robot
  };
  int screens_{1};
  std::vector<SplitView> splits_;
  std::unique_ptr<DesktopCockpit> cockpit_; // null without its model
  std::unique_ptr<ChatterWindow> chatter_;  // null without its lines
  // FreePlay's entrance and exit; null in the lessons, which start where
  // the script puts the robot.
  std::unique_ptr<BattleScenePresenter> scene_;
  int door_model_{-1};                        // the gate doors' leaves
  std::unique_ptr<DesktopSupply> supply_;     // FreePlay's; null without its model
  std::unique_ptr<DesktopEffects> effects_;   // null without the effect bank
  std::unique_ptr<DesktopMechVfx> mech_vfx_;  // MechAssault's weapon effects, when modded in
  std::unique_ptr<DesktopShots> shots_;       // with the effects, whose bank it draws
  std::unique_ptr<PointLights> point_lights_; // fires, blasts and shots lighting the stage
  std::string cue_music_;
  std::string heard_music_; // the actual song, including the saved program, for replay
  // A regular battle's music waits for the entrance: silence while the
  // robot walks out, the play-in jingle, then music_gap_seconds of silence
  // before the program or the battle's own track (0x09e24c).
  enum class MusicStart { entrance, play_in, gap, open };
  MusicStart music_start_{MusicStart::open};
  bool entrance_seen_{}; // the player's entrance scene has run
  double music_gap_left_{};
  // The message playing now, its script kept for the player.
  std::unique_ptr<StoryEvent> message_event_;
  std::unique_ptr<EventPlayer> message_;
  std::vector<std::string> sounds_; // heard whole: the menus', the chatter's
  std::vector<SoundCue> unplaced_;  // dialogue and non-spatial categorized sounds
  std::array<int, weapon_slots> heard_locks_{-1, -1, -1, -1};
  std::int64_t heard_prize_{};
  std::unique_ptr<BattleSounds> battle_sounds_;
  std::vector<PlacedCue> placed_; // heard where they are
  std::vector<DamageRumble> rumble_;
  std::vector<int> player_name_run_;
  std::vector<int> missing_run_;
  bool snap_camera_{true};
  bool jump_armed_{true}; // the button that closed a message must lift first
  bool done_{};
  bool match_results_{};
  std::shared_ptr<ReplayArchive> replay_;
  ReplayFrame replay_frame_;
  // This drawn frame is the first of its tick, the one the archive keeps.
  bool recording_frame_{};
  MatchOutcome outcome_{MatchOutcome::leave};
  double play_seconds_{};
  std::int64_t extra_prize_{};
  float victory_hold_{}; // seconds a win still stands before the match ends
  RankerContest ranker_{RankerContest::none};
  int ranker_area_{-1};
  // The camera on a boss's defeat (desktop_training_defeat.cpp).
  std::optional<ChaseCamera> defeat_chase_;
  std::optional<CameraViews> defeat_views_;
  double clock_{};       // seconds of battle: the HUD's mission time and pulses
  int day_{};
  bool standard_controls_{true}; // BUTTON CONFIG's CONTROL TYPE
  bool beginner_{};              // and its BEGINNER MODE
  ControlOptions controls_{};    // and its assignments and VIEW CHANGE TYPE
  BattleButtons buttons_;        // the script's, as BUTTON CONFIG assigns them
};

} // namespace phantomcrash
