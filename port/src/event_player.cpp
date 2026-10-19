// The event script player: a frame-by-frame copy of the original's
// interpreter (0xc5a1a) and its text (0xc5b9d), wait (0xc604b), sprite
// (0xc620c), character (0xc62ff) and wait-for-confirm (0xc6bc4) commands.

#include "phantomcrash/event_player.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

using json::required;

int integer_or(const json::JsonValue &document, const char *key, const int fallback) {
  return static_cast<int>(document.number_or(key, fallback));
}

bool is_colour(const TextToken &token) {
  return token.kind == TextToken::Kind::colour || token.kind == TextToken::Kind::reset_colour;
}

} // namespace

EventTuning parse_event_tuning(const json::JsonValue &document) {
  try {
    EventTuning tuning;
    tuning.fade_in = document.number_or("fade_in", tuning.fade_in);
    tuning.fade_out = document.number_or("fade_out", tuning.fade_out);
    tuning.character_fade_frames =
        std::max(1, integer_or(document, "character_fade_frames", tuning.character_fade_frames));
    tuning.blink_wait_frames = integer_or(document, "blink_wait_frames", tuning.blink_wait_frames);
    tuning.blink_frames = integer_or(document, "blink_frames", tuning.blink_frames);
    tuning.blink_random = integer_or(document, "blink_random", tuning.blink_random);
    tuning.mouth_hold_frames = integer_or(document, "mouth_hold_frames", tuning.mouth_hold_frames);
    tuning.speaking_glyph_above =
        integer_or(document, "speaking_glyph_above", tuning.speaking_glyph_above);
    tuning.confirm_delay_frames =
        integer_or(document, "confirm_delay_frames", tuning.confirm_delay_frames);
    const auto &buttons = required(document, "buttons");
    tuning.confirm_buttons = buttons_from_json(required(buttons, "confirm"));
    tuning.fast_forward_buttons = buttons_from_json(required(buttons, "fast_forward"));
    const auto &sounds = required(document, "sounds");
    tuning.type_sound = sounds.string_or("type", "");
    tuning.page_sound = sounds.string_or("page", "");
    tuning.skip_sound = sounds.string_or("skip", "");
    try {
      tuning.frame = parse_frame_placement(required(document, "frame"));
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string("frame: ") + error.what());
    }
    return tuning;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("talk.json: ") + error.what());
  }
}

// The drawer (0xbd720) stops at the first token once the count is used up,
// colour changes included, so a text ending in a colour change needs one
// more step than it has characters before it counts as drawn.
TextProgress text_progress(const std::vector<TextToken> &tokens, const int visible) {
  int used = 0;
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    if (visible >= 0 && used >= visible) {
      return {index, false};
    }
    if (!is_colour(tokens[index])) {
      ++used;
    }
  }
  return {tokens.size(), true};
}

EventPlayer::EventPlayer(const StoryEvent &event, const TalkTables &talk, EventTuning tuning,
                         const std::uint32_t seed)
    : event_(event), talk_(talk), tuning_(std::move(tuning)), random_(seed == 0 ? 1 : seed),
      cast_(event.cast.size()) {}

std::vector<std::string> EventPlayer::take_sounds() {
  return std::exchange(sounds_, {});
}

void EventPlayer::update(const double tick_seconds, const InputFrame &input) {
  seconds_ += tick_seconds;
  pending_ += tick_seconds * tuning_.frame_rate;
  Buttons pressed = input.pressed;
  while (pending_ >= 1.0 - 1e-9 && !finished_) {
    pending_ -= 1.0;
    step(pressed, input.held);
    pressed = 0; // a press counts on one frame only
  }
}

void EventPlayer::step(const Buttons pressed, const Buttons held) {
  frame_.sprites.clear();
  frame_.texts.clear();
  const bool confirm = (pressed & tuning_.confirm_buttons) != 0;
  const bool fast = (held & tuning_.fast_forward_buttons) != 0;
  const auto &commands = event_.commands;
  for (std::size_t index = checkpoint_; index < commands.size(); ++index) {
    const EventCommand &command = commands[index];
    if (index > reached_) {
      reached_ = index;
      if (command.op != EventOp::text) {
        skip_ = false;
      }
    }
    bool done = true;
    switch (command.op) {
    case EventOp::text:
      done = run_text(index, command.text, confirm, fast);
      break;
    case EventOp::wait:
      if (run_wait(command.frames, fast)) {
        checkpoint_ = index + 1;
        return; // the next page starts on the next frame
      }
      done = false;
      break;
    case EventOp::sprite:
      if (!command.sprite.image.empty()) {
        frame_.sprites.push_back({command.sprite, command.alpha});
      }
      break;
    case EventOp::character:
      run_character(command.character);
      break;
    case EventOp::wait_for_confirm:
      done = run_confirm(index, confirm, fast);
      break;
    case EventOp::hold:
      done = run_hold(index, command.frames, fast);
      break;
    }
    if (!done) {
      return;
    }
  }
  finished_ = true;
}

