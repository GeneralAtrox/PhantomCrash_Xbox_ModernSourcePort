#include "desktop_coop_matches.hpp"
#include <algorithm>
#include <SDL3/SDL_log.h>

namespace phantomcrash {

int CoopMatch::join(const int slot, RobotParts parts) {
  auto &player = players.at(slot);
  // Players come in addition to the area's opponents: the first takes the
  // player's place (robot 0), the others a free co-op place past the roster's
  // robots, or a new one. No opponent is ever taken over.
  int actor = -1;
  for (int index = 0; index < static_cast<int>(battle->robots().size()); ++index) {
    const bool opponent = world->robot(area, index) != nullptr;
    const bool taken =
        std::ranges::any_of(players, [index](const Participant &p) { return p.actor == index; });
    if (!opponent && !taken) {
      actor = index;
      break;
    }
  }
  if (actor < 0) {
    actor = battle->add_robot("coop", 0, {}, 0, {}, 1);
  }
  battle->take_out(actor);
  auto &robot = battle->robot(actor);
  robot.piloted = true;
  robot.drone = false;
  robot.auto_respawn = false;
  robot.frozen = false;
  robot.rival = false;
  robot.boss = false;
  robot.leader = false;
  robot.player_number = slot;
  robot.kills = 0;
  robot.lost = {};
  player = {actor, std::move(parts)};
  ++look_revision;
  SDL_Log("Co-op match join: player=%d actor=%d day=%d area=%d contest=%d robots=%zu", slot + 1,
          actor, day + 1, area, contest, battle->robots().size());
  return actor;
}

bool CoopMatch::empty() const {
  return std::ranges::none_of(players, [](const Participant &p) { return p.actor >= 0; });
}

RobotParts CoopMatch::look(const int actor) const {
  for (const auto &player : players) {
    if (player.actor == actor) {
      return player.look;
    }
  }
  const auto *robot = world->robot(area, actor);
  return robot ? robot->look : RobotParts{};
}

void CoopMatch::tick(const float seconds) {
  const auto first =
      std::ranges::find_if(players, [](const Participant &p) { return p.actor >= 0; });
  if (first == players.end()) {
    return;
  }
  elapsed += seconds;
  battle->select_player(first->actor);
  if (entrances) {
    entrances->prepare(*battle, seconds);
  }
  battle->tick({}, {}, seconds);
  collect_rewards();
  if (entrances) {
    entrances->observe(*battle);
  }
  // The other areas wait during a battle, as on the single-player battle
  // screen (rumbling.json fighting_screens: only the area choice runs them);
  // downed opponents there are still replaced.
  world->set_fighting(false);
  world->update(simulation_save, seconds);
}

std::int64_t CoopMatch::leave(const int slot, QuestSave &save) {
  auto &player = players.at(slot);
  if (player.actor < 0) {
    return 0;
  }
  for (const int beaten : player.beaten) {
    if (std::ranges::find(save.beaten, beaten) == save.beaten.end()) {
      save.beaten.push_back(beaten);
    }
  }
  const auto bounty = player.bounty;
  SDL_Log("Co-op match leave: player=%d actor=%d bounty=%lld; teammates continue", slot + 1,
          player.actor, static_cast<long long>(bounty));
  world->release_coop_robot(area, player.actor);
  player.actor = -1;
  ++look_revision;
  return bounty;
}

void CoopMatches::tick(const float seconds) {
  for (auto it = matches.begin(); it != matches.end();) {
    if (it->second->empty()) {
      it->second->world->give_back(it->second->simulation_save);
      it = matches.erase(it);
    } else {
      it->second->tick(seconds);
      ++it;
    }
  }
}

} // namespace phantomcrash
