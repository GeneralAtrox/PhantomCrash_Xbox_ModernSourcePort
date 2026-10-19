#include "desktop_foot_effects.hpp"
#include <SDL3/SDL_log.h>

namespace phantomcrash {
void DesktopFootEffects::stop(Watch &watch, EffectSystem &effects) {
  for (auto &contact : watch.contacts) {
    effects.stop(contact.takeoff, true);
    contact.takeoff = 0;
  }
}

void DesktopFootEffects::tick(const Battle &battle, EffectSystem &effects, const float seconds) {
  const auto &robots = battle.robots();
  for (std::size_t i = robots.size(); i < robots_.size(); ++i) {
    stop(robots_[i], effects);
  }
  robots_.resize(robots.size());
  for (std::size_t i = 0; i < robots.size(); ++i) {
    const auto &robot = robots[i];
    auto &watch = robots_[i];
    const auto &mover = robot.mover;
    if (watch.owner != robot.pose.get() || !robot.alive || mover.mode() == MoveMode::scripted ||
        mover.mode() == MoveMode::hit) {
      stop(watch, effects);
      watch = {};
      watch.owner = robot.pose.get();
    }
    if (!robot.pose || !robot.alive || mover.mode() == MoveMode::scripted ||
        mover.mode() == MoveMode::hit) {
      watch.grounded = mover.grounded();
      continue;
    }
    const auto &points = robot.pose->pose()[0].contacts;
    const auto &groups = robot.pose->movement_groups();
    if (watch.contacts.size() != points.size()) {
      stop(watch, effects);
      watch.contacts.assign(points.size(), {});
    }
    const bool takeoff = watch.grounded && !mover.grounded() && mover.velocity().y > 0;
    const bool landed = !watch.grounded && mover.grounded();
    for (std::size_t slot = 0; slot < points.size(); ++slot) {
      const auto &point = points[slot];
      auto &contact = watch.contacts[slot];
      if (contact.takeoff) {
        contact.remaining -= seconds;
        if (contact.remaining <= 0 || landed || !effects.alive(contact.takeoff)) {
          effects.stop(contact.takeoff, true);
          contact.takeoff = 0;
        } else {
          effects.move(contact.takeoff, point.position);
        }
      }
      const bool crossed = contact_stepped(contact.previous, point);
      contact.previous = point;
      if (!(takeoff || landed || (crossed && mover.grounded()))) {
        continue;
      }
      // Read the contacted material instead of emitting dust over empty air.
      const auto ground = battle.stage().segment_contact(point.position + Vec3{0, robot.radius, 0},
                                                         point.position - Vec3{0, robot.radius, 0});
      if (!ground || ground->normal.y < ground_normal_y) {
        continue;
      }
      const int surface = ground->surface % 1000;
      const std::size_t material = surface < 3 ? 0 : surface < 7 ? 1 : 2;
      int group = -1;
      EffectHandle handle = 0;
      if (takeoff && material < 2) {
        effects.stop(contact.takeoff, true);
        group = groups.takeoff;
        handle = contact.takeoff = effects.start(group, point.position);
        contact.remaining = groups.takeoff_seconds;
      } else {
        // Original state 5 is air/landing, 6 the ground dodge; ordinary ground
        // movement shares the first table row (0x242bd8).
        const std::size_t state = landed ? 5 : mover.mode() == MoveMode::dodge ? 6 : 0;
        if (state < groups.contact.size()) {
          group = groups.contact[state][material];
          handle = effects.start(group, ground->position);
        }
      }
      if (robot.piloted && group >= 0) {
        SDL_Log("Movement contact: robot %zu foot %zu %s group %d surface %d started %s", i, slot,
                takeoff  ? "takeoff"
                : landed ? "landing"
                         : "step",
                group, surface, handle ? "yes" : "no");
      }
    }
    watch.grounded = mover.grounded();
  }
}
} // namespace phantomcrash
