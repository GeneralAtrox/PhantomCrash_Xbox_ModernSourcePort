#include "desktop_battle_chatter.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_event_text.hpp"

#include <SDL3/SDL.h>

namespace phantomcrash {

ChatterWindow::ChatterWindow(TextureCache &textures, const GlyphFont &font, const TalkTables &talk,
                             EventTuning tuning, ChatterTables tables, std::string sound)
    : textures_(textures), font_(font), talk_(talk), tuning_(std::move(tuning)),
      tables_(std::move(tables)), chatter_(tables_), sound_(std::move(sound)) {
  for (const json::JsonValue &script : tables_.scripts) {
    textures_.preload(script);
  }
}

void ChatterWindow::set_speaker(const int robot, ChatterSpeaker speaker) {
  for (const std::optional<Sprite> *face : {&speaker.robot_face, &speaker.pilot_face}) {
    if (*face && !(*face)->image.empty()) {
      static_cast<void>(textures_.get((*face)->image));
    }
  }
  chatter_.set_speaker(robot, std::move(speaker));
}

void ChatterWindow::close() {
  player_.reset();
  event_.reset();
}

void ChatterWindow::tick(const Battle &battle, const double tick_seconds, const bool quiet) {
  for (const BattleEvent &event : battle.events()) {
    if (event.kind == BattleEventKind::chatter) {
      chatter_.raise(event.robot, event.chatter);
    }
  }
  if (quiet) {
    close();
  }
  const std::optional<ChatterLine> line = chatter_.take(quiet || player_ != nullptr);
  if (line && !quiet) {
    start(*line, *chatter_.speaker(line->robot));
  }
  if (!player_) {
    return;
  }
  // The battle's buttons fire weapons: none of them hurries a line along.
  player_->update(tick_seconds, InputFrame{});
  for (std::string &sound : player_->take_sounds()) {
    sounds_.push_back(std::move(sound));
  }
  if (player_->finished()) {
    close();
  }
}

void ChatterWindow::start(const ChatterLine &line, const ChatterSpeaker &speaker) {
  event_ = std::make_unique<StoryEvent>();
  event_->commands = chatter_script(tables_, line, speaker, font_);
  for (const EventCommand &command : event_->commands) {
    if (command.op == EventOp::sprite && !command.sprite.image.empty()) {
      static_cast<void>(textures_.get(command.sprite.image));
    }
  }
  player_ = std::make_unique<EventPlayer>(*event_, talk_, tuning_, ++lines_);
  if (!sound_.empty()) {
    sounds_.push_back(sound_);
  }
}

bool ChatterWindow::say_defeat(const int robot, const int area) {
  if (area < 0 || area >= static_cast<int>(tables_.ranker_defeats.size()) ||
      tables_.ranker_defeats[static_cast<std::size_t>(area)] < 0) {
    return false;
  }
  close();
  // The line names and shows its ranker itself.
  const ChatterSpeaker *speaker = chatter_.speaker(robot);
  start({robot, -1, tables_.ranker_defeats[static_cast<std::size_t>(area)]},
        speaker != nullptr ? *speaker : ChatterSpeaker{});
  return true;
}

void ChatterWindow::draw(const Viewport &viewport) const {
  if (!player_) {
    return;
  }
  DrawList list(textures_, font_, viewport, player_->seconds());
  const TalkFrame &frame = player_->frame();
  for (const auto &sprite : frame.sprites) {
    list.sprite(sprite.sprite, sprite.alpha);
  }
  static const std::vector<int> no_player_name;
  for (const auto &text : frame.texts) {
    draw_event_text(list, font_, text, player_->seconds(), no_player_name);
  }
  list.flush();
}

std::unique_ptr<ChatterWindow> load_chatter_window(TextureCache &textures, const GlyphFont &font,
                                                   const DataRoots &roots, const TalkTables &talk,
                                                   EventTuning tuning,
                                                   const TrainingBattle &setup) {
  const auto file = roots.find_asset(setup.chatter);
  if (!file) {
    SDL_Log("Battle chatter %s is missing; convert the robots again", setup.chatter.c_str());
    return nullptr;
  }
  try {
    tuning.frame_rate = setup.chatter_frame_rate;
    return std::make_unique<ChatterWindow>(textures, font, talk, std::move(tuning),
                                           parse_chatter_tables(json::read_file(*file)),
                                           setup.chatter_sound);
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", setup.chatter.c_str(), error.what());
  }
  return nullptr;
}

ChatterSpeaker chip_speaker(const ChipRecord &chip, const std::string &chip_name,
                            std::string player_name) {
  ChatterSpeaker speaker;
  speaker.table = chip.chatter;
  speaker.kind = ChatterKind::player;
  speaker.robot_name = chip_name.empty() ? chip.name : chip_name;
  speaker.pilot_name = std::move(player_name);
  speaker.robot_face = chip.face;
  return speaker;
}

} // namespace phantomcrash
