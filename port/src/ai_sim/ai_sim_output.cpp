#include "ai_sim_output.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace phantomcrash::ai_sim {
namespace {

std::string quoted(const std::string &text) {
  std::string out = "\"";
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out + '"';
}

// Builds one JSON object, members in the order written.
class Line {
public:
  Line &operator()(const char *key, const double value) {
    next(key) << std::setprecision(6) << value;
    return *this;
  }
  Line &operator()(const char *key, const int value) {
    next(key) << value;
    return *this;
  }
  Line &operator()(const char *key, const unsigned value) {
    next(key) << value;
    return *this;
  }
  Line &operator()(const char *key, const bool value) {
    next(key) << (value ? "true" : "false");
    return *this;
  }
  Line &operator()(const char *key, const std::string &value) {
    next(key) << quoted(value);
    return *this;
  }
  template <typename Range, typename Write>
  Line &list(const char *key, const Range &values, Write write) {
    next(key) << '[';
    bool first = true;
    for (const auto &value : values) {
      if (!first) {
        text_ << ',';
      }
      first = false;
      write(text_, value);
    }
    text_ << ']';
    return *this;
  }
  void end(std::ostream &out) {
    out << text_.str() << "}\n";
  }

private:
  std::ostream &next(const char *key) {
    text_ << (first_ ? "{" : ",") << quoted(key) << ':';
    first_ = false;
    return text_;
  }
  std::ostringstream text_;
  bool first_{true};
};

} // namespace

void write_life(std::ostream &out, const LifeStats &life) {
  Line line;
  line("run", life.run)("area", life.area)("robot", life.robot)("entrant", life.entrant);
  line("ability", life.ability)("profile", life.profile)("name", life.name)("tag", life.tag);
  line("radius", life.radius)("jump_rise", life.jump_rise);
  line.list("weapons", life.weapons,
            [](std::ostream &text, const std::string &name) { text << quoted(name); });
  line("start", life.start)("end", life.end)("ended", life.ended)("alive", life.alive);
  line.list("mode", life.mode, [](std::ostream &text, double v) { text << v; });
  line("target", life.target)("visible", life.visible)("in_reach", life.in_reach);
  line("request", life.request)("locked", life.locked)("trigger", life.trigger);
  line("opportunity", life.opportunity)("opportunity_no_target", life.opportunity_no_target);
  line("opportunity_holding", life.opportunity_holding);
  line("moving", life.moving)("stuck", life.stuck)("still_fighting", life.still_fighting);
  line("still_idle", life.still_idle)("stuck_episodes", life.stuck_episodes);
  line("long_stuck_episodes", life.long_stuck_episodes)("longest_stuck", life.longest_stuck);
  line("blocked", life.blocked)("blocked_episodes", life.blocked_episodes);
  line("long_blocked_episodes", life.long_blocked_episodes)("longest_blocked",
                                                            life.longest_blocked);
  line("distance", life.distance_travelled);
  line("shots", life.shots)("hits", life.hits)("kills", life.kills);
  line("assists", life.assists)("pickups", life.pickups);
  line("damage_dealt", life.damage_dealt)("damage_taken", life.damage_taken);
  line("first_seen", life.first_seen)("first_shot", life.first_shot);
  line("first_hit", life.first_hit);
  line.list("reactions", life.reactions, [](std::ostream &text, double v) { text << v; });
  line("acquisitions", life.acquisitions)("unanswered", life.unanswered);
  line("transitions", life.transitions)("detections", life.detections);
  line("dodges", life.dodges)("recoveries", life.recoveries);
  line("nearest_mean", life.nearest_count > 0 ? life.nearest_sum / life.nearest_count : -1.0);
  line("no_enemy_near", life.no_enemy_near);
  line("player_target", life.player_target)("player_damage", life.player_damage);
  const BehaviourStats &b = life.behaviour;
  const auto number = [](std::ostream &text, const auto v) { text << v; };
  line.list("slot_shots", b.slot_shots, number).list("slot_ready", b.slot_ready, number);
  line.list("slot_in_reach", b.slot_in_reach, number);
  line("camouflaged", b.camouflaged)("camouflage_uses", b.camouflage_uses);
  line("camouflage_threat_uses", b.camouflage_threat_uses);
  line("dodges_threat", b.dodges_threat)("dodges_other", b.dodges_other);
  line("heavy_hits", b.heavy_hits)("heavy_hit_dodges", b.heavy_hit_dodges);
  line("locked_on", b.locked_on)("locked_on_dodges", b.locked_on_dodges);
  line("shot_free", b.shot_free)("turned_free", b.turned_free);
  line("shot_busy", b.shot_busy)("turned_busy", b.turned_busy)("turn_seconds", b.turn_seconds);
  line("kills_after", b.kills_after)("hunted", b.hunted)("hunt_seconds", b.hunt_seconds);
  line("no_target", b.no_target)("no_target_moving", b.no_target_moving);
  line.end(out);
}

void write_run(std::ostream &out, const RunStats &run) {
  Line line;
  line("run", run.run)("area", run.area)("contest", run.contest)("day", run.day);
  line("stage", run.stage)("player", run.player)("seconds", run.seconds);
  line("wall_seconds", run.wall_seconds)("lives", run.lives)("kills", run.kills);
  line("destroyed", run.destroyed)("withdrawn", run.withdrawn)("shots", run.shots);
  line("hits", run.hits)("damage", run.damage)("first_kill", run.first_kill);
  line("first_shot", run.first_shot)("alive_robot_seconds", run.alive_robot_seconds);
  line("player_damage_taken", run.player_damage_taken);
  line("player_targeted", run.player_targeted);
  line("player_first_targeted", run.player_first_targeted);
  line("player_shots_at", run.player_shots_at);
  line("player_attackers", run.player_attackers)("player_attacked", run.player_attacked);
  line("player_piled", run.player_piled)("player_alive", run.player_alive);
  line("rival_attackers", run.rival_attackers)("rival_attacked", run.rival_attacked);
  line("rival_piled", run.rival_piled)("rival_alive", run.rival_alive);
  line.end(out);
}

void write_stuck_header(std::ostream &out) {
  out << "run,area,robot,entrant,t,seconds,x,y,z,mode,nav,purpose,target,goal_x,goal_y,goal_z,"
         "wp_x,wp_y,wp_z,link,crowd,recoveries\n";
}

void write_stuck(std::ostream &out, const StuckPoint &point) {
  out << point.run << ',' << point.area << ',' << point.robot << ',' << point.entrant << ','
      << point.t << ',' << point.seconds << ',' << point.at.x << ',' << point.at.y << ','
      << point.at.z << ',' << point.mode << ',' << (point.nav ? 1 : 0) << ','
      << static_cast<int>(point.purpose) << ',' << (point.target ? 1 : 0) << ',' << point.goal.x
      << ',' << point.goal.y << ',' << point.goal.z << ',' << point.waypoint.x << ','
      << point.waypoint.y << ',' << point.waypoint.z << ',' << point.link << ',' << point.crowd
      << ',' << point.recoveries << '\n';
}

void write_trace_header(std::ostream &out) {
  out << "run,area,t,robot,entrant,x,y,z,hp,mode,target,visible,nav,stall,requests,triggers,"
         "steer,forward\n";
}

} // namespace phantomcrash::ai_sim
