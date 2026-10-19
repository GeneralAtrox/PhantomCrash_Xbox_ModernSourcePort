// The battle chatter: which robot says what (0x049470, 0x049552) and the
// line's script with the speaker filled in (0x0c57e0).
#include "phantomcrash/battle_chatter.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace phantomcrash {
namespace {

using json::JsonValue;
using json::required;

constexpr int gate_any = 0;
constexpr int gate_player = 1;
constexpr int gate_rival = 2;
constexpr int no_event = chatter_event::count;

// The text codes (0xbd720) and run-time sprites (0x0c6ec5) of a line.
constexpr std::string_view robot_name_code = "{code:fff8}";
constexpr std::string_view pilot_name_code = "{code:fff9}";
constexpr int robot_face_sprite = -1;
constexpr int pilot_face_sprite = -2;

constexpr std::array<const char *, 3> chance_keys{"player", "team_two", "computer"};

template <typename Value>
std::array<Value, chatter_event::count> per_event(const JsonValue &value, const char *what) {
  const auto &items = value.array();
  if (items.size() != chatter_event::count) {
    throw std::runtime_error(std::string(what) + " needs " + std::to_string(chatter_event::count) +
                             " entries");
  }
  std::array<Value, chatter_event::count> out{};
  for (std::size_t index = 0; index < out.size(); ++index) {
    out[index] = items[index].is_null() ? Value{-1} : static_cast<Value>(items[index].number());
  }
  return out;
}

// A name as markup: braces doubled so they print.
std::string escaped(const std::string &name) {
  std::string out;
  for (const char byte : name) {
    out += byte;
    if (byte == '{') {
      out += '{';
    }
  }
  return out;
}

void replace_all(std::string &text, const std::string_view code, const std::string &with) {
  for (auto at = text.find(code); at != std::string::npos; at = text.find(code, at + with.size())) {
    text.replace(at, code.size(), with);
  }
}

JsonValue *member(JsonValue &object, const std::string_view key) {
  for (auto &[name, value] : std::get<JsonValue::Object>(object.value)) {
    if (name == key) {
      return &value;
    }
  }
  return nullptr;
}

// The face a run-time sprite shows, placed where the command puts it.
Sprite placed_face(Sprite face, const JsonValue &command) {
  face.position = json::numbers<2>(required(command, "position"), "position");
  face.layer = static_cast<int>(command.number_or("layer", face.layer));
  if (const auto *colour = command.find("colour")) {
    face.colour = parse_colour(colour->string());
  }
  return face;
}

} // namespace

ChatterTables parse_chatter_tables(const JsonValue &document) {
  try {
    ChatterTables tables;
    tables.gates = per_event<int>(required(document, "gates"), "gates");
    const JsonValue &chances = required(document, "chances");
    for (std::size_t kind = 0; kind < chance_keys.size(); ++kind) {
      tables.chances[kind] = per_event<float>(required(chances, chance_keys[kind]), "chances");
    }
    tables.scripts = required(document, "scripts").array();
    for (const auto &table : required(document, "tables").array()) {
      auto &entries = tables.tables.emplace_back(per_event<int>(table, "tables"));
      for (const int script : entries) {
        if (script >= static_cast<int>(tables.scripts.size())) {
          throw std::runtime_error("a table names script " + std::to_string(script));
        }
      }
    }
    // Older conversions have no defeat lines; the rankers then say nothing.
    if (const JsonValue *defeats = document.find("ranker_defeats")) {
      for (const auto &entry : defeats->array()) {
        const int script = entry.is_null() ? -1 : static_cast<int>(entry.number());
        if (script >= static_cast<int>(tables.scripts.size())) {
          throw std::runtime_error("ranker_defeats names script " + std::to_string(script));
        }
        tables.ranker_defeats.push_back(script);
      }
    }
    return tables;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("chatter.json: ") + error.what());
  }
}

BattleChatter::BattleChatter(const ChatterTables &tables, const std::uint32_t seed)
    : tables_(tables), random_(seed) {}

void BattleChatter::set_speaker(const int robot, ChatterSpeaker speaker) {
  const auto index = static_cast<std::size_t>(robot);
  if (speakers_.size() <= index) {
    speakers_.resize(index + 1);
    pending_.resize(index + 1, no_event);
  }
  speakers_[index] = std::move(speaker);
}

