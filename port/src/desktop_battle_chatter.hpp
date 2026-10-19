#pragma once

#include "desktop_textures.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/battle_chatter.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/event_player.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/training_battle.hpp"

#include <memory>
#include <string>
#include <vector>

namespace phantomcrash {

// The battle chatter's window: it takes the battle's chatter events, starts
// the line BattleChatter picks and plays it with the event player, in the
// place Pepper's lesson messages use.
class ChatterWindow final {
public:
  ChatterWindow(TextureCache &textures, const GlyphFont &font, const TalkTables &talk,
                EventTuning tuning, ChatterTables tables, std::string sound);

  // Loads the speaker's faces now, not as the first line opens.
  void set_speaker(int robot, ChatterSpeaker speaker);
  // Robot 0 is entering or leaving the battle (BattleChatter::set_scene).
  void set_scene(bool scene) noexcept {
    chatter_.set_scene(scene);
  }
  // `quiet`: a lesson message has the window, so any line showing closes
  // and none starts.
  void tick(const Battle &battle, double tick_seconds, bool quiet);
  // Area `area`'s ranker, robot `robot`, speaks its defeat line at once,
  // closing any line showing (0x091f29). False without the line.
  bool say_defeat(int robot, int area);
  // A line is showing (0x089bb9 waits for the defeat line's end).
  [[nodiscard]] bool speaking() const noexcept {
    return player_ != nullptr;
  }
  void draw(const Viewport &viewport) const;
  [[nodiscard]] std::vector<std::string> take_sounds() {
    return std::exchange(sounds_, {});
  }

private:
  void close();
  void start(const ChatterLine &line, const ChatterSpeaker &speaker);

  TextureCache &textures_;
  const GlyphFont &font_;
  const TalkTables &talk_;
  EventTuning tuning_;
  ChatterTables tables_;
  BattleChatter chatter_;
  std::string sound_;
  std::uint32_t lines_{}; // lines started, seeding each player
  std::unique_ptr<StoryEvent> event_;
  std::unique_ptr<EventPlayer> player_;
  std::vector<std::string> sounds_;
};

// The training battle's chatter window, or null when its lines are missing.
[[nodiscard]] std::unique_ptr<ChatterWindow>
load_chatter_window(TextureCache &textures, const GlyphFont &font, const DataRoots &roots,
                    const TalkTables &talk, EventTuning tuning, const TrainingBattle &setup);

// A player's robot speaks from its chip's line table with its chip's name
// (the one the player gave it, else its record's) and face; the pilot's name
// is the player's and there is no pilot face (0x049552, 0x06d663).
[[nodiscard]] ChatterSpeaker chip_speaker(const ChipRecord &chip, const std::string &chip_name,
                                          std::string player_name);

} // namespace phantomcrash
