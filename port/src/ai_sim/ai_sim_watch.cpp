#include "ai_sim_watch.hpp"

#include <cmath>

namespace phantomcrash::ai_sim {

void write_watch_header(std::ostream &out) {
  out << "run,t,robot,x,y,z,grounded,contact,mode,submode,search,target,visible,nav,route,next,"
         "link,wp_x,wp_y,wp_z,goal_x,goal_y,goal_z,stall,lifting,rough,vantage,holding,"
         "route_x,route_y,route_z,route_stalled,recoveries,steer,forward,jump,move,"
         "move_seconds,rise,reach,fallback,unreachable,rejoin,asked,path,pickup,jump_phase\n";
}

void write_watch(std::ostream &out, const int run, const double t, const Battle &battle,
                 const WatchArea &area) {
  const auto &robots = battle.robots();
  for (std::size_t index = 0; index < robots.size(); ++index) {
    const Combatant &robot = robots[index];
    const Vec3 at = robot.mover.position();
    if (robot.piloted || !robot.alive ||
        std::hypot(at.x - area.at.x, at.z - area.at.z) > area.radius) {
      continue;
    }
    const RobotAiState &ai = robot.ai;
    const NavAgent &nav = ai.nav;
    const bool ahead = nav.next < nav.route.size();
    const NavWaypoint w = ahead ? nav.route[nav.next] : NavWaypoint{};
    out << run << ',' << t << ',' << index << ',' << at.x << ',' << at.y << ',' << at.z << ','
        << robot.mover.grounded() << ',' << static_cast<int>(robot.stage_contact) << ','
        << static_cast<int>(ai.mode) << ',' << ai.submode << ',' << ai.search << ',' << ai.target
        << ',' << ai.visible << ',' << nav.active << ',' << nav.route.size() << ',' << nav.next
        << ',' << (ahead ? static_cast<int>(w.link) : -1) << ',' << w.position.x << ','
        << w.position.y << ',' << w.position.z << ',' << nav.goal.x << ',' << nav.goal.y << ','
        << nav.goal.z << ',' << nav.stall << ',' << nav.lifting << ',' << nav.rough << ','
        << nav.vantage << ',' << nav.holding << ',' << ai.route_target.x << ',' << ai.route_target.y
        << ',' << ai.route_target.z << ',' << ai.route_stalled << ',' << ai.recoveries << ','
        << ai.control.move[0] << ',' << ai.control.move[1] << ',' << ai.control.jump << ','
        << static_cast<int>(robot.mover.mode()) << ',' << robot.mover.mode_seconds() << ','
        << robot.mover.velocity().y << ',' << held_jump_rise(robot.mover.tuning()) << ','
        << nav.fallback_left << ',' << nav.unreachable << ',' << ai.route_rejoin << ',' << nav.asked
        << ',' << robot.path.active() << ',' << ai.pickup << ',' << ai.jump_phase << '\n';
  }
}

} // namespace phantomcrash::ai_sim
