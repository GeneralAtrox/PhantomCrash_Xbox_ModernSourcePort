#include "ai_sim_navdump.hpp"

#include "phantomcrash/nav_graph.hpp"

#include <array>
#include <cmath>

namespace phantomcrash::ai_sim {
namespace {

constexpr float look_up = 24.0F;      // how far above a node the room test looks
constexpr float side_offset = 1.2F;   // the room test also looks this far to each side
constexpr float probe_step = 0.5F;    // the body probe's height step
constexpr float floor_look_up = 3.0F; // the floor map looks for floor under this height
constexpr float sweep_step = 0.25F;   // the drop sweep moves the body this far a test
constexpr const char *link_names[] = {"walk", "jump", "drop"};

// The clear height above `feet` (look_up when nothing is hit).
float room(const StageCollision &stage, const Vec3 feet) {
  const Vec3 from = feet + Vec3{0.0F, 0.3F, 0.0F};
  const auto hit = stage.segment_hit(from, from + Vec3{0.0F, look_up, 0.0F});
  return hit ? hit->y - feet.y : look_up;
}

} // namespace

bool dump_nav(SimData &data, const ArenaOptions &options, const int area, const WatchArea &around,
              std::ostream &out) {
  SimArena arena(data, options, area, 1);
  if (!arena.ready()) {
    return false;
  }
  const StageCollision &stage = arena.battle().stage();
  NavTuning tuning = arena.battle().settings().ai.navigation;
  tuning.background = false;
  const NavGraph graph = NavGraph::build(stage, tuning);
  out << "stage " << arena.stage_name() << ": " << graph.nodes().size() << " nodes\n";
  // The floor under the spot as the battle finds it (ground_snap above the
  // feet at most) and with more reach above.
  out << "nearest node: " << graph.nearest(around.at) << '\n';
  for (const float above : {ground_snap, tuning.step, tuning.headroom}) {
    out << "floor within +" << above << ": " << stage.ground_height(around.at, above, 0.0F) << '\n';
  }
  // The robot's body rising straight up from the centre: how the stage
  // pushes a sphere of body_radius at each height (a push down stops a jump).
  for (float rise = 0.0F; rise <= tuning.jump_rise + tuning.body_radius; rise += probe_step) {
    const Vec3 start = around.at + Vec3{0.0F, tuning.body_radius + rise, 0.0F};
    Vec3 centre = start;
    const unsigned contact = stage.push_sphere(centre, tuning.body_radius);
    const Vec3 push = centre - start;
    if (contact != 0) {
      out << "body +" << rise << " push (" << push.x << ", " << push.y << ", " << push.z
          << ") contact " << contact << '\n';
    }
  }
  for (const int node : graph.nodes_near(around.at.x, around.at.z, around.radius)) {
    const NavNode &n = graph.nodes()[static_cast<std::size_t>(node)];
    const Vec3 p = n.position;
    std::array<float, 5> rooms{room(stage, p)};
    const std::array<Vec3, 4> sides{Vec3{side_offset, 0, 0}, Vec3{-side_offset, 0, 0},
                                    Vec3{0, 0, side_offset}, Vec3{0, 0, -side_offset}};
    for (std::size_t k = 0; k < sides.size(); ++k) {
      rooms[k + 1] = room(stage, p + sides[k]);
    }
    out << "node " << node << " (" << p.x << ", " << p.y << ", " << p.z << ")"
        << (n.ledge ? " ledge" : "") << " room";
    for (const float r : rooms) {
      out << ' ' << std::round(r * 10.0F) / 10.0F;
    }
    out << '\n';
    for (const NavEdge &e : graph.edges_of(node)) {
      if (e.link == NavLink::walk) {
        continue;
      }
      const Vec3 q = graph.nodes()[e.to].position;
      out << "  " << link_names[static_cast<int>(e.link)] << " -> " << e.to << " (" << q.x << ", "
          << q.y << ", " << q.z << ") cost " << e.cost;
      if (e.link == NavLink::drop) {
        // The body walked level toward the landing: where the stage first
        // turns it back (a lip or rail the link's line tests miss).
        const Vec3 flat_to{q.x - p.x, 0.0F, q.z - p.z};
        const float span = std::hypot(flat_to.x, flat_to.z);
        for (float along = sweep_step; along <= span; along += sweep_step) {
          const Vec3 start = p + flat_to * (along / span) + Vec3{0, tuning.body_radius + 0.1F, 0};
          Vec3 centre = start;
          const unsigned contact = stage.push_sphere(centre, tuning.body_radius);
          const Vec3 back = centre - start;
          if (contact != 0 && (back.x * flat_to.x + back.z * flat_to.z) < -0.01F * span) {
            out << " | body turned back at " << along << " of " << span << " push (" << back.x
                << ", " << back.y << ", " << back.z << ") contact " << contact;
            for (const float height : {0.5F, 1.0F, 2.5F, 4.0F}) {
              const Vec3 from = p + Vec3{0, height, 0};
              const auto hit = stage.segment_hit(from, Vec3{q.x, from.y, q.z});
              out << " | line +" << height << ' ';
              if (hit) {
                out << '(' << hit->x << ", " << hit->y << ", " << hit->z << ')';
              } else {
                out << "clear";
              }
            }
            break;
          }
        }
      }
      out << '\n';
    }
  }
  return true;
}

bool dump_floor(SimData &data, const ArenaOptions &options, const int area, const WatchArea &around,
                std::ostream &out) {
  SimArena arena(data, options, area, 1);
  if (!arena.ready()) {
    return false;
  }
  const StageCollision &stage = arena.battle().stage();
  const int reach = static_cast<int>(std::ceil(around.radius));
  out << "stage " << arena.stage_name() << ": floor under y " << around.at.y + floor_look_up
      << ", x " << around.at.x - reach << ".." << around.at.x + reach << " across\n";
  for (int dz = reach; dz >= -reach; --dz) {
    const float z = around.at.z + static_cast<float>(dz);
    out << "z " << z << ':';
    for (int dx = -reach; dx <= reach; ++dx) {
      const Vec3 top{around.at.x + static_cast<float>(dx), around.at.y + floor_look_up, z};
      const float floor = stage.ground_height(top, 0.0F);
      out << ' ';
      if (floor == no_ground) {
        out << "--";
      } else {
        out << std::round(floor * 10.0F) / 10.0F;
      }
    }
    out << '\n';
  }
  return true;
}

} // namespace phantomcrash::ai_sim
