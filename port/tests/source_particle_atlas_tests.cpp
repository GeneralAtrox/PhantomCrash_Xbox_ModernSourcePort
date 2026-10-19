#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/source_particles.hpp"
#include <cmath>

using namespace phantomcrash;

TEST_CASE("source particle alpha uses the half-byte value submitted to GS") {
  SourceGenerator g;
  g.flags = 0x20000 | 0x800000 | 0x400000;
  g.colour_a = {128, 64, 255, 128};
  SourceParticle p;
  p.lifetime = 100;
  const auto colour = source_particle_colour(g, p);
  CHECK(colour[0] == .5F && colour[1] == .25F);
  CHECK(colour[2] == 255.0F / 256 && colour[3] == .5F);
  g.flags = 0;
  CHECK(source_particle_colour(g, p) == (std::array<float, 4>{1, 1, 1, 1}));
}

TEST_CASE("timed source atlases advance by twice the source timestep and wrap strip cells") {
  SourceParticleDefinition d;
  SourceGenerator g;
  g.flags = 1 | 4 | 0x20000 | 0x40000;
  g.stop = 1;
  g.capacity = 1;
  g.rate = 1;
  g.lifetime = 100;
  g.atlas_frames = 4;
  g.atlas_period = 4;
  d.generators.push_back(g);
  SourceParticles system(60, .1F);
  system.start(d, {});
  system.tick(1.01F / 60);
  CHECK(system.emitters()[0].particles[0].atlas_frame == -1);
  CHECK(source_particle_uv(g, system.emitters()[0].particles[0]) ==
        (std::array<float, 4>{.75F, 0, 1, 1}));
  system.tick(1.01F / 60);
  CHECK(system.emitters()[0].particles[0].atlas_frame == 0);
  system.tick(1.01F / 60);
  CHECK(system.emitters()[0].particles[0].atlas_frame == 1);
  system.tick(6.01F / 60);
  CHECK(system.emitters()[0].particles[0].atlas_frame == 0);
}

TEST_CASE("lifetime source atlases use the authored curve and per-cell UVs") {
  SourceParticleDefinition d;
  SourceGenerator g;
  g.flags = 1 | 4 | 0x20000;
  g.stop = 1;
  g.capacity = 1;
  g.rate = 1;
  g.lifetime = 8;
  g.atlas_frames = 8;
  g.atlas_curve = 1;
  d.generators.push_back(g);
  SourceParticles system(60, .1F);
  system.start(d, {});
  system.tick(5.01F / 60);
  const auto &p = system.emitters()[0].particles[0];
  CHECK(p.age == 4 && p.atlas_frame == 3);
  CHECK(source_particle_uv(g, p) == (std::array<float, 4>{.375F, 0, .5F, 1}));
}

TEST_CASE("the shared source pool consumes every horizontal texture strip") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto table = json::read_file(roots.asset("slai/native_particles.json"));
  int strips = 0, timed = 0;
  for (const auto &entry : json::required(table, "pool").array()) {
    const auto definition = parse_source_particles(
        json::read_file(roots.asset(entry.string_or("file", ""))), entry.find("atlas_frames"));
    for (const auto &g : definition.generators) {
      if (g.atlas_frames > 1) {
        ++strips;
        timed += (g.flags & 0x40000) != 0;
        CHECK(g.atlas_frames == 2 || g.atlas_frames == 4 || g.atlas_frames == 8);
      }
    }
  }
  CHECK(strips == 21 && timed == 20);
}
