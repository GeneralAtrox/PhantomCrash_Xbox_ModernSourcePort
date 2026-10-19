#include "test_support.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/fence_impacts.hpp"

#include <cmath>
#include <numeric>

using namespace phantomcrash;

namespace {
FenceLook look() {
  FenceLook out;
  out.model = "models/binlink/197.gltf";
  out.meshes.resize(30);
  std::iota(out.meshes.begin(), out.meshes.end(), 0);
  return out;
}

StageCollision wall(const float x, const int surface) {
  CollisionTriangle t{{Vec3{x, 0, -50}, Vec3{x, 0, 50}, Vec3{x, 50, 0}}, {-1, 0, 0}, surface};
  return StageCollision({t}, {});
}

Landing shoot(const StageCollision &stage, const std::vector<Target> &targets = {}) {
  static const WeaponTuning gun = [] {
    WeaponTuning out;
    out.speed = 600;
    return out;
  }();
  Armory armory;
  armory.fire(gun, 0, 0, {0, 15, 0}, {1, 0, 0}, -1, targets);
  std::vector<WeaponHit> hits;
  armory.tick(1.0F / 30, targets, stage, hits);
  const auto landings = armory.take_landings();
  CHECK(landings.size() == 1);
  return landings[0];
}
} // namespace

TEST_CASE("shots above a fence retain its surface and normal at the impact") {
  const auto hit = shoot(wall(10, 1008));
  REQUIRE(hit.surface);
  CHECK(hit.surface->surface == 1008);
  CHECK(hit.surface->normal.x == -1);
  CHECK(std::abs(hit.at.x - 10) < 0.001F);
  CHECK(hit.at.y == 15);
  CHECK(!hit.on_robot);
}

TEST_CASE("an earlier wall or target does not inherit a fence hit behind it") {
  const auto fence = wall(10, 8);
  auto triangles = fence.walls();
  triangles.push_back(wall(5, 9).walls()[0]);
  const auto hit = shoot(StageCollision(triangles, {}));
  REQUIRE(hit.surface);
  CHECK(hit.surface->surface == 9);
  const auto robot = shoot(fence, {{{5, 15, 0}, 1, 1, true}});
  CHECK(robot.on_robot);
  CHECK(!robot.surface);
}

TEST_CASE("fence effects align to the hit plane and use the extracted scale") {
  FenceImpacts effects(look());
  effects.start({{10, 15, 0}, {-1, 0, 0}, 8});
  const auto frames = effects.frames();
  REQUIRE(frames.size() == 1);
  CHECK(frames[0].mesh == 0);
  const auto centre = transform_point({}, frames[0].world);
  CHECK(std::abs(centre.x - 10.5F) < 0.001F);
  CHECK(centre.y == 15);
  const auto normal = transform_point({0, 1, 0}, frames[0].world) - centre;
  CHECK(std::abs(normal.x + 0.2F) < 0.001F);
  CHECK(frames[0].refraction.radius == 0);
  CHECK(optical_seen(frames[0].refraction, centre) == 0);
}

TEST_CASE("fence animation advances at thirty frames per second independently of updates") {
  for (const int rate : {30, 60, 144}) {
    FenceImpacts effects(look());
    effects.start({{}, {0, 1, 0}, 8});
    for (int step = 0; step < rate / 2; ++step) {
      effects.tick(1.0F / rate);
    }
    const auto frames = effects.frames();
    REQUIRE(frames.size() == 1);
    CHECK(frames[0].mesh == 15);
    for (int step = 0; step < rate / 2 + 1; ++step) {
      effects.tick(1.0F / rate);
    }
    CHECK(effects.frames().empty());
  }
}

TEST_CASE("fence effects ignore ordinary surfaces and recycle their bounded pool") {
  FenceLook tuning = look();
  tuning.capacity = 2;
  FenceImpacts effects(tuning);
  effects.start({{}, {0, 1, 0}, 9});
  CHECK(effects.frames().empty());
  for (int i = 0; i < 3; ++i) {
    effects.start({{}, {0, 1, 0}, 8});
  }
  CHECK(effects.frames().size() == 2);
  effects.tick(1.1F);
  effects.start({{}, {0, 1, 0}, 8});
  CHECK(effects.frames().size() == 1);
}

TEST_CASE("fence effect data controls its frames timing and refraction") {
  const auto tuning = parse_fence_look(json::parse(R"({"model":"test.gltf","meshes":[3,5],
    "frame_rate":10,"capacity":4,"scale":2,"growth":1,"depth":0.5,"offset":0,
    "refraction":{"push":0.75,"reach":2,"edge":0.5,"least":0.1}})"));
  FenceImpacts effects(tuning);
  effects.start({{}, {0, 1, 0}, 8});
  effects.tick(0.1F);
  const auto frames = effects.frames();
  REQUIRE(frames.size() == 1);
  CHECK(frames[0].mesh == 5);
  CHECK(frames[0].refraction.push == 0.75F);
  effects.tick(0.1F);
  CHECK(effects.frames().empty());
}

