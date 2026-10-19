// The CHIPs the port adds (data/tables/added_chips.json): records numbered on
// from the original's 96 and the BUY CHIP offers that list them.

#include "test_support.hpp"

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "phantomcrash/added_chips.hpp"
#include "phantomcrash/chip_shop.hpp"
#include "phantomcrash/contests.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/robot_parts.hpp"

using namespace phantomcrash;

namespace {

constexpr int original_chips = 96;
constexpr int family_records = 50; // ten families of five personalities
constexpr int boss_records = 15;   // then the bosses' CHIPs, one personality each
constexpr int added_records = family_records + boss_records;
constexpr int first_specialist = 111;
constexpr int shop_days = 24;

const char *const original_chip = R"({"index": 0, "name": "Dog I", "inner_shape": 0,
  "inner_value": {"base": 200, "mid": 560, "top": 700, "knee": 60, "low_scale": -1},
  "track": 3, "outer_shape": 2,
  "outer_value": {"base": 200, "mid": 595, "top": 800, "knee": 60, "low_scale": -1},
  "lock_frames": 22, "chatter": 0})";

json::JsonValue added() {
  return DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document(added_chips_document);
}

// A parts table holding `count` chips and no weapons; the original's Rabbits
// (61-70) and enemy-only CHIPs (91 LV5 to 95 LV1) are named as theirs.
json::JsonValue parts_with(const int count) {
  std::string text = R"({"chips": [)";
  for (int index = 0; index < count; ++index) {
    std::string chip = original_chip;
    if (index >= 61 && index < 71) {
      chip.replace(chip.find("Dog I"), 5, index < 66 ? "Rabbit I" : "Rabbit II");
    }
    if (index >= 91 && index <= 95) {
      chip.replace(chip.find("Dog I"), 5, "敵専用素子LV" + std::to_string(96 - index));
    }
    text += (index == 0 ? "" : ",") + chip;
  }
  return json::parse(text + R"(], "arm_weapons": [], "shoulder_weapons": []})");
}

bool near(const float a, const float b) {
  return std::fabs(a - b) < 1e-3F;
}

} // namespace

TEST_CASE("the added CHIPs follow the original's, one record per personality") {
  auto parts = parts_with(original_chips);
  CHECK(add_chips(parts, added()));
  const PartTables tables = parse_part_tables(parts);
  const int last = original_chips + added_records;
  CHECK(tables.chips.size() == static_cast<std::size_t>(last));
  const std::array<const char *, 10> names{"Cat V",    "Cat VI",     "Cat VII", "Fox I",
                                           "Bear I",   "Tortoise I", "Boar I",  "Hawk I",
                                           "Monkey I", "Meerkat I"};
  for (int index = original_chips; index < original_chips + family_records; ++index) {
    const ChipRecord *chip = tables.chip(index);
    CHECK(chip != nullptr);
    if (chip == nullptr) {
      return;
    }
    const int offset = index - original_chips;
    CHECK(chip->index == index);
    CHECK(chip->name == names[static_cast<std::size_t>(offset / 5)]);
    CHECK(chip->chatter == offset % 5);
    CHECK(chip->face && chip->face->rect[2] == 64.0F && chip->face->rect[3] == 64.0F);
  }
  const ChipRecord &norwegian = *tables.chip(96);
  CHECK(norwegian.face->image == "textures/chips/cat_v.png");
  CHECK(norwegian.track == 3 && norwegian.lock_frames == 8);
  CHECK(near(norwegian.dodge_percent.value(part_level_max), 10.5F));
  const ChipRecord &maine_coon = *tables.chip(101);
  CHECK(maine_coon.lock_frames == 10);
  CHECK(near(maine_coon.critical_percent.value(part_level_max), 8.0F));
  CHECK(near(maine_coon.dodge_percent.value(part_level_max), 7.0F));
  const ChipRecord &ragdoll = *tables.chip(106);
  CHECK(ragdoll.track == 1 && ragdoll.lock_frames == 22);
  CHECK(near(ragdoll.knockdown_resistance.value(part_level_max), 18.5F));
  // The specialists, each best at one thing.
  CHECK(near(tables.chip(111)->critical_percent.value(part_level_max), 18.0F));
  CHECK(tables.chip(116)->face->image == "textures/chips/bear.png");
  CHECK(tables.chip(116)->track == 10);
  CHECK(near(tables.chip(121)->dodge_percent.value(part_level_max), 20.0F));
  CHECK(near(tables.chip(126)->knockdown_resistance.value(part_level_max), 32.0F));
  CHECK(tables.chip(126)->outer_shape == 1);
  CHECK(tables.chip(131)->lock_frames == 4 && tables.chip(131)->inner_shape == 1);
  const ChipRecord &mandrill = *tables.chip(136);
  CHECK(mandrill.inner_shape == 2);
  CHECK(near(mandrill.critical_percent.value(15), 5.0F));
  CHECK(near(mandrill.critical_percent.value(part_level_max), 5.5F));
  const ChipRecord &meerkat = *tables.chip(141);
  CHECK(meerkat.face->image == "textures/chips/meerkat.png");
  CHECK(meerkat.inner_shape == 0 && meerkat.outer_shape == 2 && meerkat.lock_frames == 6);
  CHECK(near(meerkat.outer_value.value(40), 999.0F));
  // The bosses' CHIPs wear their portraits from the original's atlas.
  const ChipRecord &retriever = *tables.chip(146);
  CHECK(retriever.name == "Retriever");
  CHECK(retriever.face->image == "textures/scroll/fight_chara00_tex/0.png");
  CHECK(retriever.face->rect[0] == 320.0F && retriever.face->rect[1] == 64.0F);
  CHECK(retriever.face->rect[2] == 384.0F && retriever.face->rect[3] == 128.0F);
  CHECK(tables.chip(151)->name == "Turtle");
  const ChipRecord &labrador = *tables.chip(last - 1);
  CHECK(labrador.name == "Labrador" && labrador.chatter == 4);
  CHECK(labrador.face->rect[0] == 0.0F && labrador.face->rect[1] == 0.0F);
  // Kudryavka's, the best of the best: never loses its weapons at level 99.
  CHECK(labrador.track == 7 && labrador.lock_frames == 6);
  CHECK(near(labrador.knockdown_resistance.value(0), 5.0F));
  CHECK(near(labrador.knockdown_resistance.value(50), 30.0F));
  CHECK(near(labrador.knockdown_resistance.value(part_level_max), 50.0F));
  CHECK(near(labrador.critical_percent.value(part_level_max), 16.0F));
  CHECK(near(labrador.dodge_percent.value(part_level_max), 17.0F));
  CHECK(near(labrador.inner_value.value(part_level_max), 999.0F));
}

