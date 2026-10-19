#pragma once

// The finale's epilogue on the training screen (desktop_training_finale.cpp):
// the phases playing, the line showing and what the screen keeps of them.

#include "desktop_supply.hpp"
#include "phantomcrash/chase_camera.hpp"
#include "phantomcrash/effect_system.hpp"
#include "phantomcrash/event_player.hpp"
#include "phantomcrash/finale.hpp"
#include "phantomcrash/story_event.hpp"

#include <SDL3/SDL_render.h>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

struct CapturedFrame {
  void operator()(SDL_Texture *texture) const noexcept {
    SDL_DestroyTexture(texture);
  }
};

struct TrainingFinale {
  TrainingFinale(FinaleTables tables, FinaleTuning tuning, std::map<int, SceneTrack> scenes)
      : sequence(std::move(tables), std::move(tuning), std::move(scenes)) {}

  FinaleSequence sequence;
  std::vector<std::vector<EventCommand>> scripts; // FinaleTables::scripts, parsed
  EventTuning line_tuning;                        // the chatter window's
  std::unique_ptr<StoryEvent> event;
  std::unique_ptr<EventPlayer> line; // the line showing, or null
  std::uint32_t lines{};             // lines started, seeding each player
  std::string music;                 // the track asked for last; none before the first
  std::optional<CameraShot> shot;    // the last phase's, held once they have all played
  const FinalePhase *walking{};      // the phase whose walk was last started
  // The frame a dissolve or the TV switching off draws (texture slot 549),
  // and whether to keep the frame drawn next.
  std::unique_ptr<SDL_Texture, CapturedFrame> captured;
  bool capture{};
  // The camera carrier, drawn as the stage's supply carrier, and its
  // effect groups: its own, then the three lights'.
  std::unique_ptr<DesktopSupply> carrier_view;
  float carrier_spin{};
  std::array<std::vector<EffectHandle>, 4> lights;
};

} // namespace phantomcrash
