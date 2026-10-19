#include "desktop_online_match.hpp"
#include "online/state_sections.hpp"

namespace phantomcrash::online {
StateDigest state_digest(const CoopMatch &match, AssetDigests *assets) {
  StateWriter out(assets);
  // Transport, prediction history and GPU/resource owners are outside simulation.
  out(std::uint32_t{1}, match.day, match.area, match.contest, match.world, match.battle,
      match.simulation_save, match.entrances, match.rider_scenes, match.simulation_looks,
      match.players, match.settlements, match.look_revision, match.elapsed, match.options);
  return out.finish();
}

std::vector<std::string> state_sections(const CoopMatch &match) {
  StateSections out;
  out.begin("match");
  out.add("day", match.day);
  out.add("area", match.area);
  out.add("contest", match.contest);
  out.add("world", match.world);
  out.add("battle", match.battle);
  out.add("simulation_save", match.simulation_save);
  out.add("entrances", match.entrances);
  out.add("rider_scenes", match.rider_scenes);
  out.add("simulation_looks", match.simulation_looks);
  out.add("players", match.players);
  out.add("settlements", match.settlements);
  out.add("look_revision", match.look_revision);
  out.add("elapsed", match.elapsed);
  out.add("options", match.options);
  out.show("day", match.day);
  out.show("area", match.area);
  out.show("contest", match.contest);
  out.show("elapsed", match.elapsed);
  for (std::size_t slot = 0; slot < match.players.size(); ++slot) {
    const auto &player = match.players[slot];
    out.begin("player[" + std::to_string(slot + 1) + "]");
    out.add("all", player);
    out.add("look", player.look);
    out.add("beaten", player.beaten);
    out.add("finished", player.finished);
    out.add("rider_scene", match.rider_scenes[slot]);
    out.add("settlement", match.settlements[slot]);
    out.show("actor", player.actor);
    out.show("bounty", player.bounty);
    out.show("won", player.won);
    out.show("play_seconds", player.play_seconds);
    out.show("victory_hold", player.victory_hold);
    out.show("finished", player.finished ? int(*player.finished) : -1);
  }
  if (match.world) {
    StateInspector::sections(out, *match.world);
  }
  if (match.battle) {
    StateInspector::sections(out, *match.battle);
  }
  return out.take();
}
} // namespace phantomcrash::online
