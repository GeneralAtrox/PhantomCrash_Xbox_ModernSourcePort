// Parsing of data/ui/option.json and the music credits.

#include "phantomcrash/option_menu.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

// An item's sprites take the item's position.
Sprite item_sprite(const json::JsonValue &item, const char *key) {
  Sprite sprite = parse_sprite(required(item, key));
  if (const auto *position = item.find("position")) {
    sprite.position = numbers<2>(*position, "position");
  }
  return sprite;
}

OptionItem parse_item(const json::JsonValue &value) {
  OptionItem item;
  item.id = value.string_or("id", "");
  try {
    item.normal = item_sprite(value, "normal");
    item.selected = item_sprite(value, "selected");
    item.help = parse_help(value);
    int actions = 0;
    // A quit item names its destination with "goto" too, but is one action.
    const auto *quit = value.find("quit");
    if (const auto *go_to = value.find("goto"); go_to != nullptr && quit == nullptr) {
      item.action = OptionAction::go_to;
      item.go_to = go_to->string();
      ++actions;
    }
    if (quit != nullptr) {
      item.action = OptionAction::quit;
      item.go_to = required(value, "goto").string();
      item.question = parse_lines(required(*quit, "question"));
      ++actions;
    }
    if (const auto *plate = value.find("plate")) {
      item.plate = OptionPlate{required(*plate, "text").string(), plate->string_or("font", "futura")};
    }
    if (const auto *toggle = value.find("toggle")) {
      item.action = OptionAction::toggle;
      item.option = required(*toggle, "option").string();
      GameOptions probe;
      if (game_option(probe, item.option) == nullptr) {
        throw std::runtime_error("no on/off option \"" + item.option + "\"");
      }
      item.question = parse_lines(required(*toggle, "question"));
      ++actions;
    }
    if (const auto *reset = value.find("reset")) {
      item.action = OptionAction::reset;
      item.question = parse_lines(required(*reset, "question"));
      item.done = parse_lines(required(*reset, "done"));
      item.default_choice = static_cast<int>(reset->number_or("default_choice", 1));
      if (item.default_choice != 0 && item.default_choice != 1) {
        throw std::runtime_error("default_choice must be 0 (YES) or 1 (NO)");
      }
      ++actions;
    }
    if (value.boolean_or("exit", false)) {
      item.action = OptionAction::exit;
      ++actions;
    }
    if (actions != 1) {
      throw std::runtime_error("needs one of \"goto\", \"toggle\", \"reset\" or \"exit\"");
    }
  } catch (const std::exception &error) {
    throw std::runtime_error("item \"" + item.id + "\": " + error.what());
  }
  return item;
}

int item_index(const std::vector<OptionItem> &items, const std::string &id, const char *what) {
  const auto found = std::ranges::find(items, id, &OptionItem::id);
  if (found == items.end()) {
    throw std::runtime_error(std::string(what) + " names no item \"" + id + "\"");
  }
  return static_cast<int>(found - items.begin());
}

std::string lowered(std::string text) {
  std::ranges::transform(text, text.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

void replace_all(std::string &text, const std::string &key, const std::string &value) {
  for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size())) {
    text.replace(at, key.size(), value);
  }
}

} // namespace

OptionScript parse_option(const json::JsonValue &document, const std::string &name) {
  try {
    OptionScript script;
    parse_menu_basics(document, script);
    if (document.find("cancel") == nullptr) {
      throw std::runtime_error("missing \"cancel\"");
    }
    static_cast<MenuTickers &>(script) = parse_menu_tickers(document);
    for (const auto &item : required(document, "items").array()) {
      script.items.push_back(parse_item(item));
    }
    if (script.items.empty()) {
      throw std::runtime_error("needs at least one item");
    }
    script.default_cursor =
        item_index(script.items, required(document, "default_cursor").string(), "default_cursor");
    script.cancel_cursor =
        item_index(script.items, required(document, "cancel_cursor").string(), "cancel_cursor");
    if (const auto *plate = document.find("riding_plate_y")) {
      script.riding_plate_y = static_cast<float>(plate->number());
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(name + ": " + error.what());
  }
}

namespace {

std::optional<Sprite> credit_jacket(const json::JsonValue &entry, const char *key) {
  const auto *value = entry.find(key);
  if (value == nullptr) {
    return std::nullopt;
  }
  Sprite jacket;
  jacket.id = key;
  jacket.image = required(*value, "image").string();
  jacket.rect = json::numbers<4>(required(*value, "rect"), "rect");
  return jacket;
}

} // namespace

std::vector<MusicCredit> parse_music_credits(const json::JsonValue &document) {
  std::vector<MusicCredit> credits;
  if (const auto *list = document.find("credits")) {
    for (const auto &entry : list->array()) {
      MusicCredit credit{required(entry, "file").string(), entry.string_or("title", ""),
                         entry.string_or("artist", "")};
      credit.jacket = credit_jacket(entry, "jacket");
      credit.jacket_small = credit_jacket(entry, "jacket_small");
      credit.artist_name = credit_jacket(entry, "artist_name");
      credit.artist_link = credit_jacket(entry, "artist_link");
      if (const auto *profile = entry.find("artist_profile")) {
        credit.artist_profile =
            ArtistProfile{required(*profile, "text").string(),
                          numbers<2>(required(*profile, "position"), "artist profile position"),
                          parse_colour(required(*profile, "colour").string()),
                          static_cast<int>(required(*profile, "layer").number())};
      }
      credits.push_back(std::move(credit));
    }
  }
  return credits;
}

std::string credit_line(const std::vector<MusicCredit> &credits, const std::string &file,
                        const std::string &format) {
  const std::string wanted = lowered(file);
  for (const auto &credit : credits) {
    if (lowered(credit.file) == wanted) {
      std::string line = format;
      replace_all(line, "{title}", credit.title);
      replace_all(line, "{artist}", credit.artist);
      return line;
    }
  }
  return {};
}

} // namespace phantomcrash