bool EventPlayer::run_text(const std::size_t index, const EventText &text, const bool confirm,
                           const bool fast) {
  if (fast && text.skippable) {
    return true; // fast-forward hides the text
  }
  if (index < reached_ || text.frames_per_character < 1) {
    frame_.texts.push_back({&text, -1});
    return true;
  }
  if (skip_) {
    // Confirm pressed while typing: all of it, once. The original leaves
    // its character count as it was here, which could cut the next text's
    // typing short; the port starts every text from nothing.
    frame_.texts.push_back({&text, -1});
    sound(tuning_.skip_sound);
    typed_ = 0;
    text_frames_ = 0;
    return true;
  }
  if (++text_frames_ >= text.frames_per_character) {
    text_frames_ = 0;
    ++typed_;
    typed_unit(text);
  }
  frame_.texts.push_back({&text, typed_});
  if (text_progress(text.tokens, typed_).complete) {
    typed_ = 0;
    text_frames_ = 0;
    return true;
  }
  if (confirm && text.skippable) {
    skip_ = true;
  }
  return false;
}

// The step just typed shows its newest character: a tick for anything but a
// space, and letters move the mouths.
void EventPlayer::typed_unit(const EventText &text) {
  const TextProgress progress = text_progress(text.tokens, typed_);
  for (std::size_t index = progress.tokens; index > 0; --index) {
    const TextToken &token = text.tokens[index - 1];
    if (is_colour(token)) {
      continue;
    }
    if (token.kind == TextToken::Kind::player_name ||
        (token.kind == TextToken::Kind::glyph && token.glyph != 0)) {
      sound(tuning_.type_sound);
      if (token.kind == TextToken::Kind::player_name ||
          token.glyph > tuning_.speaking_glyph_above) {
        speaking_ = true;
      }
    }
    return;
  }
}

bool EventPlayer::run_wait(const int frames, const bool fast) {
  wait_frames_ = fast ? std::max(frames, 1) : wait_frames_ + 1;
  if (wait_frames_ < 1 || wait_frames_ < frames) {
    return false;
  }
  wait_frames_ = 0;
  fade_in_ = fade_out_ = 0;
  fade_in_started_ = fade_out_started_ = false;
  std::ranges::fill(cast_, CastState{});
  return true;
}

// 0x0c6a0f: passed once, it is not waited for again; fast-forward ends it early.
bool EventPlayer::run_hold(const std::size_t index, const int frames, const bool fast) {
  if (index < reached_) {
    return true;
  }
  wait_frames_ = fast ? frames + 1 : wait_frames_ + 1;
  if (wait_frames_ <= frames) {
    return false;
  }
  wait_frames_ = 0;
  return true;
}

bool EventPlayer::run_confirm(const std::size_t index, const bool confirm, const bool fast) {
  if (index < reached_) {
    return true;
  }
  ++confirm_frames_;
  if (confirm_frames_ < tuning_.confirm_delay_frames || (!confirm && !fast)) {
    return false;
  }
  confirm_frames_ = 0;
  sound(tuning_.page_sound);
  skip_ = false;
  return true;
}

