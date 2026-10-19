// Talk scenes: the event text markup, typing, skipping, waits, character
// fades, blinks and mouths, fast-forward, the scene's fades, and which event
// plays between two scenes.

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/event_player.hpp"

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;
constexpr int letter = 200; // past the punctuation: moves the mouth
constexpr int stop = 100;   // punctuation: ticks without moving it

GlyphFont test_font() {
  GlyphFont font;
  font.glyphs = {{U'　', 0}, {U'Ａ', letter}, {U'．', stop}};
  font.fallback = 1;
  return font;
}

EventTuning test_tuning() {
  EventTuning tuning;
  tuning.confirm_buttons = button::confirm;
  tuning.fast_forward_buttons = button::left_trigger;
  tuning.type_sound = "type";
  tuning.page_sound = "page";
  tuning.skip_sound = "skip";
  return tuning;
}

EventCommand text_command(const std::string &markup, const int frames_per_character) {
  EventCommand command;
  command.op = EventOp::text;
  command.text.tokens = parse_event_text(test_font(), markup);
  command.text.units = text_units(command.text.tokens);
  command.text.frames_per_character = frames_per_character;
  return command;
}

EventCommand wait_command(const int frames) {
  EventCommand command;
  command.op = EventOp::wait;
  command.frames = frames;
  return command;
}

EventCommand confirm_command() {
  EventCommand command;
  command.op = EventOp::wait_for_confirm;
  return command;
}

EventCommand character_command(const CharacterFade fade = CharacterFade::none) {
  EventCommand command;
  command.op = EventOp::character;
  command.character.character = 0;
  command.character.position = {100.0F, 50.0F};
  command.character.layer = 10;
  command.character.fade = fade;
  return command;
}

TalkTables test_talk() {
  TalkPose pose;
  pose.body = {0, {0, 0, 100, 200}, {}};
  pose.mouth = {std::nullopt, TalkPart{0, {100, 0, 110, 10}, {5, 6}},
                TalkPart{0, {110, 0, 120, 10}, {5, 6}}};
  pose.blink = TalkPart{0, {120, 0, 130, 10}, {7, 8}};
  TalkTables talk;
  talk.characters.push_back({{"page0.png"}, {pose}});
  talk.backgrounds.push_back({});
  return talk;
}

StoryEvent test_event(std::vector<EventCommand> commands) {
  StoryEvent event;
  event.scene = 48;
  event.background = 0;
  event.cast = {0};
  event.commands = std::move(commands);
  return event;
}

void run(EventPlayer &player, const int frames, const Buttons held = 0) {
  for (int count = 0; count < frames; ++count) {
    player.update(tick, InputFrame{held, 0, 1});
  }
}

void press(EventPlayer &player, const Buttons buttons) {
  player.update(tick, InputFrame{buttons, buttons, 1});
}

int visible(const EventPlayer &player) {
  const auto &texts = player.frame().texts;
  return texts.empty() ? -2 : texts.back().visible;
}

int count_of(const std::vector<std::string> &sounds, const std::string &name) {
  return static_cast<int>(std::ranges::count(sounds, name));
}

bool has_layer(const EventPlayer &player, const int layer, const float x) {
  return std::ranges::any_of(player.frame().sprites, [&](const TalkSprite &sprite) {
    return sprite.sprite.layer == layer && sprite.sprite.position[0] == x;
  });
}

} // namespace

TEST_CASE("event text markup becomes glyphs, line breaks, colours and the name") {
  const auto tokens = parse_event_text(test_font(), "A{#eeee66}.{/}\n{name}{{");
  CHECK(tokens.size() == 7);
  CHECK(tokens[0].glyph == letter);
  CHECK(tokens[1].kind == TextToken::Kind::colour && tokens[1].colour == 0xeeee66);
  CHECK(tokens[2].glyph == stop);
  CHECK(tokens[3].kind == TextToken::Kind::reset_colour);
  CHECK(tokens[4].kind == TextToken::Kind::newline);
  CHECK(tokens[5].kind == TextToken::Kind::player_name);
  CHECK(tokens[6].kind == TextToken::Kind::glyph);
  CHECK(text_units(tokens) == 5);
  CHECK_THROWS(parse_event_text(test_font(), "{nope}"));
}

TEST_CASE("a text ending in a colour change takes one more step to finish") {
  const auto tokens = parse_event_text(test_font(), "AA{/}");
  CHECK(!text_progress(tokens, 2).complete);
  CHECK(text_progress(tokens, 2).tokens == 2);
  CHECK(text_progress(tokens, 3).complete);
  CHECK(text_progress(tokens, -1).complete);
}

