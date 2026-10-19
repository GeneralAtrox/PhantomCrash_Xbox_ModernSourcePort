#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/source_lighting.hpp"
#include "phantomcrash/training_ground.hpp"
#include <cmath>

using namespace phantomcrash;

TEST_CASE("source point lights select the nearest two and keep source quadratic falloff") {
  const std::vector<SourceLightVolume> lights{{{0, 1, 0}, {1, 0, 0}, {}, {}, 2},
                                              {{0, 2, 0}, {0, 1, 0}, {}, {}, 4},
                                              {{0, 3, 0}, {0, 0, 1}, {}, {}, 4},
                                              {{0, .5F, 0}, {1, 1, 1}, {}, {}, .1F}};
  const auto samples = sample_source_lights(lights, {});
  CHECK(std::abs(samples[0].toward.y - 1) < 1e-6F);
  CHECK(std::abs(samples[0].colour.x - .75F) < 1e-6F);
  CHECK(samples[0].colour.y == 0);
  CHECK(std::abs(samples[1].colour.y - .75F) < 1e-6F);
  CHECK(samples[1].colour.z == 0);
  const auto dark = sample_source_lights(lights, {0, 10, 0});
  CHECK(length(dark[0].colour) == 0);
  CHECK(length(dark[1].colour) == 0);
}

TEST_CASE("source strip and rectangle lights emit from their nearest transformed point") {
  SourceLightVolume light{{0, 0, 0}, {1, 1, 1}, {0, 2, 0}, {}, 2};
  CHECK(length(source_light_emitter(light, {1, 1, 0}) - Vec3{0, 1, 0}) < 1e-6F);
  CHECK(length(source_light_emitter(light, {1, 4, 0}) - Vec3{0, 2, 0}) < 1e-6F);
  light.half_z = {0, 0, 3};
  CHECK(length(source_light_emitter(light, {1, 1, 2}) - Vec3{0, 1, 2}) < 1e-6F);
  CHECK(length(source_light_emitter(light, {1, -4, 5}) - Vec3{0, -2, 3}) < 1e-6F);
  const auto samples = sample_source_lights(std::span(&light, 1), {1, 1, 2});
  CHECK(std::abs(samples[0].toward.x + 1) < 1e-6F);
  CHECK(std::abs(samples[0].colour.x - .75F) < 1e-6F);
}

TEST_CASE("registered S.L.A.I. stages consume source directions and every local light") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto manifest = roots.find_asset("slai/native_stages.json");
  if (!manifest) {
    return;
  }
  const auto catalogue = json::read_file(*manifest);
  const auto stages = parse_stages(roots.document("world/stages.json"));
  std::size_t count = 0;
  for (const auto &entry : catalogue.find("stages")->array()) {
    const auto &stage = stages.at(entry.string_or("stage", ""));
    CHECK(std::abs(length(stage.lighting.sun_direction) - 1) < 1e-5F);
    count += stage.lighting.source_lights.size();
    for (const auto &light : stage.lighting.source_lights) {
      CHECK(light.radius > 0);
      const auto samples =
          sample_source_lights(stage.lighting.source_lights, light.position + Vec3{0, .01F, 0});
      CHECK(std::isfinite(samples[0].colour.x));
      CHECK(std::isfinite(samples[0].toward.y));
    }
  }
  CHECK(count == 273);
}
