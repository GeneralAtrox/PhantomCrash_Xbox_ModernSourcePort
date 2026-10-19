#include "test_support.hpp"
#include "../src/desktop_replay_archive.hpp"
#include <cmath>
#include <filesystem>
#include <set>

using namespace phantomcrash;

TEST_CASE("replay round trip preserves submitted world, HUD, monitor and sound state") {
  ReplayArchive tape;
  ReplayFrame frame;
  frame.player.width = 1920;
  frame.player.height = 1080;
  frame.player.eye = {1, 2, 3};
  frame.player.view = translation({4, 5, 6});
  frame.player.projection = scaling({2, 3, 4});
  frame.player.shadow_length = 42;
  frame.player.transparent = true;
  frame.player.lighting.fog = true;
  frame.player.lighting.fog_end = 123;
  frame.player.lighting.source_lights = {{{1, 2, 3}, {2, 1, .5F}, {4, 0, 0}, {}, 7}};
  frame.player.lighting.screen.depth_of_field = true;
  WorldItem item;
  item.robot = 3;
  item.model = 9;
  item.world = translation({7, 8, 9});
  item.sky_nodes = {"cloud"};
  item.sky_motion = {{"cloud", .3F, {.2F, .1F}}};
  item.sky_seconds = 45;
  item.monitor_nodes = {"screen"};
  item.node_worlds = {translation({3, 6, 9})};
  item.paint = 5;
  item.alpha = .6F;
  item.flash = .2F;
  item.node = 6;
  item.colour = std::array<float, 4>{1, .8F, .5F, .3F};
  item.uv_offset = {.1F, .2F};
  item.scale_x = 2;
  item.additive = true;
  item.optical = OpticalSphere{};
  item.casts_shadow = true;
  item.double_sided = true;
  item.fogged = false;
  frame.player.items = {item};
  frame.player.overlay = {item};
  frame.hidden_player = {item};
  WorldSprite sprite;
  sprite.texture = 13;
  sprite.streak = true;
  sprite.position = {4, 7, 9};
  sprite.end = {8, 10, 12};
  sprite.size = 3;
  sprite.rotation = .3F;
  sprite.subtractive = true;
  sprite.depth_test = false;
  sprite.fixed_plane = true;
  sprite.plane_right = {0, 0, -1};
  sprite.plane_up = {0, -1, 0};
  sprite.uv = {.2F, .3F, .8F, .9F};
  sprite.colour = {.4F, .5F, .6F, .7F};
  sprite.additive = true;
  sprite.particle_kind = 2;
  sprite.cluster_centre = {13, 14, 15};
  frame.player.sprites = {sprite};
  frame.monitor = frame.player;
  ReplaySubject subject;
  subject.name = "Pilot";
  subject.alive = true;
  subject.camera.yaw = .8F;
  subject.cockpit.armour = .4F;
  frame.subjects = {subject};
  ReplayQuad quad;
  quad.texture = reinterpret_cast<SDL_Texture *>(static_cast<uintptr_t>(123));
  quad.source = {1, 2, 3, 4};
  quad.target = {5, 6, 7, 8};
  quad.pivot = {2, 3};
  quad.angle = 42;
  quad.clipped = true;
  quad.clip = {1, 2, 30, 40};
  quad.blend = SDL_BLENDMODE_ADD;
  quad.colour = {12, 34, 56, 78};
  quad.flip = SDL_FLIP_HORIZONTAL;
  frame.overlay = {quad};
  frame.music = "battle.wav";
  frame.sounds = {{SoundCue::Action::play, "laser", {.4F, -.2F}, true, 1234}};
  frame.sounds[0].position = Vec3{2, 4, 6};
  SoundCue listener;
  listener.action = SoundCue::Action::listener;
  listener.listener.eye = {10, 20, 30};
  listener.falloff.rolloff = .2F;
  frame.sounds.push_back(listener);
  tape.append(frame);
  const auto out = tape.read(0);
  CHECK(out.player.width == 1920);
  CHECK(out.player.height == 1080);
  CHECK(out.player.eye.y == 2);
  CHECK(out.player.view.m == frame.player.view.m);
  CHECK(out.player.projection.m == frame.player.projection.m);
  CHECK(out.player.lighting.fog);
  CHECK(out.player.lighting.fog_end == 123);
  CHECK(out.player.lighting.screen.depth_of_field);
  CHECK(out.player.shadow_length == 42);
  REQUIRE(out.player.lighting.source_lights.size() == 1);
  CHECK(out.player.lighting.source_lights[0].position.z == 3);
  CHECK(out.player.lighting.source_lights[0].colour.x == 2);
  CHECK(out.player.lighting.source_lights[0].half_x.x == 4);
  CHECK(out.player.lighting.source_lights[0].radius == 7);
  CHECK(out.player.transparent);
  const auto &copy = out.player.items.at(0);
  CHECK(copy.robot == 3);
  CHECK(copy.model == 9);
  CHECK(copy.world.m == item.world.m);
  CHECK(copy.sky_nodes == item.sky_nodes);
  CHECK(copy.sky_motion[0].node == "cloud");
  CHECK(copy.sky_motion[0].rotation_per_second == .3F);
  CHECK(copy.sky_motion[0].uv_per_second == item.sky_motion[0].uv_per_second);
  CHECK(copy.sky_seconds == 45);
  CHECK(copy.monitor_nodes == item.monitor_nodes);
  CHECK(copy.node_worlds[0].m == item.node_worlds[0].m);
  CHECK(copy.paint == 5);
  CHECK(copy.alpha == .6F);
  CHECK(copy.flash == .2F);
  CHECK(copy.node == 6);
  CHECK(copy.colour == item.colour);
  CHECK(copy.uv_offset == item.uv_offset);
  CHECK(copy.scale_x == 2);
  CHECK(copy.additive);
  CHECK(copy.optical.has_value());
  CHECK(copy.casts_shadow);
  CHECK(copy.double_sided);
  CHECK(!copy.fogged);
  CHECK(out.player.overlay[0].model == 9);
  CHECK(out.hidden_player[0].robot == 3);
  CHECK(out.player.sprites[0].texture == 13);
  CHECK(out.player.sprites[0].subtractive);
  CHECK(!out.player.sprites[0].depth_test);
  CHECK(out.player.sprites[0].fixed_plane);
  CHECK(out.player.sprites[0].plane_right.z == -1);
  CHECK(out.player.sprites[0].plane_up.y == -1);
  CHECK(out.player.sprites[0].uv == sprite.uv);
  CHECK(out.player.sprites[0].colour == sprite.colour);
  CHECK(out.player.sprites[0].particle_kind == 2);
  CHECK(out.player.sprites[0].cluster_centre.z == 15);
  CHECK(out.monitor->items[0].model == 9);
  CHECK(out.subjects[0].name == "Pilot");
  CHECK(out.subjects[0].camera.yaw == .8F);
  CHECK(out.subjects[0].cockpit.armour == .4F);
  CHECK(out.overlay[0].texture == quad.texture);
  CHECK(out.overlay[0].target.w == 7);
  CHECK(out.overlay[0].source.h == 4);
  CHECK(out.overlay[0].angle == 42);
  CHECK(out.overlay[0].clipped);
  CHECK(out.overlay[0].clip.w == 30);
  CHECK(out.overlay[0].colour.a == 78);
  CHECK(out.overlay[0].blend == SDL_BLENDMODE_ADD);
  CHECK(out.overlay[0].flip == SDL_FLIP_HORIZONTAL);
  CHECK(out.overlay[0].pivot.y == 3);
  CHECK(out.music == "battle.wav");
  CHECK(out.sounds[0].name == "laser");
  CHECK(out.sounds[0].key == 1234);
  CHECK(out.sounds[0].mix.pan == -.2F);
  REQUIRE(out.sounds[0].position.has_value());
  CHECK(out.sounds[0].position->z == 6);
  CHECK(out.sounds[1].action == SoundCue::Action::listener);
  CHECK(out.sounds[1].listener.eye.y == 20);
  CHECK(out.sounds[1].falloff.rolloff == .2F);
}