TEST_CASE("text types a character every few frames with a tick, then waits for confirm") {
  const auto talk = test_talk();
  const auto event = test_event({text_command("A A", 3), confirm_command()});
  EventPlayer player(event, talk, test_tuning());
  run(player, 2);
  CHECK(visible(player) == 0);
  run(player, 1);
  CHECK(visible(player) == 1);
  run(player, 3);
  CHECK(visible(player) == 2);
  CHECK(count_of(player.take_sounds(), "type") == 1); // the space is silent
  run(player, 3);
  CHECK(visible(player) == 3);
  run(player, 1);
  CHECK(visible(player) == -1); // done: redrawn whole from now on
  run(player, 30);
  CHECK(!player.finished());
  press(player, button::a);
  run(player, 1);
  CHECK(player.finished());
  CHECK(count_of(player.take_sounds(), "page") == 1);
}

TEST_CASE("confirm while typing shows the whole text on the next frame") {
  const auto talk = test_talk();
  const auto event = test_event({text_command("AAAAAA", 3), confirm_command()});
  EventPlayer player(event, talk, test_tuning());
  run(player, 4);
  press(player, button::a);
  CHECK(visible(player) == 1);
  run(player, 1);
  CHECK(visible(player) == -1);
  CHECK(count_of(player.take_sounds(), "skip") == 1);
  run(player, 5);
  CHECK(!player.finished()); // the same press does not also turn the page
  CHECK(player.take_sounds().empty());
}

TEST_CASE("a wait holds the page, then the next page replaces it") {
  const auto talk = test_talk();
  auto first = text_command("A", 0);
  auto second = text_command("AA", 0);
  const auto event = test_event({first, wait_command(5), second, wait_command(1)});
  EventPlayer player(event, talk, test_tuning());
  run(player, 4);
  CHECK(player.frame().texts.size() == 1);
  CHECK(player.frame().texts[0].text->units == 1);
  run(player, 2); // the wait ends on its fifth frame; the page changes the frame after
  CHECK(player.frame().texts[0].text->units == 2);
  run(player, 1);
  CHECK(player.finished());
}

TEST_CASE("a hold keeps the page drawn and fast-forward cuts it short") {
  const auto talk = test_talk();
  EventCommand hold;
  hold.op = EventOp::hold;
  hold.frames = 10;
  // Pepper's lesson texts are not skippable, so fast-forward leaves them shown.
  auto first = text_command("A", 0);
  auto second = text_command("AA", 0);
  first.text.skippable = second.text.skippable = false;
  const auto event = test_event({first, hold, second, confirm_command()});
  EventPlayer player(event, talk, test_tuning());
  run(player, 5);
  CHECK(player.frame().texts.size() == 1);
  run(player, 6); // past the hold: both texts, the first still drawn
  CHECK(player.frame().texts.size() == 2);
  EventPlayer pressed(event, talk, test_tuning());
  run(pressed, 1);
  press(pressed, button::a); // confirm does not end it
  CHECK(pressed.frame().texts.size() == 1);
  run(pressed, 1, button::left_trigger);
  CHECK(pressed.frame().texts.size() == 2);
}

TEST_CASE("holding fast-forward hides the text and runs through the pages") {
  const auto talk = test_talk();
  const auto event = test_event({text_command("AAAA", 3), confirm_command(), wait_command(60),
                                 text_command("AAAA", 3), confirm_command(), wait_command(60)});
  EventPlayer player(event, talk, test_tuning());
  run(player, 1, button::left_trigger);
  CHECK(player.frame().texts.empty());
  CHECK(!player.finished()); // the wait for confirm still takes two frames
  run(player, 4, button::left_trigger);
  CHECK(player.finished());
}

TEST_CASE("characters fade in over thirty frames, then blink and move their mouths") {
  const auto talk = test_talk();
  const auto event = test_event(
      {character_command(CharacterFade::in), text_command("AAAAAAAAAAAA", 1), confirm_command()});
  EventPlayer player(event, talk, test_tuning());
  run(player, 1);
  CHECK(player.frame().sprites.size() == 1);
  CHECK(player.frame().sprites[0].alpha < 0.1F);
  run(player, 28);
  CHECK(player.frame().sprites[0].alpha < 1.0F);
  run(player, 1);
  CHECK(player.frame().sprites[0].alpha == 1.0F);
  // Fully shown: the first blink starts at once.
  run(player, 1);
  CHECK(has_layer(player, 11, 107.0F));
}

