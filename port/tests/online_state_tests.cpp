#include "test_support.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/online_state.hpp"
#include "../src/online/state_writer.hpp"
#include <bit>

using namespace phantomcrash;
namespace net = phantomcrash::online;
namespace {
template <class T> auto digest(const T &value) {
  net::StateWriter out;
  out(value);
  return out.finish();
}
std::unique_ptr<Battle> fixture(std::uint32_t seed = 123) {
  auto settings = parse_battle_settings(json::parse(R"({
    "weapons":{"gun":{"kind":"gun","damage":7,"cooldown":0.08,"speed":80}}
  })"));
  auto battle = std::make_unique<Battle>(settings, EnemyPath{}, seed);
  battle->add_robot("player", 0, {}, 0, {"gun", "", "", ""}, 100);
  battle->add_robot("target", 1, {0, 0, -10}, 0, {}, 100);
  return battle;
}
} // namespace

TEST_CASE("state writer preserves bits structure and contents without pointer identity") {
  struct Record {
    float value;
    std::array<int, 3> array;
    std::optional<Vec3> point;
    std::vector<bool> flags;
    std::vector<std::string> names;
  };
  Record record{1, {2, 3, 4}, Vec3{5, 6, 7}, {false, true}, {"a", "bc"}};
  auto other = record;
  CHECK(digest(record) == digest(other));
  other.array[2]++;
  CHECK(digest(record) != digest(other));
  other = record;
  other.names = {"ab", "c"};
  CHECK(digest(record) != digest(other));
  other = record;
  other.value = std::bit_cast<float>(std::bit_cast<std::uint32_t>(record.value) + 1);
  CHECK(digest(record) != digest(other));
  CHECK(digest(0.0F) != digest(-0.0F));
  other = record;
  struct References {
    const Record *a;
    const Record *b;
  };
  CHECK(digest(References{&record, &record}) == digest(References{&record, &other}));
  other.flags[0] = true;
  CHECK(digest(References{&record, &record}) != digest(References{&record, &other}));
  CHECK(digest(References{&record, nullptr}) != digest(References{&record, &record}));
  struct MutableReferences {
    Record *a;
    Record *b;
  };
  other = record;
  CHECK(digest(MutableReferences{&record, &record}) != digest(MutableReferences{&record, &other}));
  auto third = record;
  CHECK(digest(MutableReferences{&record, &other}) == digest(MutableReferences{&other, &third}));
  std::minstd_rand random(123);
  const auto before = random;
  const auto first = digest(random);
  CHECK(random == before);
  static_cast<void>(random());
  CHECK(digest(random) != first);
}

TEST_CASE("battle state digest includes invisible AI queues and RNG and survives restore") {
  auto owner = fixture();
  auto &battle = *owner;
  const auto initial = net::state_digest(battle);
  CHECK(net::state_digest(*fixture()) == initial);
  CHECK(net::state_digest(*fixture(124)) != initial);
  const auto saved = battle.checkpoint();
  CHECK(net::state_digest(*saved) == initial);
  battle.robot(0).ai.nav.replan_left = 0.001F;
  CHECK(net::state_digest(battle) != initial);
  battle.restore_checkpoint(*saved);
  CHECK(net::state_digest(battle) == initial);
  battle.robot(0).queued_rounds[3] = 1;
  CHECK(net::state_digest(battle) != initial);
  battle.restore_checkpoint(*saved);
  battle.robot(0).ai.shots++;
  CHECK(net::state_digest(battle) != initial);
  battle.restore_checkpoint(*saved);
  battle.robot(0).magazines[0].charge = 0.01F;
  CHECK(net::state_digest(battle) != initial);
  battle.restore_checkpoint(*saved);
  battle.set_player_target_view(TargetView{});
  CHECK(net::state_digest(battle) != initial);
  battle.restore_checkpoint(*saved);
  battle.tick({}, FireControl{{true, false, false, false}}, 1.0F / 30);
  const auto advanced = net::state_digest(battle);
  CHECK(advanced != initial);
  const auto advanced_copy = battle.checkpoint();
  CHECK(net::state_digest(*advanced_copy) == advanced);
  battle.tick({}, {}, 1.0F / 30);
  battle.restore_checkpoint(*advanced_copy);
  CHECK(net::state_digest(battle) == advanced);
}
