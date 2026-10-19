// RUMBLING's live opponents (rumbling_world.hpp): each one out drawn on the
// area map by its maker and legs type (0x01492b), the one CHARACTER INFO
// shows lit with a ring turning round it, and its armour on the card.

#include "desktop_draw_list.hpp"
#include "desktop_rumbling.hpp"
#include "desktop_coop_matches.hpp"
#include "phantomcrash/rumbling_robot.hpp"

#include <cmath>
#include <string>

namespace phantomcrash {

std::shared_ptr<CoopMatch> RumblingScreen::live_match() const {
  if (!coop_matches_) {
    return {};
  }
  const CoopMatches::Key key{logic_.quest().day, logic_.area(), logic_.contest(logic_.area())};
  const auto found = coop_matches_->matches.find(key);
  return found == coop_matches_->matches.end() ? nullptr : found->second;
}

void RumblingScreen::draw_coop_robots(DrawList &list, const CoopMatch &match) const {
  const auto &markers = logic_.script().robot_markers;
  const auto &map = logic_.script().areas.at(logic_.area()).robot_map;
  const auto &check = logic_.check();
  const auto *shown = shown_member();
  for (int actor = 0; actor < static_cast<int>(match.battle->robots().size()); ++actor) {
    const auto &robot = match.battle->robots()[actor];
    const auto position = robot.mover.position();
    if (!robot.alive || robot.hp <= 0 || robot.mover.mode() == MoveMode::scripted ||
        !match.battle->stage().in_play_area(position)) {
      continue;
    }
    int player = -1;
    for (int slot = 0; slot < static_cast<int>(match.players.size()); ++slot) {
      if (match.players[slot].actor == actor) {
        player = slot;
      }
    }
    // Human actors include slot zero and extra actors in ranker-only matches;
    // neither is necessarily represented by an NPC roster member.
    if (robot.piloted && player < 0) {
      continue;
    }
    RobotRecord record;
    record.parts[static_cast<std::size_t>(PartSlot::legs)] = RobotPart{match.look(actor).legs, 0};
    const auto kind = robot_kind(check.parts, check.rules, record);
    const auto icon = markers.icons.find({kind.maker, kind.legs_type});
    if (icon == markers.icons.end()) {
      continue;
    }
    const auto *npc = player < 0 ? match.world->robot(match.area, actor) : nullptr;
    const bool selected = shown && npc && shown->entrant == npc->entrant;
    Sprite sprite = icon->second;
    sprite.position = rumbling_map_point(markers, map, position.x, position.z);
    sprite.centered = true;
    sprite.angle = rumbling_map_degrees(map, robot.mover.heading());
    sprite.colour = player >= 0 || selected ? markers.chosen_colour : markers.colour;
    list.sprite(sprite);
    if (player >= 0) {
      const auto at = sprite.position;
      auto label = layout_run(font_, glyph_run(font_, "P" + std::to_string(player + 1)), 0, 0);
      for (auto &glyph : label) {
        glyph.x = at[0] + glyph.x * 0.5F;
        glyph.y = at[1] + glyph.y * 0.5F;
        glyph.width *= 0.5F;
        glyph.height *= 0.5F;
      }
      list.text(sprite.layer + 1, std::move(label), markers.chosen_colour);
    }
    if (selected) {
      Sprite ring = markers.ring;
      ring.position = sprite.position;
      ring.centered = true;
      ring.angle =
          std::fmod(static_cast<float>(logic_.seconds()) * markers.ring_degrees_per_second, 360.0F);
      list.sprite(ring);
    }
  }
}

const RosterMember *RumblingScreen::shown_member() const {
  const int area = logic_.area();
  const auto card = static_cast<std::size_t>(logic_.card());
  if (logic_.view() != RumblingView::character_info || area < 0 || area >= roster_areas ||
      card >= logic_.cards().size()) {
    return nullptr;
  }
  const int entrant = logic_.cards()[card].entrant;
  for (const RosterMember &member :
       logic_.quest().rosters[static_cast<std::size_t>(area)].members) {
    if (member.entrant == entrant) {
      return &member;
    }
  }
  return nullptr;
}

void RumblingScreen::draw_robots(DrawList &list) const {
  const RumblingScript &script = logic_.script();
  const RumblingRobotMarkers &markers = script.robot_markers;
  const int area = logic_.area();
  if (!markers.present || area < 0 || area >= roster_areas) {
    return;
  }
  if (const auto match = live_match()) {
    if (logged_match_.lock() != match) {
      SDL_Log("Co-op live map: day=%d area=%d contest=%d source=shared battle robots=%zu",
              match->day + 1, match->area, match->contest, match->battle->robots().size());
      logged_match_ = match;
    }
    draw_coop_robots(list, *match);
    return;
  }
  logged_match_.reset();
  const auto &members = logic_.quest().rosters[static_cast<std::size_t>(area)].members;
  if (members.size() < markers.fewest) {
    return;
  }
  const RumblingMapProjection &map = script.areas[static_cast<std::size_t>(area)].robot_map;
  const RegulationCheck &check = logic_.check();
  const RosterMember *shown = shown_member();
  for (const RosterMember &member : members) {
    const LiveRobot &live = member.live;
    const Entrant *who = contests_.entrant(member.entrant);
    const RobotRecord *record = who != nullptr ? check.garage.robot(who->robot) : nullptr;
    if (!live.placed || live.down || record == nullptr) {
      continue;
    }
    const RobotKind kind = robot_kind(check.parts, check.rules, *record);
    const auto icon = markers.icons.find({kind.maker, kind.legs_type});
    if (icon == markers.icons.end()) {
      continue;
    }
    const std::array<float, 2> at =
        rumbling_map_point(markers, map, live.position[0], live.position[2]);
    Sprite sprite = icon->second;
    sprite.position = at;
    sprite.centered = true;
    sprite.angle = rumbling_map_degrees(map, live.heading);
    sprite.colour = &member == shown ? markers.chosen_colour : markers.colour;
    list.sprite(sprite);
    if (&member == shown) {
      Sprite ring = markers.ring;
      ring.position = at;
      ring.centered = true;
      ring.angle =
          std::fmod(static_cast<float>(logic_.seconds()) * markers.ring_degrees_per_second, 360.0F);
      list.sprite(ring);
    }
  }
}

void RumblingScreen::draw_armour(DrawList &list, const int entrant) const {
  const RumblingCharacterInfo &info = logic_.script().character_info;
  const RosterMember *member = shown_member();
  if (!info.show_armour || member == nullptr || member->entrant != entrant ||
      !member->live.placed) {
    return;
  }
  const LiveRobot &live = member->live;
  const std::string line =
      live.down ? info.down_label
                : info.armour_label + std::to_string(static_cast<int>(std::ceil(live.armour))) +
                      "/" + std::to_string(static_cast<int>(std::ceil(live.max_armour)));
  list.text(info.text_layer,
            layout_run(font_, glyph_run(font_, line), info.armour[0], info.armour[1]),
            info.text_colour);
}

} // namespace phantomcrash