TEST_CASE("fence sphere contacts retain tangency without changing collision response") {
  const auto stage = wall(10, 1008);
  Vec3 at{9, 20, 0};
  std::optional<StageHit> contact;
  CHECK((stage.push_sphere(at, 2, &contact) & stage_contact::fence) != 0);
  REQUIRE(contact);
  CHECK(contact->position.x == 10);
  CHECK(contact->normal.x == -1);
  CHECK(std::abs(at.x - 8) < 0.001F);
  contact.reset();
  at.y -= 1; // falling along the wall after it has already pushed the robot out
  CHECK(stage.push_sphere(at, 2, &contact) == 0);
  REQUIRE(contact);
  CHECK(std::abs(contact->position.y - 19) < 0.001F);
  CHECK(at.x == 8);

  for (const Vec3 position : {Vec3{7, 20, 0}, Vec3{11, 20, 0}}) {
    at = position;
    contact.reset();
    stage.push_sphere(at, 2, &contact);
    CHECK(!contact); // separated, or behind the one-sided field
  }
  at = {9, 20, 0};
  wall(10, 9).push_sphere(at, 2, &contact);
  CHECK(!contact);
}

TEST_CASE("falling along the field produces moving contact pulses without steering") {
  for (const int rate : {30, 60, 144}) {
    Battle battle(BattleSettings{}, {});
    battle.set_stage(wall(10, 8));
    battle.add_robot("player", 0, {8, 20, 0}, 0, {}, 100);
    float first_y = 0, last_y = 0;
    int pulses = 0;
    for (int tick = 0; tick < rate / 2; ++tick) {
      battle.tick({}, {}, 1.0F / rate);
      for (const auto &event : battle.events()) {
        if (event.kind != BattleEventKind::fence_contact) {
          continue;
        }
        REQUIRE(event.surface);
        CHECK(event.robot == 0);
        CHECK(std::abs(event.at.x - 10) < 0.001F);
        CHECK(event.surface->normal.x == -1);
        CHECK(std::abs(event.at.y - battle.player().aim_origin().y) < 0.001F);
        if (pulses++ == 0) {
          first_y = event.at.y;
        }
        last_y = event.at.y;
        FenceImpacts effects(look());
        effects.start(*event.surface);
        CHECK(effects.frames().size() == 1);
      }
    }
    CHECK(pulses == 2); // immediate, then one after a quarter second
    CHECK(last_y < first_y);
    CHECK(battle.player().hp == 100);
  }
}

TEST_CASE("contact effects respect ordinary walls disabled tuning and scripted robots") {
  for (const int scenario : {0, 1, 2, 3}) {
    auto settings = parse_battle_settings(json::parse(R"({"fence_contact_seconds":0})"));
    if (scenario != 1) {
      settings.fence_contact_seconds = 0.25F;
    }
    Battle battle(settings, {});
    battle.set_stage(wall(10, scenario == 0 ? 9 : 8));
    battle.add_robot("player", 0, {8, 20, 0}, 0, {}, 100);
    if (scenario == 2) {
      battle.robot(0).mover.script({});
    } else if (scenario == 3) {
      battle.robot(0).alive = false;
    }
    battle.tick({}, {}, 1.0F / 30);
    for (const auto &event : battle.events()) {
      CHECK(event.kind != BattleEventKind::fence_contact);
    }
  }
}

TEST_CASE("a slide along the field leaves a fading, smeared wake of touch ripples") {
  FenceContactLook contact;
  contact.spacing = 1.0F;
  contact.trail = 4;
  FenceImpacts effects(look(), contact);
  // Sliding down 0.5 units a tick: a new ripple every second tick.
  int started = 0;
  for (int tick = 0; tick < 12; ++tick) {
    effects.tick(1.0F / 60);
    started += effects.contact({{10, 20.0F - 0.5F * tick, 0}, {-1, 0, 0}, 8}, 0) ? 1 : 0;
  }
  CHECK(started == 6);
  const auto frames = effects.frames();
  REQUIRE(frames.size() == 4); // the trail's length; the oldest went first
  // The older ripples have faded and are smeared along the slide (y).
  CHECK(frames.front().refraction.push < frames.back().refraction.push);
  const auto centre = transform_point({}, frames.front().world);
  const auto along = transform_point({1, 0, 0}, frames.front().world) - centre;
  const auto across = transform_point({0, 0, 1}, frames.front().world) - centre;
  CHECK(std::abs(along.y) > length(across));
  // Shots keep their own pool beside the touch ripples.
  effects.start({{10, 15, 0}, {-1, 0, 0}, 8});
  CHECK(effects.frames().size() == 5);
}

TEST_CASE("a crowd along the field shares one budget of touch ripples") {
  FenceContactLook contact;
  contact.spacing = 0.1F;
  contact.limit = 5;
  FenceImpacts effects(look(), contact);
  for (int tick = 0; tick < 10; ++tick) {
    effects.tick(1.0F / 60);
    for (int robot = 0; robot < 3; ++robot) {
      const float y = 20.0F - 0.5F * static_cast<float>(tick);
      static_cast<void>(effects.contact({{10, y, 4.0F * robot}, {-1, 0, 0}, 8}, robot));
    }
  }
  CHECK(effects.active_contacts() == 5);
}

TEST_CASE("force-field touch data controls the wake and rejects bad values") {
  const auto tuning =
      parse_fence_contact(json::parse(R"({"spacing":2,"refresh_seconds":0.5,"trail":3,"fade":0})"));
  CHECK(tuning.spacing == 2.0F);
  CHECK(tuning.trail == 3);
  CHECK(tuning.fade == 0.0F);
  CHECK_THROWS(parse_fence_contact(json::parse(R"({"spacing":0})")));
  CHECK_THROWS(parse_fence_contact(json::parse(R"({"trail":0})")));
  CHECK_THROWS(parse_fence_contact(json::parse(R"({"limit":0})")));
}