const ChatterSpeaker *BattleChatter::speaker(const int robot) const {
  if (robot < 0 || robot >= static_cast<int>(speakers_.size())) {
    return nullptr;
  }
  return &speakers_[static_cast<std::size_t>(robot)];
}

void BattleChatter::raise(const int robot, const int event) {
  const ChatterSpeaker *who = speaker(robot);
  if (who == nullptr || event < 0 || event >= chatter_event::count) {
    return;
  }
  const auto slot = static_cast<std::size_t>(event);
  const int gate = tables_.gates[slot];
  if ((gate == gate_player && who->kind != ChatterKind::player) ||
      (gate == gate_rival && who->kind != ChatterKind::rival) ||
      (gate != gate_any && gate != gate_player && gate != gate_rival)) {
    return;
  }
  std::uniform_real_distribution<float> unit(0.0F, 1.0F);
  if (unit(random_) >= tables_.chances[static_cast<std::size_t>(who->kind)][slot]) {
    return;
  }
  int &pending = pending_[static_cast<std::size_t>(robot)];
  pending = std::min(pending, event);
}

std::optional<ChatterLine> BattleChatter::take(const bool showing) {
  int robot = -1;
  int event = no_event;
  for (std::size_t index = 0; index < pending_.size(); ++index) {
    if (pending_[index] < event) {
      event = pending_[index];
      robot = static_cast<int>(index);
    }
  }
  if (scene_) {
    robot = pending_.empty() ? -1 : 0;
    event = robot < 0 ? no_event : pending_.front();
  }
  std::ranges::fill(pending_, no_event);
  const auto listed = [event](const auto &events) {
    return std::ranges::find(events, event) != events.end();
  };
  const bool closes =
      scene_ ? listed(chatter_event::scene_interrupting) : listed(chatter_event::interrupting);
  if (robot < 0 || event == no_event || (scene_ && !listed(chatter_event::scene)) ||
      (showing && !closes)) {
    return std::nullopt;
  }
  const int table = speakers_[static_cast<std::size_t>(robot)].table;
  if (table < 0 || table >= static_cast<int>(tables_.tables.size())) {
    return std::nullopt;
  }
  const int script =
      tables_.tables[static_cast<std::size_t>(table)][static_cast<std::size_t>(event)];
  if (script < 0) {
    return std::nullopt;
  }
  return ChatterLine{robot, event, script};
}

std::vector<EventCommand> chatter_script(const ChatterTables &tables, const ChatterLine &line,
                                         const ChatterSpeaker &speaker, const GlyphFont &font) {
  const auto &source = tables.scripts.at(static_cast<std::size_t>(line.script)).array();
  JsonValue commands{JsonValue::Array{}};
  auto &kept = std::get<JsonValue::Array>(commands.value);
  std::vector<std::pair<std::size_t, Sprite>> faces;
  for (const JsonValue &command : source) {
    const std::string op = command.string_or("op", "");
    if (op == "sprite") {
      const int id = static_cast<int>(command.number_or("sprite", 0));
      const std::optional<Sprite> &face = id == robot_face_sprite   ? speaker.robot_face
                                          : id == pilot_face_sprite ? speaker.pilot_face
                                                                    : std::nullopt;
      if (id == robot_face_sprite || id == pilot_face_sprite) {
        if (!face) {
          continue;
        }
        faces.emplace_back(kept.size(), placed_face(*face, command));
      }
    }
    JsonValue &copy = kept.emplace_back(command);
    if (op == "text") {
      if (JsonValue *text = member(copy, "text"); text != nullptr && text->is_string()) {
        std::string &markup = std::get<std::string>(text->value);
        replace_all(markup, robot_name_code, escaped(speaker.robot_name));
        replace_all(markup, pilot_name_code, escaped(speaker.pilot_name));
      }
    }
  }
  std::vector<EventCommand> out = parse_event_commands(commands, font);
  for (auto &[index, face] : faces) {
    out[index].sprite = std::move(face);
  }
  return out;
}

} // namespace phantomcrash