TEST_CASE("the retune gives the original's Rabbits their jump-fighter stats") {
  auto parts = parts_with(original_chips);
  CHECK(add_chips(parts, added()));
  const PartTables tables = parse_part_tables(parts);
  for (const int index : {61, 70}) {
    const ChipRecord &rabbit = *tables.chip(index);
    CHECK(rabbit.lock_frames == 10);
    CHECK(near(rabbit.inner_value.value(35), 999.0F));
    CHECK(near(rabbit.outer_value.value(0), 500.0F));
    CHECK(near(rabbit.dodge_percent.value(part_level_max), 12.0F));
    CHECK(rabbit.inner_shape == 0); // the fixture's; the retune leaves shapes alone
  }
  CHECK(tables.chip(60)->lock_frames == 22); // not a Rabbit
  // The enemy-only CHIPs' Aim Speed, which the original left at 1.
  CHECK(tables.chip(91)->track == 6);
  CHECK(tables.chip(93)->track == 4);
  CHECK(tables.chip(95)->track == 2);
  CHECK(tables.chip(90)->track == 3); // the fixture's
}

TEST_CASE("the added CHIPs leave a table alone that does not end at the original's last") {
  auto parts = parts_with(3);
  CHECK(!add_chips(parts, added()));
  CHECK(parse_part_tables(parts).chips.size() == 3U);
}

TEST_CASE("each shop day offers one added cat and one specialist, in turn") {
  std::string text = R"({"chip_lineups": [)";
  for (int day = 0; day < shop_days; ++day) {
    text += day == 0 ? "[]" : ",[]";
  }
  auto garage = json::parse(text + "]}");
  add_chip_lineups(garage, added());
  const auto &days = json::required(garage, "chip_lineups").array();
  for (int day = 0; day < shop_days; ++day) {
    const auto &offers = days[static_cast<std::size_t>(day)].array();
    CHECK(offers.size() == 2U);
    if (offers.size() != 2U) {
      return;
    }
    const int cat = original_chips + day % 3 * 5 + day / 3 % 5;
    const int specialist = first_specialist + day % 7 * 5 + day % 5;
    CHECK(json::required(offers[0], "id").number() == cat);
    CHECK(json::required(offers[1], "id").number() == specialist);
    CHECK(json::required(offers[1], "level").number() == 0.0);
  }
  const auto &bosses = json::required(garage, "boss_chips").array();
  CHECK(bosses.size() == static_cast<std::size_t>(boss_records));
  const auto &atsurou = json::required(bosses.front(), "contests").array();
  CHECK(json::required(bosses.front(), "id").number() == 146.0);
  CHECK(json::required(bosses.front(), "level").number() == 0.0);
  CHECK(atsurou.size() == 2U && atsurou[0].number() == 3.0 && atsurou[1].number() == 7.0);
}

TEST_CASE("a boss beaten sells its CHIP once on the days its contest is held") {
  const PartTables parts = parse_part_tables(parts_with(3));
  GarageTables garage;
  garage.boss_chips = {{1, 0, {3, 7}}, {2, 0, {14}}};
  ContestTables contests;
  contests.calendar.resize(2);
  contests.calendar[0].venues = {3, 7, 14};
  contests.calendar[1].venues = {5, no_contest, no_contest};
  const auto sold = [&](const int day, const std::vector<int> &won,
                        std::vector<ChipOffer> offers = {}) {
    add_boss_chips(offers, parts, garage, contests, day, won);
    std::vector<int> ids;
    for (const ChipOffer &offer : offers) {
      ids.push_back(offer.chip);
    }
    return ids;
  };
  const std::vector<int> retriever{1};
  const std::vector<int> sparrow{2};
  const std::vector<int> both{1, 2};
  CHECK(sold(0, {}).empty());
  // Both of the boss's contests are held, but its CHIP is on sale once.
  CHECK(sold(0, {7}) == retriever);
  const std::vector<int> won_both = sold(0, {7, 14});
  CHECK(won_both == both);
  const std::vector<int> other_day = sold(1, {3, 7, 14});
  CHECK(other_day.empty());
  CHECK(sold(2, {14}) == sparrow); // the calendar repeats
  const std::vector<int> listed = sold(0, {3}, {ChipOffer{1}});
  CHECK(listed == retriever); // already in the lineup
}
