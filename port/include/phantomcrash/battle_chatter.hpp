#pragma once

// The battle chatter: robots comment on what happens to them in a window
// mid-screen, a face, a name and a line (tables/chatter.json, converted
// from the XBE; docs/PART_RECORDS.md). Game code raises an event for a robot
// (0x049470); it passes its gate and chance, and each robot keeps only its
// most urgent (lowest) event. Every tick 0x049552 shows the lowest event
// pending over all robots from the speaker's line table, unless a line is
// still showing, and forgets the rest.

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/story_event.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace phantomcrash {

// The chatter events the port raises: the original's numbers, with
// researcher-assigned names.
namespace chatter_event {
constexpr int rival_entrance = 0;     // a computer robot of the other side entering (0x088ffa)
constexpr int first_kill = 1;         // the battle's first enemy shot down (0x07675e)
constexpr int shot_down_by_rival = 2; // the robot, by the other side
constexpr int quarter_by_rival = 3;   // armour fell below a quarter, the other side hitting
constexpr int boss_entrance = 4;      // a boss entering (loadout +0x70 1)
constexpr int shot_down = 5;
constexpr int arms_lost = 6;       // a knockdown took its last arm (0x08d686)
constexpr int left_hurt = 9;       // left the battle below a quarter of its armour (0x08224d)
constexpr int hit_from_behind = 7; // 0x0988e1
constexpr int kill_low_armour = 8; // a kill with less than a quarter of its armour left
constexpr int dodge = 10;
constexpr int repaired_low = 11; // a repair pickup left it below a quarter of its armour (0x066e50)
constexpr int knockdown_low_armour = 12; // knocked down below a quarter of its armour
constexpr int ammo_half_low = 13;        // rounds down to half, armour below a quarter (0x075773)
constexpr int quarter = 14;              // armour fell below a quarter
constexpr int arm_lost = 15;             // a knockdown took an arm, one is left
constexpr int camouflage_low = 16;       // its camouflage gauge fell below a quarter (0x0751be)
constexpr int critical_hit = 17;         // the robot took one
constexpr int entrance = 18;             // a player's or an ally computer robot entering (0x088ffa)
constexpr int ammo_low = 19;             // an ammo pickup taken below half its rounds (0x066e50)
constexpr int ammo_half = 20;            // its rounds fell to half (0x075773)
constexpr int kill = 21;
constexpr int shot_carrier = 22;  // shot the supply carrier down (0x047ed9)
constexpr int locked_on = 23;     // a missile lock was made on it (0x051045)
constexpr int half = 24;          // armour fell below half
constexpr int melee_strike = 25;  // its melee weapon struck a robot (0x047ed9)
constexpr int beam_strike = 26;   // its beam struck a robot
constexpr int blast = 27;         // it lived through a blast that hurt it (0x09c9c2)
constexpr int left = 28;          // left the battle with a quarter of its armour or more
constexpr int repaired = 29;      // a repair pickup (0x066e50)
constexpr int ammo = 30;          // an ammo pickup taken short of full
constexpr int object_broken = 31; // its shot broke a stage object (0x047ed9)
constexpr int weapon_fired = 32;  // it fired a missile or grenade weapon (0x09b5db)
constexpr int idle = 33;          // standing idle with the pad untouched (0x08e480)
constexpr int camouflage = 34;    // Optic Camouflage switched on (0x074fa4)
constexpr int count = 35;
// The events that close a line still showing (0x049552).
constexpr std::array<int, 2> interrupting{1, 4};
// While a player's robot 0 enters or leaves (robot states 1-6), only its
// own entrance and leaving lines play, and the leaving lines close a line
// still showing (0x049552).
constexpr std::array<int, 3> scene{entrance, left_hurt, left};
constexpr std::array<int, 2> scene_interrupting{left_hurt, left};
} // namespace chatter_event

// Armour shares the events above compare against (0x199fd4, 0x17accc).
constexpr float chatter_low_armour = 0.25F;
constexpr float chatter_half_armour = 0.5F;
// The camouflage gauge share its low line is spoken at (0x199fd4).
constexpr float camouflage_low_share = 0.25F;

// Whose chances an event takes, and who a gate lets through: a robot a
// player controls, a computer robot on the other side (loadout +0x6c 2),
// or another computer robot.
enum class ChatterKind { player, rival, computer };

struct ChatterTables {
  // Per event: 0 any robot, 1 only a player's, 2 only a rival's (0x203848).
  std::array<int, chatter_event::count> gates{};
  // Per kind, the chance each event passes (0x203890, 0x203920, 0x2039b0).
  std::array<std::array<float, chatter_event::count>, 3> chances{};
  std::vector<json::JsonValue> scripts; // command arrays, as events.json gives them
  // Line tables (0x1b3520): per event, an index into scripts, or -1.
  std::vector<std::array<int, chatter_event::count>> tables;
  // Each Area Ranker's defeat line, by area (0x2431b4), an index into
  // scripts or -1.
  std::vector<int> ranker_defeats;
};

// Throws std::runtime_error when a value is missing or wrong.
[[nodiscard]] ChatterTables parse_chatter_tables(const json::JsonValue &document);

// Who speaks for a robot: its line table (a player's robot its chip's,
// chip +0x18), the names the text codes print (0xfff8 the robot's, 0xfff9
// the pilot's) and the faces sprites -1 and -2 show. A robot without a
// table says nothing.
struct ChatterSpeaker {
  int table{-1};
  ChatterKind kind{ChatterKind::computer};
  std::string robot_name;
  std::string pilot_name;
  std::optional<Sprite> robot_face;
  std::optional<Sprite> pilot_face;
};

struct ChatterLine {
  int robot{};
  int event{};
  int script{};
};

class BattleChatter final {
public:
  explicit BattleChatter(const ChatterTables &tables, std::uint32_t seed = 1);

  void set_speaker(int robot, ChatterSpeaker speaker);
  [[nodiscard]] const ChatterSpeaker *speaker(int robot) const;
  // An event happened to `robot`: it stays pending if it passes its gate
  // and chance and is more urgent than the robot's pending one (0x049470).
  void raise(int robot, int event);
  // The line to start this tick: the lowest pending event over all robots,
  // when no line is `showing` or the event closes it. Every pending event
  // is forgotten either way (0x049552).
  [[nodiscard]] std::optional<ChatterLine> take(bool showing);
  // Robot 0 is entering or leaving the battle: see chatter_event::scene.
  void set_scene(bool scene) noexcept {
    scene_ = scene;
  }

private:
  const ChatterTables &tables_;
  std::minstd_rand random_;
  std::vector<ChatterSpeaker> speakers_;
  std::vector<int> pending_; // per robot, chatter_event::count for none
  bool scene_{};
};

// The line's script with the speaker's names and faces filled in; a face
// the speaker lacks is left out.
[[nodiscard]] std::vector<EventCommand> chatter_script(const ChatterTables &tables,
                                                       const ChatterLine &line,
                                                       const ChatterSpeaker &speaker,
                                                       const GlyphFont &font);

} // namespace phantomcrash