TEST_CASE("replay lookup follows recorded time regardless of presentation cadence") {
  ReplayArchive tape;
  ReplayFrame frame;
  for (int i = 0; i < 61; ++i) {
    frame.seconds = 5 + i / 60.0;
    CHECK(tape.accepts(frame.seconds));
    tape.append(frame);
    CHECK(!tape.accepts(frame.seconds));
    tape.append(frame); // a repeated presentation does not advance the clock
  }
  CHECK(tape.size() == 61);
  for (int i = 0; i < 61; ++i) {
    CHECK(tape.at(i / 60.0) == static_cast<std::size_t>(i));
  }
  CHECK(std::abs(tape.duration() - 1) < 1e-6);
  for (const double rate : {30.0, 60.0, 144.0}) {
    const double clock = rate * (1.0 / rate);
    CHECK(std::abs(tape.read(tape.at(clock)).seconds - 1) < .017);
  }
  CHECK(tape.at(-1) == 0);
  CHECK(tape.at(100) == 60);
}

TEST_CASE("replay spool is deleted when its owning result is released") {
  const auto names = [] {
    std::set<std::filesystem::path> files;
    for (const auto &file :
         std::filesystem::directory_iterator(std::filesystem::temp_directory_path())) {
      if (file.path().filename().string().starts_with("pcr")) {
        files.insert(file.path());
      }
    }
    return files;
  };
  const auto before = names();
  std::filesystem::path created;
  {
    ReplayArchive tape;
    for (const auto &path : names()) {
      if (!before.contains(path)) {
        created = path;
      }
    }
    REQUIRE(!created.empty());
    tape.append(ReplayFrame{});
  }
  CHECK(!std::filesystem::exists(created));
}
