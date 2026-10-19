#include "test_support.hpp"
#include "phantomcrash/source_effect_slots.hpp"
#include "phantomcrash/source_particles.hpp"

using namespace phantomcrash;

TEST_CASE("source effect counts distinguish robot owners and include source wildcards") {
  SourceEffectSlots slots;
  CHECK(slots.allocate(1, 27, std::nullopt, 0).has_value());
  CHECK(slots.allocate(2, 27, std::nullopt, 1).has_value());
  CHECK(slots.allocate(3, 27).has_value());
  CHECK(slots.allocate(4, 26, std::nullopt, 0).has_value());
  CHECK(slots.count(27, 0) == 2);
  CHECK(slots.count(27, 1) == 2);
  CHECK(slots.count(27, 2) == 1);
  slots.release(1);
  CHECK(slots.count(27, 0) == 1);
  CHECK(slots.count(26, 0) == 1);
}
namespace {
void fill(SourceEffectSlots &slots, int resource,
          std::optional<SourceEffectPriority> priority = std::nullopt) {
  for (std::uint64_t handle = 1; handle <= 64; ++handle) {
    const auto allocation = slots.allocate(handle, resource, priority);
    REQUIRE(allocation.has_value());
    CHECK(allocation->slot == handle - 1);
    CHECK(allocation->evicted == 0);
  }
}
SourceParticleDefinition two_generators() {
  SourceGenerator generator;
  generator.flags = 1 | 4;
  generator.rate = 1;
  generator.capacity = 1;
  generator.stop = 1;
  generator.lifetime = 2;
  auto longer = generator;
  longer.lifetime = 6;
  return {{generator, longer}};
}
void advance(SourceParticles &system, int frames = 1) {
  for (int i = 0; i < frames; ++i) {
    system.tick(1.00001F / 60);
  }
}
} // namespace

TEST_CASE("source effect free search uses all 64 entries and reuses the first vacancy") {
  SourceEffectSlots slots;
  fill(slots, 21, SourceEffectPriority{});
  CHECK(slots.size() == 64);
  CHECK(!slots.allocate(65, 21, SourceEffectPriority{}));
  slots.release(4);
  const auto allocation = slots.allocate(65, 21, SourceEffectPriority{});
  REQUIRE(allocation.has_value());
  CHECK(allocation->slot == 3);
  CHECK(slots.contains(65));
  CHECK(!slots.contains(4));
}

TEST_CASE("source ordinary overflow preserves zero-priority actions and unprioritized impacts") {
  SourceEffectSlots slots;
  fill(slots, 18);
  slots.step();
  CHECK(!slots.allocate(65, 21, SourceEffectPriority{}));
  CHECK(!slots.allocate(66, 18));
  CHECK(slots.size() == 64);
  SourceEffectSlots actions;
  fill(actions, 21, SourceEffectPriority{});
  CHECK(!actions.allocate(65, 26));
}

TEST_CASE("protected source arrivals replace the oldest ordinary impact excluding entry zero") {
  SourceEffectSlots slots;
  CHECK(slots.allocate(1, 18).has_value());
  slots.step();
  CHECK(slots.allocate(2, 18).has_value());
  slots.step();
  for (std::uint64_t handle = 3; handle <= 64; ++handle) {
    CHECK(slots.allocate(handle, 26).has_value());
  }
  const auto allocation = slots.allocate(65, 58);
  REQUIRE(allocation.has_value());
  CHECK(allocation->slot == 1);
  CHECK(allocation->evicted == 2);
  CHECK(slots.contains(1));
  CHECK(!slots.contains(2));
  CHECK(slots.size() == 64);
}

TEST_CASE("source recycling compares signed primary then secondary then effect age") {
  SourceEffectSlots slots;
  CHECK(slots.allocate(1, 21, SourceEffectPriority{1, 1}).has_value());
  CHECK(slots.allocate(2, 21, SourceEffectPriority{2, 1}).has_value());
  slots.step();
  CHECK(slots.allocate(3, 21, SourceEffectPriority{2, 1}).has_value());
  CHECK(slots.allocate(4, 26, SourceEffectPriority{127, 127}).has_value());
  CHECK(slots.allocate(5, 21, SourceEffectPriority{-1, 127}).has_value());
  for (std::uint64_t handle = 6; handle <= 64; ++handle) {
    CHECK(slots.allocate(handle, 21, SourceEffectPriority{}).has_value());
  }
  const auto oldest = slots.allocate(65, 21, SourceEffectPriority{2, 2});
  REQUIRE(oldest.has_value());
  CHECK(oldest->evicted == 2);
  const auto secondary = slots.allocate(66, 21, SourceEffectPriority{});
  REQUIRE(secondary.has_value());
  CHECK(secondary->evicted == 65);
  CHECK(slots.contains(4));
}

TEST_CASE("source protected entries cannot be recycled even by another protected arrival") {
  SourceEffectSlots slots;
  fill(slots, 59);
  CHECK(!slots.allocate(65, 26));
  for (int resource : {26, 27, 31, 32, 33, 58, 59}) {
    CHECK(SourceEffectSlots::protected_resource(resource));
  }
  CHECK(!SourceEffectSlots::protected_resource(30));
  CHECK(!SourceEffectSlots::protected_resource(34));
}

TEST_CASE("managed particles count effects rather than generators and retain stopped tails") {
  const auto definition = two_generators();
  SourceParticles system(60, 1);
  CHECK(system.start_managed(definition, {}, 7, 21, SourceEffectPriority{}, 4));
  CHECK(system.managed_effects() == 1);
  CHECK(system.managed_count(21, 4) == 1);
  CHECK(system.managed_count(21, 5) == 0);
  CHECK(system.emitters().size() == 2);
  CHECK(system.start_managed(definition, {}, 7, 21, SourceEffectPriority{}));
  CHECK(system.managed_effects() == 1);
  advance(system);
  system.stop(7);
  advance(system, 3);
  CHECK(system.emitters().size() == 1);
  CHECK(system.managed_effects() == 1);
  CHECK(system.managed_count(21, 4) == 1);
  CHECK(system.start_managed(definition, {}, 7, 21, SourceEffectPriority{}));
  CHECK(system.managed_effects() == 2);
  CHECK(system.managed_count(21, 4) == 2); // old owned tail plus the new wildcard effect
  CHECK(system.managed_count(21, 5) == 1);
  advance(system, 10);
  CHECK(system.managed_effects() == 0);
  CHECK(system.managed_count(21, 4) == 0);
}

TEST_CASE("managed source eviction discards every generator of the selected effect") {
  const auto definition = two_generators();
  SourceParticles system(60, 1);
  for (std::uint64_t key = 1; key <= 64; ++key) {
    CHECK(system.start_managed(definition, {}, key, 18));
  }
  CHECK(system.emitters().size() == 128);
  CHECK(!system.start_managed(definition, {}, 65, 18));
  CHECK(system.start_managed(definition, {}, 66, 26));
  CHECK(system.managed_effects() == 64);
  CHECK(system.emitters().size() == 128);
  CHECK(system.contains(1));
  CHECK(!system.contains(2));
  CHECK(system.contains(66));
}