void EventPlayer::run_character(const EventCharacter &command) {
  if (command.character < 0 || std::cmp_greater_equal(command.character, talk_.characters.size())) {
    return;
  }
  const TalkCharacter &character = talk_.characters[static_cast<std::size_t>(command.character)];
  const TalkPose *pose = character.pose(command.pose);
  if (pose == nullptr) {
    return;
  }
  // One fade in and one fade out per page, shared by its characters.
  const int frames = tuning_.character_fade_frames;
  const bool fading_in = command.fade == CharacterFade::in;
  const bool fading_out = command.fade == CharacterFade::out;
  if (fading_in && !fade_in_started_ && fade_in_ == 0) {
    fade_in_ = frames;
    fade_in_started_ = true;
  }
  if (fading_out && !fade_out_started_ && fade_out_ == 0) {
    fade_out_ = frames;
    fade_out_started_ = true;
  }
  if (fading_in && fade_in_started_ && fade_in_ != 0) {
    --fade_in_;
  }
  if (fading_out && fade_out_started_ && fade_out_ != 0) {
    --fade_out_;
  }
  float alpha = 1.0F;
  if (fading_in && fade_in_started_ && fade_in_ != 0) {
    alpha = 1.0F - static_cast<float>(fade_in_) / static_cast<float>(frames);
  } else if (fading_out && fade_out_started_) {
    alpha = static_cast<float>(fade_out_) / static_cast<float>(frames);
  }
  add_part(character, pose->body, command, command.layer, alpha);

  const int slot = cast_slot(command.character);
  if (alpha < 1.0F || slot < 0) {
    return;
  }
  CastState &cast = cast_[static_cast<std::size_t>(slot)];
  if (cast.blink == 0) {
    if (cast.blink_wait == 0) {
      cast.blink_wait = tuning_.blink_wait_frames + random(tuning_.blink_wait_frames);
      cast.blink = tuning_.blink_frames + random(tuning_.blink_random);
    } else {
      --cast.blink_wait;
    }
  } else {
    if (pose->blink) {
      add_part(character, *pose->blink, command, command.layer + 1, alpha);
    }
    --cast.blink;
  }
  if (!command.mouth) {
    return;
  }
  if (cast.mouth_timer == 0) {
    if (speaking_) {
      cast.mouth_shape = (cast.mouth_shape + 1) & 3;
      cast.mouth_timer = tuning_.mouth_hold_frames;
    } else {
      cast.mouth_shape = 0;
    }
  } else {
    --cast.mouth_timer;
  }
  speaking_ = false;
  static constexpr std::array<std::size_t, 4> shape_part{0, 1, 2, 1};
  if (const auto &mouth = pose->mouth[shape_part[static_cast<std::size_t>(cast.mouth_shape)]]) {
    add_part(character, *mouth, command, command.layer + 1, alpha);
  }
}

void EventPlayer::add_part(const TalkCharacter &character, const TalkPart &part,
                           const EventCharacter &command, const int layer, const float alpha) {
  if (part.page < 0 || std::cmp_greater_equal(part.page, character.images.size())) {
    return;
  }
  Sprite sprite;
  sprite.image = character.images[static_cast<std::size_t>(part.page)];
  sprite.rect = part.rect;
  sprite.position = {command.position[0] + part.offset[0], command.position[1] + part.offset[1]};
  sprite.layer = layer;
  sprite.colour = command.colour;
  frame_.sprites.push_back({std::move(sprite), alpha});
}

int EventPlayer::cast_slot(const int character) const {
  const auto found = std::ranges::find(event_.cast, character);
  return found == event_.cast.end() ? -1 : static_cast<int>(found - event_.cast.begin());
}

int EventPlayer::random(const int below) {
  if (below <= 0) {
    return 0;
  }
  return static_cast<int>(random_() % static_cast<std::uint32_t>(below));
}

void EventPlayer::sound(const std::string &name) {
  if (!name.empty()) {
    sounds_.push_back(name);
  }
}

TalkScene::TalkScene(const StoryEvent &event, const TalkTables &talk, EventTuning tuning,
                     std::string next, const std::uint32_t seed)
    : event_(event), talk_(talk), next_(std::move(next)), fade_(tuning.fade_in, tuning.fade_out),
      player_(event, talk, std::move(tuning), seed) {}

void TalkScene::update(const double tick_seconds, const InputFrame &input) {
  seconds_ += tick_seconds;
  if (fade_.update(tick_seconds)) {
    started_ = true;
  }
  if (!started_ || fade_.leaving() || fade_.done()) {
    return;
  }
  player_.update(tick_seconds, input);
  if (player_.finished()) {
    fade_.leave(next_);
  }
}

const TalkBackground *TalkScene::background() const {
  const int index = event_.background;
  if (index < 0 || std::cmp_greater_equal(index, talk_.backgrounds.size())) {
    return nullptr;
  }
  return &talk_.backgrounds[static_cast<std::size_t>(index)];
}

const TalkFrame *TalkScene::frame() const {
  return started_ && !player_.finished() ? &player_.frame() : nullptr;
}

} // namespace phantomcrash
