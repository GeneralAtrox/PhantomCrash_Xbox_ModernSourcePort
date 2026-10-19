#pragma once

#include "phantomcrash/canvas.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/rumble.hpp"
#include "phantomcrash/rumbling_entry.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/world_sound.hpp"

#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

struct MenuPresence {
  std::string selection;
  std::array<float, 2> position{};
  std::string context;
  bool busy{};
  std::string parent_selection;
};

// A movie or scripted scene playing out and how far it has run, for the
// window title and the scene dump: "Entrance (shot 2)", frame 210 of 540.
struct SceneProgress {
  std::string label;
  int frame{};
  int total{};
  std::string unit{"frame"};
};

// One full-screen part of the game (intro, front end, placeholder), run by
// ScreenRunner. Logic runs on the fixed tick; advance() and draw() run once
// per presented frame.
class Screen {
public:
  virtual ~Screen() = default;

  virtual void tick(const InputFrame &input, double tick_seconds) = 0;
  virtual void advance(double /*elapsed_seconds*/) {}
  virtual void draw(const Viewport &viewport) = 0;
  virtual void set_paused(bool /*paused*/) {}
  virtual void set_presentation(const AudioOptions &, const PostProcessOptions &) {}
  [[nodiscard]] virtual std::optional<MenuPresence> menu_presence() const {
    return std::nullopt;
  }
  // Resolve a peer's selection in this player's layout, not the peer's layout.
  [[nodiscard]] virtual std::optional<MenuPresence> peer_presence(const MenuPresence &peer) const {
    const auto own = menu_presence();
    return own && own->context == peer.context ? std::optional{peer} : std::nullopt;
  }
  [[nodiscard]] virtual bool gameplay_paused() const {
    return false;
  }
  // Mouse-only shortcut; screens without a visible day label do not expose it.
  [[nodiscard]] virtual bool day_clicked(const InputFrame & /*input*/) const {
    return false;
  }
  // The HUD's SAVE button (a port addition), once a tick: lights it under
  // the mouse and says whether it was clicked. Only screens showing the
  // day and money offer it.
  virtual bool save_clicked(const InputFrame & /*input*/) {
    return false;
  }
  virtual void set_movie_gain(float /*gain*/) {}
  [[nodiscard]] virtual AudioBus sound_bus() const {
    return AudioBus::ui;
  }
  [[nodiscard]] virtual bool mouse_aim() const {
    return false;
  }
  [[nodiscard]] virtual bool mouse_menu_back() const {
    return !mouse_aim();
  }
  // Whether the keyboard types text into this screen now (InputFrame::typed).
  [[nodiscard]] virtual bool wants_text() const {
    return false;
  }

  // The music track this screen wants (empty: silence; "keep": whatever
  // plays on), and its volume.
  [[nodiscard]] virtual std::string music() const {
    return {};
  }
  [[nodiscard]] virtual float music_volume() const {
    return 1.0F;
  }
  // Play the saved battle program instead of music(); false for a scene cue.
  [[nodiscard]] virtual bool uses_music_program() const {
    return false;
  }
  // Seconds of silence before the program's next song starts.
  [[nodiscard]] virtual double music_program_gap() const {
    return 0.0;
  }
  // The music file playing now, or empty for silence: told as the screen
  // opens and whenever it changes, for the screens that show its credit.
  virtual void set_music(const std::string & /*file*/) {}
  // The CURRENT MUSIC meter's levels for the music heard now, each tick
  // before tick(), for the screens that show the meter.
  virtual void hear_music(const std::vector<float> & /*levels*/) {}
  // Sound effects started since the last call.
  [[nodiscard]] virtual std::vector<std::string> take_sounds() {
    return {};
  }
  // Placed sounds and keyed loops since the last call (the battle's).
  // The controllers whose robots took damage since the last take (rumble.hpp).
  [[nodiscard]] virtual std::vector<DamageRumble> take_rumble() {
    return {};
  }
  [[nodiscard]] virtual std::array<RumbleMotors, controller_ports> session_rumble() const {
    return {};
  }
  [[nodiscard]] virtual std::vector<SoundCue> take_sound_cues() {
    return {};
  }
  // Effects the screen may play, loaded as it opens.
  [[nodiscard]] virtual std::vector<std::string> sound_preloads() const {
    return {};
  }
  // Sound effects that stop as the screen closes, as the original's
  // destroy steps stop them.
  [[nodiscard]] virtual std::vector<std::string> held_sounds() const {
    return {};
  }
  [[nodiscard]] virtual bool done() const = 0;
  // The screen to show next, once done.
  [[nodiscard]] virtual std::string next() const = 0;
  // The game options as this screen has left them, for screens that change them.
  [[nodiscard]] virtual const GameOptions *game_options() const {
    return nullptr;
  }
  // The quest save as this screen has left it, for screens that change it.
  [[nodiscard]] virtual const QuestSave *quest_save() const {
    return nullptr;
  }
  [[nodiscard]] virtual const QuestSave *session_save() const {
    return quest_save();
  }
  // The game as this screen has it now, changes it hands back only as it
  // closes included (a shop's purchases), for SAVE opened over it; null
  // when it keeps none of its own.
  [[nodiscard]] virtual const QuestSave *pending_quest() const {
    return nullptr;
  }
  // The parts-stock entry NAME ENTRY names when it comes next (a dealer's
  // new CHIP, 0x3ad84), or -1.
  [[nodiscard]] virtual int naming_stock() const {
    return -1;
  }
  // The hangar whose Scoobee NAME ENTRY names when it comes next
  // (ASSEMBLE's NAME), or -1.
  [[nodiscard]] virtual int naming_hangar() const {
    return -1;
  }
  // The hangar whose Scoobee the hangar's KEEP chose for KEEP SV, or -1.
  [[nodiscard]] virtual int kept_hangar() const {
    return -1;
  }
  // The battle RUMBLING enters when it comes next, or none.
  [[nodiscard]] virtual const RumblingEntry *rumbling_entry() const {
    return nullptr;
  }
  // What the scene dump says of this screen (docs/SCENE_DUMP.md): an
  // object whose "summary" heads the task, whose "kinds" and "screens"
  // pick files in data/debug/scene_dump.json; null for nothing more.
  [[nodiscard]] virtual json::JsonValue scene_details() const {
    return {};
  }
  [[nodiscard]] virtual std::optional<SceneProgress> scene_progress() const {
    return std::nullopt;
  }
  // The live RUMBLING's opponents as they now stand (rumbling_world.hpp).
  virtual void set_live_rosters(const std::array<AreaRoster, roster_areas> & /*rosters*/) {}
  // Tools > Cheats' switches, every tick: a battle applies its own.
  virtual void set_cheats(const CheatOptions & /*cheats*/) {}
};

} // namespace phantomcrash