TEST_CASE("a speaking character's mouth steps half, open, half, closed") {
  const auto talk = test_talk();
  const auto event =
      test_event({character_command(), text_command(std::string(60, 'A'), 1), confirm_command()});
  EventPlayer player(event, talk, test_tuning());
  std::vector<float> mouths;
  for (int frame = 0; frame < 20; ++frame) {
    run(player, 1);
    const auto &sprites = player.frame().sprites;
    const auto mouth = std::ranges::find_if(
        sprites, [](const TalkSprite &sprite) { return sprite.sprite.position[0] == 105.0F; });
    mouths.push_back(mouth == sprites.end() ? 0.0F : mouth->sprite.rect[0]);
  }
  // The mouth reads the previous frame's typing, then holds each shape.
  CHECK(mouths[0] == 0.0F);
  CHECK(mouths[1] == 100.0F);
  CHECK(mouths[6] == 100.0F);
  CHECK(mouths[7] == 110.0F);
  CHECK(mouths[13] == 100.0F);
  CHECK(mouths[19] == 0.0F);
}

TEST_CASE("the talk scene fades in, plays, and fades out to the next scene") {
  const auto talk = test_talk();
  const auto event = test_event({text_command("A", 0), wait_command(20)});
  TalkScene scene(event, talk, test_tuning(), "name_entry");
  CHECK(scene.background() != nullptr);
  for (int frame = 0; frame < 20; ++frame) {
    scene.update(tick, InputFrame{0, 0, 1});
  }
  CHECK(scene.frame() == nullptr);
  CHECK(scene.black_alpha() > 0.0F);
  for (int frame = 0; frame < 20; ++frame) {
    scene.update(tick, InputFrame{0, 0, 1});
  }
  CHECK(scene.frame() != nullptr);
  for (int frame = 0; frame < 60; ++frame) {
    scene.update(tick, InputFrame{0, 0, 1});
  }
  CHECK(scene.done());
  CHECK(scene.next() == "name_entry");
}

TEST_CASE("an event plays once, on its move, after the events it needs") {
  std::vector<StoryEvent> events(3);
  events[0].scene = 48;
  events[0].from_scene = 25;
  events[0].to_scene = 22;
  events[1].scene = 49;
  events[1].from_scene = 22;
  events[1].to_scene = 19;
  events[2].scene = 50;
  events[2].from_scene = 19;
  events[2].to_scene = 41;
  events[2].requires_seen = {49};
  CHECK(event_between(events, 25, 22, {}) == &events[0]);
  CHECK(event_between(events, 25, 22, {48}) == nullptr);
  CHECK(event_between(events, 22, 25, {}) == nullptr);
  CHECK(event_between(events, 19, 41, {}) == nullptr);
  CHECK(event_between(events, 19, 41, {49}) == &events[2]);
  events[0].enabled = false;
  CHECK(event_between(events, 25, 22, {}) == nullptr);
}

TEST_CASE("an event with a check the port lacks does not play") {
  const auto parse = [](const char *bytes) {
    const std::string text =
        std::string(R"({"events": [{"scene": 152, "from_scene": 8, "to_scene": 41, "cast": [],
                     "commands": [], "condition_bytes": )") +
        bytes + "}]}";
    return parse_story_events(json::parse(text), GlyphFont{});
  };
  // Event 48's bytes set no check; event 152's name test function 22.
  CHECK(!parse("[1, 75, 56, 4, 28, 2]").front().unported_conditions);
  const auto late = parse("[1, 22, 56, 4, 28, 2]");
  CHECK(late.front().unported_conditions);
  CHECK(event_between(late, 8, 41, {}) == nullptr);
  CHECK(parse("[1, 255, 3, 4, 28, 2]").front().unported_conditions);
  CHECK_THROWS(parse("[1, 2]"));
}

TEST_CASE("the shipped talk tuning and tables parse") {
  const auto roots = DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
  const auto tuning = parse_event_tuning(roots.document("story/talk.json"));
  CHECK(tuning.character_fade_frames == 30);
  CHECK(tuning.confirm_buttons == button::confirm);
  CHECK((tuning.fast_forward_buttons & button::right_trigger) != 0);
  CHECK(tuning.type_sound == "talk_type");
}

TEST_CASE("talk backgrounds take their widescreen edges from the talk tuning") {
  TalkTables talk;
  talk.backgrounds.resize(2);
  apply_talk_edges(json::parse(R"({"background_edges": {
    "back": {"extend_edges": "both", "edge_inset": [1, 0], "edge_bands_right": [[0, 33]]},
    "front": {"extend_edges": "left"}}})"),
                   talk);
  for (const TalkBackground &background : talk.backgrounds) {
    CHECK(background.back.extend_left);
    CHECK(background.back.extend_right);
    CHECK(background.back.edge_inset[0] == 1.0F);
    CHECK(background.back.edge_bands[0].empty());
    CHECK(background.back.edge_bands[1].size() == 1);
    CHECK(background.front.extend_left);
    CHECK(!background.front.extend_right);
  }
  TalkTables plain;
  plain.backgrounds.resize(1);
  apply_talk_edges(json::parse(R"({})"), plain);
  CHECK(!plain.backgrounds[0].back.extend_left);
}
